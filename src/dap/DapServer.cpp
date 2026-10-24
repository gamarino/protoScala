/*
 * DapServer — `protoscala --dap`: the Debug Adapter Protocol over stdin/stdout
 * (docs/debugging.md).
 *
 * Threads:
 *  - The protocol loop runs on the main thread. It is not a protoCore thread
 *    and never touches a ProtoObject, so it never needs an unmanaged region.
 *  - `launch` starts the debuggee: a launcher thread runs the evaluator thread
 *    (StackGuard.h), which waits for `configurationDone`, then creates the
 *    Session -- and with it the ProtoSpace -- and runs the script exactly as
 *    `protoscala file.scala` does.
 *  - A thread that reaches a breakpoint, a step target or an exception stop
 *    calls stopped() from inside the interpreter. Only that thread stops: it
 *    parks in an unmanaged region on its own context, so a collection never
 *    waits for it, and serves the requests that need the language -- rendering
 *    locals, evaluating an expression -- itself, on its own context, in managed
 *    mode. The protocol thread hands those to it (one queue per stopped thread)
 *    and waits. Values are therefore read only by the thread whose frames root
 *    them, and only while those frames are stopped. Other threads -- actor
 *    workers, Thread.start bodies -- keep running, and several can be stopped
 *    at once; each is resumed, stepped and inspected by its own thread id.
 *  - DapStdio forwards the script's stdout and stderr as `output` events.
 *
 * Frame ids come from one counter for the whole session, so they are unique
 * across threads; a variablesReference is 2 * frameId (+1 for the globals).
 */
#include "dap/DapServer.h"
#include "dap/DapStdio.h"
#include "dap/DapTransport.h"

#include "frontend/Parser.h"
#include "repl/Session.h"
#include "runtime/Debugger.h"
#include "runtime/StackGuard.h"
#include "protoCore.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace protoScala {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxValueChars = 4000;

bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    const auto c0 = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

std::string clip(std::string s) {
    if (s.size() <= kMaxValueChars) return s;
    std::size_t cut = kMaxValueChars;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;  // UTF-8 boundary
    s.resize(cut);
    return s + "...";
}

// The parse error of `expr` as a debugger expression, or "" when it parses.
std::string syntaxError(const std::string& expr) {
    try {
        parseSource("(e) => {\n" + expr + "\n}\n");
    } catch (const ParseError& e) {
        return e.what();
    } catch (const std::exception& e) {
        return e.what();
    }
    return std::string();
}

std::string baseName(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// A frame of a stopped thread, as the client is shown it.
struct FrameView {
    int id;  // the DAP frame id, unique in the session
    debug::FrameRecord* rec;
    std::string name;
    std::string path;
    int line;
};

// A request a stopped thread runs for the protocol thread.
struct Work {
    std::function<void()> fn;
    bool done = false;
};

// One stopped thread. Shared between the thread (which owns the frames) and
// the protocol thread; guarded by DapServer::mu_.
struct StoppedThread {
    int id = 0;
    debug::FrameRecord* top = nullptr;
    proto::ProtoContext* current = nullptr;  // the thread's innermost live context
    std::vector<FrameView> frames;  // innermost first
    std::deque<std::shared_ptr<Work>> work;
    bool resumeReady = false;
    debug::StepMode resumeMode = debug::StepMode::None;
    bool gone = false;  // resumed: its frames are no longer valid
    std::optional<debug::ExceptionStop> exception;
};

class DapServer : public debug::Controller {
public:
    DapServer() : transport_(stdio_.protocolIn(), stdio_.protocolOut()) {
        // stdout is a pipe now, which C buffers fully: what the script prints
        // would reach the Debug Console only in 4 KiB pieces or at a stop.
        std::setvbuf(stdout, nullptr, _IONBF, 0);
    }

    ~DapServer() override { stdio_.finish(nullptr); }

    int run() {
        stdio_.startForwarding([this](const char* category, const std::string& text) {
            sendOutput(category, text);
        });
        for (;;) {
            std::optional<json> msg = transport_.readMessage();
            if (!msg) break;  // the client went away
            if (msg->value("type", "") != "request") continue;
            if (handle(*msg)) break;
        }
        return shutdown();
    }

    // --- debug::Controller (on the debuggee's threads) ----------------------
    void stopped(debug::FrameRecord& top, proto::ProtoContext* current, const char* reason,
                 int threadId, const debug::ExceptionStop* ex, const std::string* note) override {
        proto::ProtoContext* ctx = current;
        // A line printed without a newline is still in stdio's buffer.
        std::fflush(stdout);
        std::fflush(stderr);
        auto st = std::make_shared<StoppedThread>();
        st->id = threadId;
        st->top = &top;
        st->current = current;
        if (ex) st->exception = *ex;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (shuttingDown_) return;
            for (debug::FrameRecord* r = &top; r; r = r->parent) {
                if (!r->src) continue;  // the prelude: no source to show
                const int id = nextFrameId_++;
                st->frames.push_back({id, r, r->mod->name(), r->src->path, r->line});
                frameOwner_[id] = threadId;
            }
            stopped_[threadId] = st;
            lastStopped_ = threadId;
        }
        json body = {{"reason", reason}, {"threadId", threadId}, {"allThreadsStopped", false}};
        if (ex) {
            body["description"] = ex->description;
            body["text"] = ex->description;
        }
        if (note) {
            body["description"] = *note;
            body["text"] = ex ? ex->description + "\n" + *note : *note;
        }
        sendEvent("stopped", body);

        debug::StepMode mode = debug::StepMode::None;
        for (;;) {
            std::shared_ptr<Work> work;
            {
                proto::ProtoContext::UnmanagedScope unmanaged(ctx);
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return st->resumeReady || !st->work.empty(); });
                if (!st->work.empty()) {
                    work = st->work.front();
                    st->work.pop_front();
                } else {
                    mode = st->resumeMode;
                    st->gone = true;
                    for (const FrameView& f : st->frames) frameOwner_.erase(f.id);
                    auto it = stopped_.find(threadId);
                    if (it != stopped_.end() && it->second == st) stopped_.erase(it);
                }
            }
            if (!work) break;
            {
                debug::Suppress quiet;  // the debugger's own code never stops
                try {
                    work->fn();
                } catch (...) {
                }
            }
            {
                std::lock_guard<std::mutex> lk(mu_);
                work->done = true;
            }
            cv_.notify_all();
        }
        cv_.notify_all();
        // Output the requests printed (an evaluated println) goes out now.
        std::fflush(stdout);
        if (mode != debug::StepMode::None) debug::setStep(mode, top);
    }

    void breakpointsResolved(const std::vector<Breakpoint>& bps) override {
        for (const Breakpoint& b : bps)
            sendEvent("breakpoint", {{"reason", "changed"},
                                     {"breakpoint", {{"id", b.id}, {"verified", b.verified},
                                                     {"line", b.line}}}});
    }

    Evaluation evaluateHere(proto::ProtoContext* current, debug::FrameRecord& frame,
                            const std::string& expr, const char* boundName,
                            const proto::ProtoObject* bound, bool asText) override {
        Evaluation out;
        if (!session_) {
            out.text = "no session";
            return out;
        }
        proto::ProtoContext scope(current->space, current);
        out.ok = evaluateIn(&scope, frame, asText ? "\"\" + (" + expr + ")" : expr, boundName,
                            bound, &out.text);
        // `"" + (x)` is a String: its text without the REPL's quotes.
        if (out.ok && asText && out.text.size() >= 2 && out.text.front() == '"' &&
            out.text.back() == '"')
            out.text = out.text.substr(1, out.text.size() - 2);
        return out;
    }

    void output(const char* category, const std::string& text) override {
        sendOutput(category, text);
    }

    void threadStarted(int threadId) override {
        sendEvent("thread", {{"reason", "started"}, {"threadId", threadId}});
    }
    void threadExited(int threadId) override {
        sendEvent("thread", {{"reason", "exited"}, {"threadId", threadId}});
    }

private:
    // Answers true when the loop should end.
    bool handle(const json& req) {
        const std::string command = req.value("command", "");
        if (command == "initialize") {
            sendResponse(req,
                         {{"supportsConfigurationDoneRequest", true},
                          {"supportsEvaluateForHovers", true},
                          {"supportsExceptionInfoRequest", true},
                          {"supportsExceptionFilterOptions", true},
                          {"supportsConditionalBreakpoints", true},
                          {"supportsHitConditionalBreakpoints", true},
                          {"supportsLogPoints", true},
                          {"supportsTerminateThreadsRequest", false},
                          {"exceptionBreakpointFilters",
                           json::array({{{"filter", "raised"},
                                         {"label", "All Exceptions"},
                                         {"description",
                                          "Stop wherever an exception is thrown, caught or not"},
                                         {"default", false},
                                         {"supportsCondition", true},
                                         {"conditionDescription",
                                          "The exception is `e`, e.g. "
                                          "e.isInstanceOf[IllegalStateException]"}},
                                        {{"filter", "userUnhandled"},
                                         {"label", "Uncaught Exceptions"},
                                         {"description",
                                          "Stop where an exception is thrown that no try/catch "
                                          "of the program will catch"},
                                         {"default", true},
                                         {"supportsCondition", true},
                                         {"conditionDescription",
                                          "The exception is `e`, e.g. "
                                          "e.getMessage.contains(\"timeout\")"}}})}});
            sendEvent("initialized", json::object());
        } else if (command == "launch") {
            handleLaunch(req);
        } else if (command == "setBreakpoints") {
            handleSetBreakpoints(req);
        } else if (command == "setExceptionBreakpoints") {
            handleSetExceptionBreakpoints(req);
        } else if (command == "exceptionInfo") {
            handleExceptionInfo(req);
        } else if (command == "configurationDone") {
            sendResponse(req, json::object());
            {
                std::lock_guard<std::mutex> lk(mu_);
                configDone_ = true;
            }
            cv_.notify_all();
        } else if (command == "threads") {
            handleThreads(req);
        } else if (command == "continue" || command == "next" || command == "stepIn" ||
                   command == "stepOut") {
            handleResume(req, command);
        } else if (command == "pause") {
            const int threadId = req.value("arguments", json::object()).value("threadId", 0);
            // A thread in native code or blocked stops when it is back in the
            // interpreter; the response does not wait for it.
            if (threadId == 0 || !debug::requestPause(threadId)) debug::requestStop("pause");
            sendResponse(req, json::object());
        } else if (command == "stackTrace") {
            handleStackTrace(req);
        } else if (command == "scopes") {
            handleScopes(req);
        } else if (command == "variables") {
            handleVariables(req);
        } else if (command == "evaluate") {
            handleEvaluate(req);
        } else if (command == "disconnect" || command == "terminate") {
            sendResponse(req, json::object());
            return true;
        } else {
            sendError(req, "unsupported request: " + command);
        }
        return false;
    }

    // --- launch -------------------------------------------------------------
    void handleLaunch(const json& req) {
        const json args = req.value("arguments", json::object());
        program_ = args.value("program", "");
        if (program_.empty()) {
            sendError(req, "launch: 'program' names no script");
            return;
        }
        if (launcher_.joinable()) {
            sendError(req, "launch: a script is already running");
            return;
        }
        if (args.contains("args") && args["args"].is_array())
            for (const json& a : args["args"])
                if (a.is_string()) args_.push_back(a.get<std::string>());
        const std::string cwd = args.value("cwd", "");
        if (!cwd.empty()) {
            std::error_code ec;
            std::filesystem::current_path(std::filesystem::path(cwd), ec);
            if (ec) sendOutput("stderr", "protoscala: cannot change to '" + cwd + "'\n");
        }
        // The script's path as the session will name it in frames, absolute so
        // that the client can open it whatever its working directory.
        {
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(program_), ec);
            if (!ec) program_ = abs.lexically_normal().string();
        }
        noDebug_ = args.value("noDebug", false);
        if (!noDebug_) {
            debug::attach(this);
            if (args.value("stopOnEntry", false)) debug::requestStop("entry");
        }
        launcher_ = std::thread([this] { launcherMain(); });
        sendResponse(req, json::object());
    }

    void launcherMain() {
        int rc = 1;
        try {
            rc = runOnEvaluatorThread(
                [](void* self) { return static_cast<DapServer*>(self)->evaluatorMain(); }, this);
        } catch (const std::exception& e) {
            std::fflush(stdout);
            std::fprintf(stderr, "protoscala: internal error: %s\n", e.what());
        } catch (...) {
            std::fflush(stdout);
            std::fprintf(stderr, "protoscala: internal error\n");
        }
        // The script's output goes out before `terminated`.
        stdio_.finish(nullptr);
        sendEvent("terminated", json::object());
        sendEvent("exited", {{"exitCode", rc}});
        {
            std::lock_guard<std::mutex> lk(mu_);
            finished_ = true;
        }
        cv_.notify_all();
    }

    // On the evaluator thread.
    int evaluatorMain() {
        {
            // No ProtoSpace exists yet: this thread is not registered anywhere.
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return configDone_ || shuttingDown_; });
            if (shuttingDown_) return 0;
        }
        debug::nameThread("main");
        Session session;
        {
            std::lock_guard<std::mutex> lk(mu_);
            session_ = &session;
        }
        int rc = 1;
        try {
            rc = session.runScript(program_, args_);
        } catch (const proto::ProtoException&) {
            std::fflush(stdout);
            std::fprintf(stderr, "protoscala: uncaught exception\n");
        }
        std::fflush(stdout);
        {
            std::lock_guard<std::mutex> lk(mu_);
            session_ = nullptr;
        }
        return rc;
    }

    // --- breakpoints --------------------------------------------------------
    void handleSetBreakpoints(const json& req) {
        const json args = req.value("arguments", json::object());
        const std::string path = args.value("source", json::object()).value("path", "");
        std::vector<debug::BreakpointSpec> specs;
        if (args.contains("breakpoints") && args["breakpoints"].is_array()) {
            for (const json& b : args["breakpoints"]) {
                debug::BreakpointSpec sp;
                sp.line = b.value("line", 0);
                sp.condition = b.value("condition", "");
                sp.hitCondition = b.value("hitCondition", "");
                sp.isLogpoint = b.contains("logMessage") && b["logMessage"].is_string();
                if (sp.isLogpoint) sp.logMessage = b["logMessage"].get<std::string>();
                if (!sp.condition.empty()) {
                    const std::string err = syntaxError(sp.condition);
                    if (!err.empty())
                        sendOutput("stderr", "breakpoint condition `" + sp.condition +
                                                 "` failed: " + err + "\n");
                }
                specs.push_back(std::move(sp));
            }
        } else if (args.contains("lines") && args["lines"].is_array()) {
            for (const json& l : args["lines"]) {
                debug::BreakpointSpec sp;
                sp.line = l.get<int>();
                specs.push_back(std::move(sp));
            }
        }
        json out = json::array();
        if (!path.empty()) {
            for (const Breakpoint& b : debug::setBreakpoints(path, specs)) {
                json e = {{"id", b.id}, {"verified", b.verified}, {"line", b.line}};
                if (!b.verified) e["message"] = b.message;
                out.push_back(std::move(e));
            }
        }
        sendResponse(req, {{"breakpoints", out}});
    }

    void handleSetExceptionBreakpoints(const json& req) {
        const json args = req.value("arguments", json::object());
        unsigned mask = 0;
        std::string conditions[2];
        json answered = json::array();
        auto take = [&](const std::string& f, const std::string& condition) {
            int bit = -1;
            if (f == "raised") bit = 0;
            else if (f == "userUnhandled" || f == "uncaught") bit = 1;
            if (bit < 0) {
                answered.push_back({{"verified", false}, {"message", "unknown filter " + f}});
                return;
            }
            mask |= 1u << bit;
            conditions[bit] = condition;
            json b = {{"verified", true}};
            if (!condition.empty()) {
                const std::string err = syntaxError(condition);
                if (!err.empty()) {
                    // Kept: the stop reports the failure where it happens.
                    const std::string msg =
                        "exception breakpoint condition `" + condition + "` failed: " + err;
                    sendOutput("stderr", msg + "\n");
                    b["message"] = msg;
                }
            }
            answered.push_back(std::move(b));
        };
        if (args.contains("filters") && args["filters"].is_array())
            for (const json& f : args["filters"])
                if (f.is_string()) take(f.get<std::string>(), std::string());
        if (args.contains("filterOptions") && args["filterOptions"].is_array())
            for (const json& o : args["filterOptions"])
                take(o.value("filterId", ""), o.value("condition", ""));
        debug::setExceptionBreaks(mask, conditions[0], conditions[1]);
        sendResponse(req, {{"breakpoints", answered}});
    }

    // --- threads ------------------------------------------------------------
    void handleThreads(const json& req) {
        json threads = json::array();
        for (const debug::ThreadView& t : debug::threads())
            threads.push_back({{"id", t.id}, {"name", t.name}});
        // Before the script runs (or after the main thread ended) VS Code still
        // needs a thread to show.
        if (threads.empty()) threads.push_back({{"id", 1}, {"name", "main"}});
        sendResponse(req, {{"threads", threads}});
    }

    // --- resuming -----------------------------------------------------------
    void handleResume(const json& req, const std::string& command) {
        debug::StepMode mode = debug::StepMode::None;
        if (command == "next") mode = debug::StepMode::Over;
        else if (command == "stepIn") mode = debug::StepMode::In;
        else if (command == "stepOut") mode = debug::StepMode::Out;
        const json args = req.value("arguments", json::object());
        const int threadId = args.value("threadId", 0);
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& [id, st] : stopped_) {
                // No thread named: continue all (a client that knows one thread).
                if (threadId != 0 && id != threadId) continue;
                st->resumeMode = mode;
                st->resumeReady = true;
            }
        }
        cv_.notify_all();
        sendResponse(req, command == "continue" ? json{{"allThreadsContinued", threadId == 0}}
                                                : json::object());
    }

    // --- inspection ---------------------------------------------------------
    // The stopped thread a frame id belongs to, or nullptr.
    std::shared_ptr<StoppedThread> ownerOf(int frameId, debug::FrameRecord** rec) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = frameOwner_.find(frameId);
        if (it == frameOwner_.end()) return nullptr;
        auto st = stopped_.find(it->second);
        if (st == stopped_.end()) return nullptr;
        for (const FrameView& f : st->second->frames)
            if (f.id == frameId) {
                if (rec) *rec = f.rec;
                return st->second;
            }
        return nullptr;
    }

    // Runs `fn` on the stopped thread `st` and waits for it; false when the
    // thread resumed first (or there is no session).
    bool onStoppedThread(const std::shared_ptr<StoppedThread>& st, std::function<void()> fn) {
        auto work = std::make_shared<Work>();
        work->fn = std::move(fn);
        std::unique_lock<std::mutex> lk(mu_);
        if (st->gone || !session_) return false;
        st->work.push_back(work);
        cv_.notify_all();
        cv_.wait(lk, [&] { return work->done || st->gone; });
        return work->done;
    }

    void handleStackTrace(const json& req) {
        const json args = req.value("arguments", json::object());
        const int threadId = args.value("threadId", 0);
        const int start = args.value("startFrame", 0);
        const int levels = args.value("levels", 0);
        json frames = json::array();
        int total = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = stopped_.find(threadId);
            if (it != stopped_.end()) {
                const std::vector<FrameView>& fs = it->second->frames;
                total = static_cast<int>(fs.size());
                for (int i = start; i < total; ++i) {
                    if (levels > 0 && static_cast<int>(frames.size()) >= levels) break;
                    const FrameView& f = fs[static_cast<std::size_t>(i)];
                    frames.push_back({{"id", f.id},
                                      {"name", f.name},
                                      {"line", f.line},
                                      {"column", 1},
                                      {"source", {{"name", baseName(f.path)}, {"path", f.path}}}});
                }
            }
        }
        sendResponse(req, {{"stackFrames", frames}, {"totalFrames", total}});
    }

    void handleExceptionInfo(const json& req) {
        const int threadId = req.value("arguments", json::object()).value("threadId", 0);
        std::optional<debug::ExceptionStop> ex;
        std::string trace;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = stopped_.find(threadId);
            if (it != stopped_.end() && it->second->exception) {
                ex = it->second->exception;
                for (const FrameView& f : it->second->frames)
                    trace += "  at " + f.name + " (" + f.path + ":" + std::to_string(f.line) + ")\n";
            }
        }
        if (!ex) {
            sendError(req, "the thread is not stopped on an exception");
            return;
        }
        sendResponse(req, {{"exceptionId", ex->typeName},
                           {"description", ex->message.empty() ? ex->description : ex->message},
                           {"breakMode", ex->breakMode},
                           {"details",
                            {{"typeName", ex->typeName},
                             {"message", ex->message},
                             {"stackTrace", trace}}}});
    }

    // variablesReference: 2 * frameId + 0 the frame's locals, + 1 the globals.
    void handleScopes(const json& req) {
        const int frameId = req.value("arguments", json::object()).value("frameId", 0);
        json scopes = json::array();
        if (ownerOf(frameId, nullptr)) {
            scopes.push_back({{"name", "Locals"}, {"presentationHint", "locals"},
                              {"variablesReference", 2 * frameId}, {"expensive", false}});
            scopes.push_back({{"name", "Globals"}, {"variablesReference", 2 * frameId + 1},
                              {"expensive", false}});
        }
        sendResponse(req, {{"scopes", scopes}});
    }

    // The named, assigned slots of `rec`, Cells opened. On the stopped thread.
    struct Local {
        std::string name;
        const proto::ProtoObject* value;
    };
    std::vector<Local> localsOf(const debug::FrameRecord& rec, proto::ProtoContext* ctx) {
        const BytecodeModule& mod = *rec.mod;
        const RuntimeLayout& L = session_->layout();
        const int n = mod.arity() + mod.localCount();
        std::vector<Local> out;
        std::map<std::string, std::size_t> seen;
        for (int i = 0; i < n; ++i) {
            std::string name = mod.slotName(i);
            if (name.empty() && static_cast<std::size_t>(i) < mod.paramNames().size())
                name = mod.paramNames()[static_cast<std::size_t>(i)];
            if (name.empty() || name.find('$') != std::string::npos || name.rfind("__", 0) == 0)
                continue;
            const proto::ProtoObject* v = rec.slots[i];
            if (!v) continue;  // not assigned yet
            if (mod.slotBoxed(i) && v != PROTO_NONE && v->getPrototype(ctx) == L.cellProto) {
                v = v->getOwnAttributeDirect(ctx, L.valueKey);
                if (!v) continue;
            }
            // A name declared again in an inner scope: the later slot wins.
            auto it = seen.find(name);
            if (it != seen.end()) {
                out[it->second].value = v;
            } else {
                seen[name] = out.size();
                out.push_back({name, v});
            }
        }
        return out;
    }

    // Evaluates `expr` over `rec`'s locals and the globals, with `boundName`
    // bound to `bound` (it shadows a local of that name). On the thread that
    // owns `rec`, under the caller's Suppress.
    bool evaluateIn(proto::ProtoContext* scope, const debug::FrameRecord& rec,
                    const std::string& expr, const char* boundName,
                    const proto::ProtoObject* bound, std::string* result) {
        const std::vector<Local> locals = localsOf(rec, scope);
        if (boundName && expr == boundName) {
            *result = session_->debugShow(scope, bound);
            return true;
        }
        for (const Local& l : locals)
            if (l.name == expr && !(boundName && l.name == boundName)) {
                *result = session_->debugShow(scope, l.value);
                return true;
            }
        std::vector<std::string> names;
        std::vector<const proto::ProtoObject*> values;
        for (const Local& l : locals) {
            if (l.name == "this" || !isIdentifier(l.name)) continue;
            if (boundName && l.name == boundName) continue;
            names.push_back(l.name);
            values.push_back(l.value);
        }
        if (boundName) {
            names.push_back(boundName);
            values.push_back(bound ? bound : PROTO_NONE);
        }
        return session_->debugEvaluate(scope, expr, names, values.data(), result);
    }

    void handleVariables(const json& req) {
        const int ref = req.value("arguments", json::object()).value("variablesReference", 0);
        const int frameId = ref / 2;
        const bool globals = (ref % 2) == 1;
        debug::FrameRecord* rec = nullptr;
        std::shared_ptr<StoppedThread> st = ownerOf(frameId, &rec);
        json vars = json::array();
        if (st) {
            onStoppedThread(st, [&] {
                proto::ProtoContext scope(st->current->space, st->current);
                if (globals) {
                    for (const Session::DebugGlobal& g : session_->debugGlobals(&scope))
                        vars.push_back({{"name", g.name},
                                        {"value", clip(session_->debugShow(&scope, g.value))},
                                        {"variablesReference", 0}});
                } else {
                    for (const Local& l : localsOf(*rec, &scope))
                        vars.push_back({{"name", l.name},
                                        {"value", clip(session_->debugShow(&scope, l.value))},
                                        {"variablesReference", 0}});
                }
            });
        }
        sendResponse(req, {{"variables", vars}});
    }

    void handleEvaluate(const json& req) {
        const json args = req.value("arguments", json::object());
        const std::string expr = args.value("expression", "");
        int frameId = args.value("frameId", 0);
        if (frameId == 0) {
            // No frame selected: the innermost frame of the thread stopped last.
            std::lock_guard<std::mutex> lk(mu_);
            auto it = stopped_.find(lastStopped_);
            if (it == stopped_.end()) it = stopped_.begin();
            if (it != stopped_.end() && !it->second->frames.empty())
                frameId = it->second->frames.front().id;
        }
        debug::FrameRecord* rec = nullptr;
        std::shared_ptr<StoppedThread> st = ownerOf(frameId, &rec);
        if (!st) {
            sendError(req, "expressions are evaluated while the program is stopped");
            return;
        }
        bool ok = false;
        std::string result;
        const bool ran = onStoppedThread(st, [&] {
            proto::ProtoContext scope(st->current->space, st->current);
            ok = evaluateIn(&scope, *rec, expr, nullptr, nullptr, &result);
        });
        if (!ran) {
            sendError(req, "the program resumed before the expression could be evaluated");
            return;
        }
        if (!ok) {
            sendError(req, clip(result));
            return;
        }
        sendResponse(req, {{"result", clip(result)}, {"variablesReference", 0}});
    }

    // --- shutting down ------------------------------------------------------
    int shutdown() {
        debug::detach();  // nothing stops from here on
        {
            std::lock_guard<std::mutex> lk(mu_);
            shuttingDown_ = true;
            configDone_ = true;
            for (auto& [id, st] : stopped_) {
                st->resumeMode = debug::StepMode::None;
                st->resumeReady = true;
            }
        }
        cv_.notify_all();
        if (!launcher_.joinable()) return 0;
        // The client asked to stop: a script that does not end promptly by
        // itself is ended with the process, which is what "stop" means.
        bool finished;
        {
            std::unique_lock<std::mutex> lk(mu_);
            finished = cv_.wait_for(lk, std::chrono::milliseconds(500), [&] { return finished_; });
        }
        if (!finished) {
            stdio_.protocolOut().flush();
            std::_Exit(0);
        }
        launcher_.join();
        return 0;
    }

    // --- sending ------------------------------------------------------------
    void sendResponse(const json& req, const json& body) {
        json r = {{"seq", seq_++}, {"type", "response"}, {"request_seq", req.value("seq", 0)},
                  {"success", true}, {"command", req.value("command", "")}};
        if (!body.is_null()) r["body"] = body;
        transport_.writeMessage(r);
    }
    void sendError(const json& req, const std::string& message) {
        transport_.writeMessage({{"seq", seq_++}, {"type", "response"},
                                 {"request_seq", req.value("seq", 0)}, {"success", false},
                                 {"command", req.value("command", "")}, {"message", message},
                                 {"body", {{"error", {{"id", 1}, {"format", message}}}}}});
    }
    void sendEvent(const std::string& event, const json& body) {
        json e = {{"seq", seq_++}, {"type", "event"}, {"event", event}};
        if (!body.is_null()) e["body"] = body;
        transport_.writeMessage(e);
    }
    void sendOutput(const std::string& category, const std::string& text) {
        sendEvent("output", {{"category", category}, {"output", text}});
    }

    DapStdio stdio_;  // before transport_: it owns the streams
    DapTransport transport_;
    std::atomic<int> seq_{1};

    std::string program_;
    std::vector<std::string> args_;
    bool noDebug_ = false;
    std::thread launcher_;

    // Shared between the protocol thread and the debuggee's threads.
    std::mutex mu_;
    std::condition_variable cv_;
    bool configDone_ = false;
    bool shuttingDown_ = false;
    bool finished_ = false;
    Session* session_ = nullptr;
    std::map<int, std::shared_ptr<StoppedThread>> stopped_;  // by thread id
    std::map<int, int> frameOwner_;                          // frame id -> thread id
    int nextFrameId_ = 1;
    int lastStopped_ = 0;
};

}  // namespace

int runDapServer() {
    DapServer server;
    return server.run();
}

}  // namespace protoScala
