#include "runtime/Debugger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>

namespace protoScala::debug {

std::atomic<bool> gAttached{false};

namespace {

std::atomic<Controller*> gController{nullptr};

// Stepping: one request at a time, for the thread that was stopped.
std::atomic<int> gStepMode{0};
std::atomic<const void*> gStepThread{nullptr};
std::atomic<unsigned> gStepDepth{0};
// A stop any thread takes at its next new line ("entry", "pause"), or nullptr.
std::atomic<const char*> gStopRequest{nullptr};

thread_local FrameRecord* tTop = nullptr;
thread_local bool tSuppressed = false;
thread_local char tToken;  // its address identifies the thread

struct Requested {
    int id;
    int line;
};

struct Registry {
    std::mutex mu;
    std::map<std::string, std::unique_ptr<Source>> sources;      // by key
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
    std::vector<Controller::Breakpoint> out;
    for (const Requested& r : req) {
        const int at = resolveLine(s, r.line);
        if (at > 0) s.armed[static_cast<std::size_t>(at)].store(1, std::memory_order_relaxed);
        out.push_back({r.id, at > 0 ? at : r.line, at > 0});
    }
    return out;
}

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
    gController.store(nullptr, std::memory_order_release);
    gStepMode.store(0);
    gStopRequest.store(nullptr);
}

void enterFrame(FrameRecord& r, const BytecodeModule& mod, proto::ProtoContext* ctx,
                const proto::ProtoObject** slots) {
    r.parent = tTop;
    r.depth = tTop ? tTop->depth + 1 : 1;
    r.mod = &mod;
    r.src = static_cast<const Source*>(mod.debugSource());
    r.ctx = ctx;
    r.slots = slots;
    r.pc = 0;
    r.line = -1;
    tTop = &r;
}

void leaveFrame(FrameRecord& r) {
    tTop = r.parent;
    // The frame a step over or a step out started in returns: the step ends in
    // its caller, at the call site, rather than at the caller's next line --
    // which for `n * fact(n - 1)` would be after the multiplication and the
    // caller's own return. A prelude frame in between (List.map running a
    // lambda) has no line to show, so the mark goes to the nearest one that has.
    const auto mode = static_cast<StepMode>(gStepMode.load(std::memory_order_relaxed));
    if ((mode == StepMode::Over || mode == StepMode::Out) &&
        gStepThread.load(std::memory_order_relaxed) == &tToken &&
        r.depth <= gStepDepth.load(std::memory_order_relaxed)) {
        for (FrameRecord* p = r.parent; p; p = p->parent)
            if (p->src) {
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
    if (gStopRequest.load(std::memory_order_relaxed)) reason = gStopRequest.exchange(nullptr);
    if (!reason) {
        const auto mode = static_cast<StepMode>(gStepMode.load(std::memory_order_relaxed));
        if (mode != StepMode::None && gStepThread.load(std::memory_order_relaxed) == &tToken) {
            const unsigned depth = gStepDepth.load(std::memory_order_relaxed);
            if (mode == StepMode::In || (mode == StepMode::Over && r.depth <= depth) ||
                (mode == StepMode::Out && r.depth < depth))
                reason = "step";
        }
    }
    if (!reason && r.src->armedAt(r.line)) reason = "breakpoint";
    if (!reason) return;
    gStepMode.store(0, std::memory_order_relaxed);
    c->stopped(r, reason);
}

void setStep(StepMode mode, const FrameRecord& top) {
    gStepThread.store(&tToken, std::memory_order_relaxed);
    gStepDepth.store(top.depth, std::memory_order_relaxed);
    gStepMode.store(static_cast<int>(mode), std::memory_order_release);
}

void requestStop(const char* reason) { gStopRequest.store(reason, std::memory_order_release); }

FrameRecord* currentFrame() { return tTop; }

Suppress::Suppress() : saved_(tSuppressed) { tSuppressed = true; }
Suppress::~Suppress() { tSuppressed = saved_; }

void registerModule(const BytecodeModule& mod, const std::string& path) {
    if (!attached() || path.empty() || path.front() == '<') return;
    std::vector<std::uint8_t> lines;
    markLines(mod, lines);
    std::vector<Controller::Breakpoint> resolved;
    {
        Registry& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mu);
        const std::string key = normalisePath(path);
        auto& slot = reg.sources[key];
        if (!slot) {
            slot = std::make_unique<Source>();
            // Absolute, with the platform's separators: what a client opens.
            std::error_code ec;
            const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);
            slot->path = ec ? path : abs.lexically_normal().make_preferred().string();
            slot->key = key;
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
    if (c && !resolved.empty()) c->breakpointsResolved(resolved);
}

std::vector<Controller::Breakpoint> setBreakpoints(const std::string& path,
                                                   const std::vector<int>& lines) {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mu);
    const std::string key = normalisePath(path);
    std::vector<Requested>& req = reg.requested[key];
    req.clear();
    for (int l : lines) req.push_back({reg.nextId++, l});
    auto it = reg.sources.find(key);
    if (it != reg.sources.end()) return arm(*it->second, req);
    std::vector<Controller::Breakpoint> out;
    for (const Requested& r : req) out.push_back({r.id, r.line, false});
    return out;
}

}  // namespace protoScala::debug
