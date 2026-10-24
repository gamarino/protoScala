#include "runtime/Debugger.h"

#include "protoCore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>

namespace protoScala::debug {

std::atomic<bool> gAttached{false};
std::atomic<unsigned> gExceptionBreaks{0};

namespace {

std::atomic<Controller*> gController{nullptr};

// A stop the first thread to reach a new line takes ("entry"), or nullptr.
std::atomic<const char*> gStopRequest{nullptr};
std::atomic<bool> gJustMyCode{false};

// A step may stop in `s`.
bool stepsInto(const Source* s) {
    return s && !(s->library && gJustMyCode.load(std::memory_order_relaxed));
}

// One OS thread that ran interpreted code while attached.
struct ThreadInfo {
    int id = 0;
    std::string name;
    std::atomic<const char*> pause{nullptr};  // a stop requested for this thread
    std::string activity;                     // guarded by Threads::mu
};

struct Threads {
    std::mutex mu;
    std::map<int, std::shared_ptr<ThreadInfo>> live;
    int nextId = 1;
};

Threads& threadTable() {
    static Threads t;
    return t;
}

// The calling thread's debugging state. Its destructor runs when the OS thread
// ends and reports the thread's exit.
struct ThreadSlot {
    std::shared_ptr<ThreadInfo> info;
    std::string pendingName;   // nameThread before registration
    StepMode stepMode = StepMode::None;
    unsigned stepDepth = 0;
    // The exception reported last (alreadyReported). A ProtoException copy
    // shares the original's pin, so the value stays alive while remembered.
    std::unique_ptr<proto::ProtoException> reported;
    const void* reportedError = nullptr;
    // The innermost NativeBoundary on this thread.
    bool boundaryOn = false;
    Boundary boundaryKind = Boundary::None;
    const FrameRecord* boundaryParent = nullptr;

    ~ThreadSlot() {
        // The space may be gone by now (the evaluator thread outlives its
        // Session): never release a pin here. leaveFrame released it already
        // unless the thread ended inside interpreted code.
        (void)reported.release();
        if (!info) return;
        Threads& t = threadTable();
        std::lock_guard<std::mutex> lk(t.mu);
        t.live.erase(info->id);
        if (Controller* c = gController.load(std::memory_order_acquire)) c->threadExited(info->id);
    }
};

thread_local FrameRecord* tTop = nullptr;
thread_local bool tSuppressed = false;
thread_local ThreadSlot tSlot;

void registerThread() {
    auto info = std::make_shared<ThreadInfo>();
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    info->id = t.nextId++;
    info->name = !tSlot.pendingName.empty() ? tSlot.pendingName
                 : info->id == 1             ? std::string("main")
                                             : "thread " + std::to_string(info->id);
    t.live[info->id] = info;
    tSlot.info = info;
    if (Controller* c = gController.load(std::memory_order_acquire)) c->threadStarted(info->id);
}

}  // namespace

// What a line breakpoint does beyond stopping.
struct LineBreakpoint {
    enum class Hit { None, Eq, Ge, Gt, Lt, Le, Mod };
    std::string condition;
    Hit hit = Hit::None;
    long long hitN = 0;
    bool isLog = false;
    std::string logMessage;
    std::atomic<long long> hits{0};  // hits whose condition held
    bool plain() const { return condition.empty() && hit == Hit::None && !isLog; }
};

namespace {

struct Requested {
    int id;
    int line;
    std::shared_ptr<LineBreakpoint> bp;  // null: an unconditional breakpoint
    std::string invalid;                 // why it cannot be armed ("" when it can)
};

// `3`, `== 3`, `>= 2`, `> 2`, `< 4`, `<= 4`, `% 2`; false when malformed.
bool parseHitCondition(const std::string& text, LineBreakpoint& bp) {
    std::size_t i = 0;
    auto skip = [&] {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    skip();
    LineBreakpoint::Hit op = LineBreakpoint::Hit::Eq;
    auto take = [&](const char* tok, LineBreakpoint::Hit h) {
        const std::size_t n = std::char_traits<char>::length(tok);
        if (text.compare(i, n, tok) != 0) return false;
        i += n;
        op = h;
        return true;
    };
    take("==", LineBreakpoint::Hit::Eq) || take(">=", LineBreakpoint::Hit::Ge) ||
        take("<=", LineBreakpoint::Hit::Le) || take(">", LineBreakpoint::Hit::Gt) ||
        take("<", LineBreakpoint::Hit::Lt) || take("%", LineBreakpoint::Hit::Mod) ||
        take("=", LineBreakpoint::Hit::Eq);
    skip();
    const std::size_t start = i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    if (i == start || i - start > 15) return false;
    const long long n = std::stoll(text.substr(start, i - start));
    skip();
    if (i != text.size()) return false;
    if (op == LineBreakpoint::Hit::Mod && n <= 0) return false;
    bp.hit = op;
    bp.hitN = n;
    return true;
}

bool hitPasses(const LineBreakpoint& bp, long long n) {
    switch (bp.hit) {
        case LineBreakpoint::Hit::None: return true;
        case LineBreakpoint::Hit::Eq: return n == bp.hitN;
        case LineBreakpoint::Hit::Ge: return n >= bp.hitN;
        case LineBreakpoint::Hit::Gt: return n > bp.hitN;
        case LineBreakpoint::Hit::Lt: return n < bp.hitN;
        case LineBreakpoint::Hit::Le: return n <= bp.hitN;
        case LineBreakpoint::Hit::Mod: return n % bp.hitN == 0;
    }
    return true;
}

struct Registry {
    std::mutex mu;
    std::map<std::string, std::unique_ptr<Source>> sources;      // by key
    std::vector<const Source*> order;                            // registration order
    std::map<int, const Source*> byRef;
    int nextRef = 1;
    std::map<std::string, std::vector<Requested>> requested;     // by key
    int nextId = 1;
};

Registry& registry() {
    static Registry r;
    return r;
}

// The line a breakpoint requested on `line` is armed on: the first line at or
// after it that has code, as other debuggers do; 0 when there is none.
int resolveLine(const Source& s, int line) {
    if (line <= 0) return 0;
    for (std::size_t l = static_cast<std::size_t>(line); l < s.lineCount; ++l)
        if (s.hasCode[l]) return static_cast<int>(l);
    return 0;
}

// Arms `s` from the requests for its file. Called with the registry locked.
std::vector<Controller::Breakpoint> arm(Source& s, const std::vector<Requested>& req) {
    for (std::size_t l = 0; l < s.lineCount; ++l) s.armed[l].store(0, std::memory_order_relaxed);
    s.special.clear();
    std::vector<Controller::Breakpoint> out;
    for (const Requested& r : req) {
        if (!r.invalid.empty()) {
            out.push_back({r.id, r.line, false, r.invalid});
            continue;
        }
        const int at = resolveLine(s, r.line);
        if (at > 0) {
            auto& flag = s.armed[static_cast<std::size_t>(at)];
            // Two breakpoints resolved to one line: the first one set wins.
            if (flag.load(std::memory_order_relaxed) == 0 && r.bp) s.special[at] = r.bp;
            flag.store(1, std::memory_order_relaxed);
        }
        out.push_back({r.id, at > 0 ? at : r.line, at > 0,
                       at > 0 ? std::string() : "no code on or after this line (yet)"});
    }
    return out;
}

// The breakpoint armed on `line` of `s`, when it is not a plain one.
std::shared_ptr<LineBreakpoint> specialAt(const Source& s, int line) {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    auto it = s.special.find(line);
    return it == s.special.end() ? nullptr : it->second;
}

// `{expr}` parts evaluated in `r`, `{{` and `}}` literal braces.
std::string interpolate(Controller* c, proto::ProtoContext* current, FrameRecord& r,
                        const std::string& msg) {
    std::string out;
    for (std::size_t i = 0; i < msg.size(); ++i) {
        const char ch = msg[i];
        if ((ch == '{' || ch == '}') && i + 1 < msg.size() && msg[i + 1] == ch) {
            out += ch;
            ++i;
            continue;
        }
        if (ch != '{') {
            out += ch;
            continue;
        }
        // The matching `}`, counting nested braces of the expression.
        int depth = 1;
        std::size_t j = i + 1;
        for (; j < msg.size() && depth > 0; ++j) {
            if (msg[j] == '{') ++depth;
            else if (msg[j] == '}') --depth;
        }
        if (depth != 0) {  // no closing brace: the rest is text
            out += msg.substr(i);
            break;
        }
        const std::string expr = msg.substr(i + 1, j - i - 2);
        const Controller::Evaluation e = c->evaluateHere(current, r, expr, nullptr, nullptr, true);
        out += e.ok ? e.text : "<error: " + e.text + ">";
        i = j - 1;
    }
    return out;
}

// Exception filter conditions, by filter bit (guarded by the mutex).
std::mutex gExceptionConditionsMu;
std::string gExceptionConditions[2];

// A function's epilogue: its RETURN, or the `()` a Unit body returns. The
// compiler gives it the line of the definition (the top level's, line 1), so
// stopping there would jump back to the `def` line after the body's last line.
bool isEpilogue(const BytecodeModule& m, std::size_t pc) {
    const auto& code = m.code();
    const auto op = [&](std::size_t i) { return static_cast<Op>(code[i] & 0xFF); };
    if (op(pc) == Op::RETURN) return true;
    return op(pc) == Op::PUSH_UNIT && pc + 1 < code.size() && op(pc + 1) == Op::RETURN &&
           m.lineAt(pc + 1) == m.lineAt(pc);
}

void markLines(const BytecodeModule& m, std::vector<std::uint8_t>& lines) {
    for (std::size_t pc = 0; pc < m.code().size(); ++pc) {
        const int l = m.lineAt(pc);
        if (l <= 0 || isEpilogue(m, pc)) continue;
        if (lines.size() <= static_cast<std::size_t>(l)) lines.resize(static_cast<std::size_t>(l) + 1, 0);
        lines[static_cast<std::size_t>(l)] = 1;
    }
    for (std::size_t i = 0; i < m.blockCount(); ++i) markLines(m.block(i), lines);
}

void tag(const BytecodeModule& m, const Source* s) {
    m.setDebugSource(s);
    for (std::size_t i = 0; i < m.blockCount(); ++i) tag(m.block(i), s);
}

}  // namespace

std::string normalisePath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    std::string out = (ec ? p : abs).lexically_normal().string();
#if defined(_WIN32)
    std::replace(out.begin(), out.end(), '\\', '/');
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return out;
}

void attach(Controller* c) {
    gController.store(c, std::memory_order_release);
    gAttached.store(c != nullptr, std::memory_order_release);
}

void detach() {
    gAttached.store(false, std::memory_order_release);
    gExceptionBreaks.store(0);
    gStopRequest.store(nullptr);
    // Taken so that no thread is inside a threadStarted/threadExited call on
    // the controller once this returns.
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    gController.store(nullptr, std::memory_order_release);
    for (auto& [id, info] : t.live) info->pause.store(nullptr);
}

void enterFrame(FrameRecord& r, const BytecodeModule& mod, proto::ProtoContext* ctx,
                const proto::ProtoObject** slots) {
    if (!tSlot.info) registerThread();
    r.parent = tTop;
    r.depth = tTop ? tTop->depth + 1 : 1;
    r.mod = &mod;
    r.src = static_cast<const Source*>(mod.debugSource());
    r.ctx = ctx;
    r.slots = slots;
    r.pc = 0;
    r.line = -1;
    r.boundary = tSlot.boundaryOn && tSlot.boundaryParent == tTop ? tSlot.boundaryKind
                                                                   : Boundary::None;
    tTop = &r;
}

void leaveFrame(FrameRecord& r) {
    tTop = r.parent;
    if (!tTop) {
        // The thread left interpreted code: whatever it reported is settled.
        tSlot.reported.reset();
        tSlot.reportedError = nullptr;
    }
    // The frame a step over or a step out started in returns: the step ends in
    // its caller, at the call site, rather than at the caller's next line --
    // which for `n * fact(n - 1)` would be after the multiplication and the
    // caller's own return. A prelude frame in between (List.map running a
    // lambda) has no line to show, so the mark goes to the nearest one that has.
    const StepMode mode = tSlot.stepMode;
    if ((mode == StepMode::Over || mode == StepMode::Out) && r.depth <= tSlot.stepDepth) {
        for (FrameRecord* p = r.parent; p; p = p->parent)
            if (stepsInto(p->src)) {
                p->line = -1;
                break;
            }
    }
}

void newLine(FrameRecord& r) {
    if (!r.src || r.line <= 0 || tSuppressed || isEpilogue(*r.mod, r.pc)) return;
    Controller* c = gController.load(std::memory_order_acquire);
    if (!c) return;
    const char* reason = nullptr;
    ThreadInfo* self = tSlot.info.get();
    if (self && self->pause.load(std::memory_order_relaxed)) reason = self->pause.exchange(nullptr);
    // "entry" (and a pause for no thread in particular) is the program's first
    // line, never the prelude's initialisation.
    if (!reason && !r.src->library && gStopRequest.load(std::memory_order_relaxed))
        reason = gStopRequest.exchange(nullptr);
    if (!reason) {
        const StepMode mode = tSlot.stepMode;
        if (mode != StepMode::None && stepsInto(r.src)) {
            const unsigned depth = tSlot.stepDepth;
            if (mode == StepMode::In || (mode == StepMode::Over && r.depth <= depth) ||
                (mode == StepMode::Out && r.depth < depth))
                reason = "step";
        }
    }
    std::string note;
    if (!reason && r.src->armedAt(r.line)) {
        std::shared_ptr<LineBreakpoint> bp = specialAt(*r.src, r.line);
        if (!bp) {
            reason = "breakpoint";
        } else {
            // The condition, the hit count and the log message run on this
            // thread, in this frame, with every stop suppressed.
            Suppress quiet;
            bool stop = true;
            if (!bp->condition.empty()) {
                const Controller::Evaluation e =
                    c->evaluateHere(r.ctx, r, bp->condition, nullptr, nullptr, false);
                if (!e.ok) {
                    note = "breakpoint condition `" + bp->condition + "` failed: " + e.text;
                } else if (e.text == "false") {
                    stop = false;
                } else if (e.text != "true") {
                    note = "breakpoint condition `" + bp->condition + "` is not a Boolean: " + e.text;
                }
            }
            if (stop && note.empty()) {
                const long long n = bp->hits.fetch_add(1) + 1;
                stop = hitPasses(*bp, n);
                if (stop && bp->isLog) {
                    c->output("console", interpolate(c, r.ctx, r, bp->logMessage) + "\n");
                    stop = false;
                }
            }
            if (!note.empty()) c->output("stderr", note + "\n");
            if (stop) reason = "breakpoint";
        }
    }
    if (!reason) return;
    tSlot.stepMode = StepMode::None;
    c->stopped(r, r.ctx, reason, self ? self->id : 0, nullptr, note.empty() ? nullptr : &note);
}

void setStep(StepMode mode, const FrameRecord& top) {
    tSlot.stepDepth = top.depth;
    tSlot.stepMode = mode;
}

void requestStop(const char* reason) { gStopRequest.store(reason, std::memory_order_release); }

bool requestPause(int threadId) {
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    auto it = t.live.find(threadId);
    if (it == t.live.end()) return false;
    it->second->pause.store("pause", std::memory_order_release);
    return true;
}

FrameRecord* currentFrame() { return tTop; }

Suppress::Suppress() : saved_(tSuppressed) { tSuppressed = true; }
Suppress::~Suppress() { tSuppressed = saved_; }

// --- Exceptions --------------------------------------------------------------

void setExceptionBreaks(unsigned mask, const std::string& raisedCondition,
                        const std::string& uncaughtCondition) {
    {
        std::lock_guard<std::mutex> lk(gExceptionConditionsMu);
        gExceptionConditions[0] = raisedCondition;
        gExceptionConditions[1] = uncaughtCondition;
    }
    gExceptionBreaks.store(mask, std::memory_order_release);
}

bool suppressed() { return tSuppressed; }

bool alreadyReported(const proto::ProtoObject* value, const void* error) {
    if (error && error == tSlot.reportedError) return true;
    return value && tSlot.reported && tSlot.reported->value() == value;
}

void markReported(proto::ProtoContext* ctx, const proto::ProtoObject* value, const void* error) {
    tSlot.reportedError = error;
    tSlot.reported.reset();
    if (value) tSlot.reported = std::make_unique<proto::ProtoException>(ctx, value);
}

void noteMaterialised(proto::ProtoContext* ctx, const void* error, const proto::ProtoObject* value) {
    if (!error || error != tSlot.reportedError) return;
    markReported(ctx, value, nullptr);
}

void stopForException(FrameRecord& top, proto::ProtoContext* current, const ExceptionStop& ex,
                      const std::string* note) {
    Controller* c = gController.load(std::memory_order_acquire);
    if (!c || tSuppressed) return;
    tSlot.stepMode = StepMode::None;  // the stop ends a step in progress
    c->stopped(top, current, "exception", tSlot.info ? tSlot.info->id : 0, &ex, note);
}

bool exceptionConditionHolds(unsigned filters, proto::ProtoContext* current, FrameRecord& top,
                             const proto::ProtoObject* value, std::string* note) {
    Controller* c = gController.load(std::memory_order_acquire);
    if (!c) return false;
    std::string conditions[2];
    {
        std::lock_guard<std::mutex> lk(gExceptionConditionsMu);
        conditions[0] = gExceptionConditions[0];
        conditions[1] = gExceptionConditions[1];
    }
    for (int bit = 0; bit < 2; ++bit) {
        if (!(filters & (1u << bit))) continue;
        const std::string& cond = conditions[bit];
        if (cond.empty()) return true;
        Controller::Evaluation e;
        {
            Suppress quiet;
            e = c->evaluateHere(current, top, cond, "e", value, false);
        }
        if (e.text == "true" && e.ok) return true;
        if (!e.ok || e.text != "false") {
            *note = "exception breakpoint condition `" + cond + "` " +
                    (e.ok ? "is not a Boolean: " : "failed: ") + e.text;
            c->output("stderr", *note + "\n");
            return true;
        }
    }
    return false;
}

NativeBoundary::NativeBoundary(Boundary kind) {
    if (!attached()) return;
    active_ = true;
    savedOn_ = tSlot.boundaryOn;
    savedKind_ = tSlot.boundaryKind;
    savedParent_ = tSlot.boundaryParent;
    tSlot.boundaryOn = true;
    tSlot.boundaryKind = kind;
    tSlot.boundaryParent = tTop;
}

NativeBoundary::~NativeBoundary() {
    if (!active_) return;
    tSlot.boundaryOn = savedOn_;
    tSlot.boundaryKind = savedKind_;
    tSlot.boundaryParent = savedParent_;
}

// --- Threads -------------------------------------------------------------------

void nameThread(const std::string& name) {
    if (!attached()) return;
    if (!tSlot.info) {
        tSlot.pendingName = name;
        return;
    }
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    tSlot.info->name = name;
}

Activity::~Activity() {
    if (!set_ || !tSlot.info) return;
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    tSlot.info->activity.clear();
}

void Activity::set(const std::string& text) {
    if (!attached()) return;
    if (!tSlot.info) registerThread();
    set_ = true;
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    tSlot.info->activity = text;
}

std::string currentLocation() {
    for (const FrameRecord* r = tTop; r; r = r->parent) {
        if (!r->src || r->src->library) continue;
        const std::string& path = r->src->path;
        const auto slash = path.find_last_of("/\\");
        return (slash == std::string::npos ? path : path.substr(slash + 1)) + ":" +
               std::to_string(r->line);
    }
    return std::string();
}

int currentThreadId() { return tSlot.info ? tSlot.info->id : 0; }

std::vector<ThreadView> threads() {
    std::vector<ThreadView> out;
    Threads& t = threadTable();
    std::lock_guard<std::mutex> lk(t.mu);
    for (const auto& [id, info] : t.live)
        out.push_back({id, info->activity.empty() ? info->name
                                                  : info->name + " (" + info->activity + ")"});
    return out;
}

// --- Sources and breakpoints -----------------------------------------------------

namespace {

void registerSource(const BytecodeModule& mod, const std::string& key, const std::string& path,
                    const std::string& name, const char* content, const std::string& origin) {
    std::vector<std::uint8_t> lines;
    markLines(mod, lines);
    std::vector<Controller::Breakpoint> resolved;
    const Source* fresh = nullptr;
    {
        Registry& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mu);
        auto& slot = reg.sources[key];
        if (!slot) {
            slot = std::make_unique<Source>();
            slot->path = path;
            slot->key = key;
            slot->name = name;
            slot->content = content;
            slot->origin = origin;
            slot->library = content != nullptr;
            slot->ref = reg.nextRef++;
            reg.byRef[slot->ref] = slot.get();
            reg.order.push_back(slot.get());
            fresh = slot.get();
        }
        Source& s = *slot;
        // A file compiled twice (a module re-imported): the union of its lines.
        if (lines.size() < s.hasCode.size()) lines.resize(s.hasCode.size(), 0);
        for (std::size_t l = 0; l < s.hasCode.size(); ++l) lines[l] |= s.hasCode[l];
        if (lines.size() + 1 > s.lineCount) {
            // Grown only while no frame of this file runs: it is compiled, not yet run.
            auto armed = std::make_unique<std::atomic<std::uint8_t>[]>(lines.size() + 1);
            for (std::size_t l = 0; l < lines.size() + 1; ++l) armed[l].store(0);
            s.armed = std::move(armed);
            s.lineCount = lines.size() + 1;
        }
        lines.resize(s.lineCount, 0);
        s.hasCode = std::move(lines);
        tag(mod, &s);
        auto req = reg.requested.find(key);
        if (req != reg.requested.end()) resolved = arm(s, req->second);
    }
    Controller* c = gController.load(std::memory_order_acquire);
    if (c && fresh) c->sourceLoaded(*fresh);
    if (c && !resolved.empty()) c->breakpointsResolved(resolved);
}

}  // namespace

void registerModule(const BytecodeModule& mod, const std::string& path) {
    if (!attached() || path.empty() || path.front() == '<') return;
    // Absolute, with the platform's separators: what a client opens.
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);
    const std::string full = ec ? path : abs.lexically_normal().make_preferred().string();
    const auto slash = full.find_last_of("/\\");
    registerSource(mod, normalisePath(path), full,
                   slash == std::string::npos ? full : full.substr(slash + 1), nullptr, std::string());
}

void registerEmbeddedModule(const BytecodeModule& mod, const std::string& name,
                            const char* content, const std::string& origin) {
    if (!attached()) return;
    registerSource(mod, "embedded:" + name, std::string(), name, content, origin);
}

std::vector<const Source*> loadedSources() {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    return reg.order;
}

const Source* sourceByRef(int ref) {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    auto it = reg.byRef.find(ref);
    return it == reg.byRef.end() ? nullptr : it->second;
}

void setJustMyCode(bool on) { gJustMyCode.store(on, std::memory_order_release); }

std::vector<Controller::Breakpoint> setBreakpoints(const std::string& path,
                                                   const std::vector<BreakpointSpec>& specs) {
    return setBreakpointsForKey(normalisePath(path), specs);
}

std::vector<Controller::Breakpoint> setBreakpointsForKey(const std::string& key,
                                                         const std::vector<BreakpointSpec>& specs) {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    std::vector<Requested>& req = reg.requested[key];
    req.clear();
    for (const BreakpointSpec& sp : specs) {
        Requested r{reg.nextId++, sp.line, nullptr, std::string()};
        auto bp = std::make_shared<LineBreakpoint>();
        bp->condition = sp.condition;
        bp->isLog = sp.isLogpoint;
        bp->logMessage = sp.logMessage;
        bool hitOk = true;
        std::string hit = sp.hitCondition;
        hit.erase(0, hit.find_first_not_of(" \t"));
        if (!hit.empty()) hitOk = parseHitCondition(hit, *bp);
        if (!hitOk)
            r.invalid = "invalid hit condition `" + sp.hitCondition +
                        "`: write N, == N, >= N, > N, < N, <= N or % N";
        else if (!bp->plain())
            r.bp = std::move(bp);
        req.push_back(std::move(r));
    }
    auto it = reg.sources.find(key);
    if (it != reg.sources.end()) return arm(*it->second, req);
    std::vector<Controller::Breakpoint> out;
    for (const Requested& r : req)
        out.push_back({r.id, r.line, false,
                       r.invalid.empty() ? "no code on or after this line (yet)" : r.invalid});
    return out;
}

}  // namespace protoScala::debug
