/*
 * Debugger — the interpreter's source-level debugging hook (docs/debugging.md).
 *
 * Off unless `protoscala --dap` attached a Controller: then every interpreted
 * frame (ExecutionEngine::runLoop) keeps a FrameRecord on a per-thread chain
 * and reports each instruction; the first instruction of a new source line is
 * checked against the breakpoints and the stepping state, and a match hands the
 * thread to the controller, which parks it until the client resumes.
 *
 * Cost when detached: one well-predicted branch per frame entry. The per-
 * instruction check is only compiled into frames that started while attached.
 *
 * Only interpreted bytecode is seen: a transpiled module (protoscalac) runs as
 * native code and has no line table at run time.
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "support/Attributes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoObject;
}

namespace protoScala::debug {

// One source file the session compiled while a debugger was attached.
struct Source {
    std::string path;                  // as the session named it
    std::string key;                   // normalised, for matching client paths
    std::vector<std::uint8_t> hasCode; // per line: some instruction maps to it
    std::unique_ptr<std::atomic<std::uint8_t>[]> armed;  // per line: a breakpoint
    std::size_t lineCount = 0;         // size of both arrays
    bool armedAt(int line) const {
        return line > 0 && static_cast<std::size_t>(line) < lineCount &&
               armed[static_cast<std::size_t>(line)].load(std::memory_order_relaxed) != 0;
    }
};

// One running interpreted frame. Lives on runLoop's C++ stack.
struct FrameRecord {
    FrameRecord* parent = nullptr;     // the caller's record on this thread
    unsigned depth = 0;                // 1 for the outermost record of the thread
    const BytecodeModule* mod = nullptr;
    const Source* src = nullptr;       // nullptr: the prelude, a REPL line...
    proto::ProtoContext* ctx = nullptr;          // the frame's context
    const proto::ProtoObject** slots = nullptr;  // parameters, locals, captures
    std::size_t pc = 0;                // the instruction being executed
    int line = -1;                     // its source line
};

enum class StepMode : int { None = 0, In = 1, Over = 2, Out = 3 };

// The receiver of stops: the DAP server.
class Controller {
public:
    virtual ~Controller() = default;
    // Called on the stopped thread, inside the interpreter; returns when the
    // thread may continue. `reason` is a DAP stop reason.
    virtual void stopped(FrameRecord& top, const char* reason) = 0;
    // A source was registered and the breakpoints asked for it were resolved
    // (called on the thread that compiled it).
    struct Breakpoint {
        int id;
        int line;        // the line the breakpoint is armed on
        bool verified;   // false: no code on or after the requested line
    };
    virtual void breakpointsResolved(const std::vector<Breakpoint>& bps) = 0;
};

// --- Attaching (any thread, before the debuggee starts) -------------------
void attach(Controller* c);
void detach();
extern std::atomic<bool> gAttached;
inline bool attached() { return gAttached.load(std::memory_order_relaxed); }

// --- The interpreter's side (ExecutionEngine) ------------------------------
void enterFrame(FrameRecord& r, const BytecodeModule& mod, proto::ProtoContext* ctx,
                const proto::ProtoObject** slots);
void leaveFrame(FrameRecord& r);
PROTOSCALA_NOINLINE void newLine(FrameRecord& r);
inline void atInstruction(FrameRecord& r, std::size_t pc) {
    const int line = r.mod->lineAt(pc);
    // A new line, or the same line entered again by a backward jump (a loop).
    const bool fresh = line != r.line || pc < r.pc;
    r.pc = pc;
    if (!fresh) return;
    r.line = line;
    newLine(r);
}

// --- The session's side ----------------------------------------------------
// Registers `mod` and its blocks as compiled from `path` and arms the
// breakpoints the client set on that file. No-op while detached.
void registerModule(const BytecodeModule& mod, const std::string& path);

// --- The controller's side -------------------------------------------------
// Replaces the breakpoints of `path` (a client path). Answers one entry per
// line, in order; a file not compiled yet answers unverified entries, and
// breakpointsResolved reports them once it is.
std::vector<Controller::Breakpoint> setBreakpoints(const std::string& path,
                                                   const std::vector<int>& lines);
// The stepping request for the CALLING thread (the stopped one), relative to
// the frame `top` it is stopped in.
void setStep(StepMode mode, const FrameRecord& top);
// Stops the first thread that reaches a new line ("entry" for stopOnEntry,
// "pause" for a pause request).
void requestStop(const char* reason);
// The innermost record of the calling thread.
FrameRecord* currentFrame();
// While alive, the calling thread runs without stopping (the debugger's own
// evaluations and toString calls).
class Suppress {
public:
    Suppress();
    ~Suppress();
    Suppress(const Suppress&) = delete;
    Suppress& operator=(const Suppress&) = delete;

private:
    bool saved_;
};

// `c:\Dir\x.scala`, `C:/dir/./x.scala` -> one key (absolute, normalised; on
// Windows lower-case with forward slashes).
std::string normalisePath(const std::string& path);

}  // namespace protoScala::debug
