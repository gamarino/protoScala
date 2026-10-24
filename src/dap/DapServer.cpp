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
 *  - A thread that reaches a breakpoint or a step target calls stopped() from
 *    inside the interpreter. It parks in an unmanaged region on its own
 *    context, so a collection never waits for it, and serves the requests that
 *    need the language -- rendering locals, evaluating an expression -- itself,
 *    on its own context, in managed mode. The protocol thread hands those to it
 *    and waits. Values are therefore read only by the thread whose frames root
 *    them, and only while those frames are stopped.
 *  - DapStdio forwards the script's stdout and stderr as `output` events.
 *
 * One stop at a time: a second thread reaching a breakpoint (an actor) waits,
 * unmanaged, until the first resumes. The client sees one thread.
 */
#include "dap/DapServer.h"
#include "dap/DapStdio.h"
#include "dap/DapTransport.h"

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

std::string baseName(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// A frame of the stopped thread, as the client is shown it.
struct FrameView {
    debug::FrameRecord* rec;
    std::string name;
    std::string path;
    int line;
};

// A request the stopped thread runs for the protocol thread.
struct Work {
    std::function<void()> fn;
    bool done = false;
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
    void stopped(debug::FrameRecord& top, const char* reason) override {
        proto::ProtoContext* ctx = top.ctx;
        // A line printed without a newline is still in stdio's buffer.
        std::fflush(stdout);
        std::fflush(stderr);
        std::vector<FrameView> frames;
        for (debug::FrameRecord* r = &top; r; r = r->parent) {
            if (!r->src) continue;  // the prelude: no source to show
            frames.push_back({r, r->mod->name(), r->src->path, r->line});
        }
        std::unique_lock<std::mutex> stopLk(stopMu_, std::defer_lock);
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            stopLk.lock();
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (shuttingDown_) return;
            stopped_ = true;
            resumeReady_ = false;
            frames_ = std::move(frames);
            stoppedTop_ = &top;
        }
        sendEvent("stopped", {{"reason", reason}, {"threadId", 1}, {"allThreadsStopped", true}});

        debug::StepMode mode = debug::StepMode::None;
        for (;;) {
            std::shared_ptr<Work> work;
            {
                proto::ProtoContext::UnmanagedScope unmanaged(ctx);
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return resumeReady_ || !work_.empty(); });
                if (!work_.empty()) {
                    work = work_.front();
                    work_.pop_front();
                } else {
                    mode = resumeMode_;
                    resumeReady_ = false;
                    stopped_ = false;
                    stoppedTop_ = nullptr;
                    frames_.clear();
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

private:
    // Answers true when the loop should end.
    bool handle(const json& req) {
        const std::string command = req.value("command", "");
        if (command == "initialize") {
            sendResponse(req, {{"supportsConfigurationDoneRequest", true},
                               {"supportsEvaluateForHovers", true}});
            sendEvent("initialized", json::object());
        } else if (command == "launch") {
            handleLaunch(req);
        } else if (command == "setBreakpoints") {
            handleSetBreakpoints(req);
        } else if (command == "setExceptionBreakpoints") {
            sendResponse(req, {{"breakpoints", json::array()}});
        } else if (command == "configurationDone") {
            sendResponse(req, json::object());
            {
                std::lock_guard<std::mutex> lk(mu_);
                configDone_ = true;
            }
            cv_.notify_all();
        } else if (command == "threads") {
            sendResponse(req, {{"threads", json::array({{{"id", 1}, {"name", "main"}}})}});
        } else if (command == "continue" || command == "next" || command == "stepIn" ||
                   command == "stepOut") {
            handleResume(req, command);
        } else if (command == "pause") {
            debug::requestStop("pause");
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
        std::vector<int> lines;
        if (args.contains("breakpoints") && args["breakpoints"].is_array())
            for (const json& b : args["breakpoints"]) lines.push_back(b.value("line", 0));
        else if (args.contains("lines") && args["lines"].is_array())
            for (const json& l : args["lines"]) lines.push_back(l.get<int>());
        json out = json::array();
        if (!path.empty()) {
            for (const Breakpoint& b : debug::setBreakpoints(path, lines)) {
                json e = {{"id", b.id}, {"verified", b.verified}, {"line", b.line}};
                if (!b.verified) e["message"] = "no code on or after this line (yet)";
                out.push_back(std::move(e));
            }
        }
        sendResponse(req, {{"breakpoints", out}});
    }

    // --- resuming -----------------------------------------------------------
    void handleResume(const json& req, const std::string& command) {
        debug::StepMode mode = debug::StepMode::None;
        if (command == "next") mode = debug::StepMode::Over;
        else if (command == "stepIn") mode = debug::StepMode::In;
        else if (command == "stepOut") mode = debug::StepMode::Out;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stopped_) {
                resumeMode_ = mode;
                resumeReady_ = true;
            }
        }
        cv_.notify_all();
        sendResponse(req, command == "continue" ? json{{"allThreadsContinued", true}}
                                                : json::object());
    }

    // --- inspection ---------------------------------------------------------
    // Runs `fn` on the stopped thread and waits for it; false when no thread is
    // stopped (or it resumed first).
    bool onStoppedThread(std::function<void()> fn) {
        auto work = std::make_shared<Work>();
        work->fn = std::move(fn);
        std::unique_lock<std::mutex> lk(mu_);
        if (!stopped_ || !session_) return false;
        work_.push_back(work);
        cv_.notify_all();
        cv_.wait(lk, [&] { return work->done || !stopped_; });
        return work->done;
    }

    void handleStackTrace(const json& req) {
        const json args = req.value("arguments", json::object());
        const int start = args.value("startFrame", 0);
        const int levels = args.value("levels", 0);
        json frames = json::array();
        int total = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            total = static_cast<int>(frames_.size());
            for (int i = start; i < total; ++i) {
                if (levels > 0 && static_cast<int>(frames.size()) >= levels) break;
                const FrameView& f = frames_[static_cast<std::size_t>(i)];
                frames.push_back({{"id", i + 1},
                                  {"name", f.name},
                                  {"line", f.line},
                                  {"column", 1},
                                  {"source", {{"name", baseName(f.path)}, {"path", f.path}}}});
            }
        }
        sendResponse(req, {{"stackFrames", frames}, {"totalFrames", total}});
    }

    // variablesReference: 2 * frameId + 0 the frame's locals, + 1 the globals.
    void handleScopes(const json& req) {
        const int frameId = req.value("arguments", json::object()).value("frameId", 0);
        json scopes = json::array();
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (frameId >= 1 && frameId <= static_cast<int>(frames_.size())) {
                scopes.push_back({{"name", "Locals"}, {"presentationHint", "locals"},
                                  {"variablesReference", 2 * frameId}, {"expensive", false}});
                scopes.push_back({{"name", "Globals"}, {"variablesReference", 2 * frameId + 1},
                                  {"expensive", false}});
            }
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

    debug::FrameRecord* frameRecord(int frameId) {
        std::lock_guard<std::mutex> lk(mu_);
        if (frameId < 1 || frameId > static_cast<int>(frames_.size())) return nullptr;
        return frames_[static_cast<std::size_t>(frameId - 1)].rec;
    }

    void handleVariables(const json& req) {
        const int ref = req.value("arguments", json::object()).value("variablesReference", 0);
        const int frameId = ref / 2;
        const bool globals = (ref % 2) == 1;
        debug::FrameRecord* rec = frameRecord(frameId);
        json vars = json::array();
        if (rec) {
            onStoppedThread([&] {
                proto::ProtoContext scope(stoppedTop_->ctx->space, stoppedTop_->ctx);
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
        if (frameId == 0) frameId = 1;
        debug::FrameRecord* rec = frameRecord(frameId);
        if (!rec) {
            sendError(req, "expressions are evaluated while the program is stopped");
            return;
        }
        bool ok = false;
        std::string result;
        const bool ran = onStoppedThread([&] {
            proto::ProtoContext scope(stoppedTop_->ctx->space, stoppedTop_->ctx);
            const std::vector<Local> locals = localsOf(*rec, &scope);
            for (const Local& l : locals)
                if (l.name == expr) {
                    result = session_->debugShow(&scope, l.value);
                    ok = true;
                    return;
                }
            std::vector<std::string> names;
            std::vector<const proto::ProtoObject*> values;
            for (const Local& l : locals) {
                if (l.name == "this" || !isIdentifier(l.name)) continue;
                names.push_back(l.name);
                values.push_back(l.value);
            }
            ok = session_->debugEvaluate(&scope, expr, names, values.data(), &result);
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
            resumeMode_ = debug::StepMode::None;
            resumeReady_ = true;
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
    std::mutex stopMu_;  // one stop at a time
    bool configDone_ = false;
    bool shuttingDown_ = false;
    bool finished_ = false;
    bool stopped_ = false;
    bool resumeReady_ = false;
    debug::StepMode resumeMode_ = debug::StepMode::None;
    Session* session_ = nullptr;
    debug::FrameRecord* stoppedTop_ = nullptr;
    std::vector<FrameView> frames_;
    std::deque<std::shared_ptr<Work>> work_;
};

}  // namespace

int runDapServer() {
    DapServer server;
    return server.run();
}

}  // namespace protoScala
