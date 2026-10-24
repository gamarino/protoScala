/*
 * Debugger — the interpreter's source-level debugging hook (docs/debugging.md).
 *
 * Off unless `protoscala --dap` attached a Controller: then every interpreted
 * frame (ExecutionEngine::runLoop) keeps a FrameRecord on a per-thread chain
 * and reports each instruction; the first instruction of a new source line is
 * checked against the breakpoints and the thread's stepping state, and a match
 * hands the thread to the controller, which parks THAT thread until the client
 * resumes it. Other threads keep running.
 *
 * Every OS thread that runs interpreted code while attached is registered with
 * a stable id and a name (the evaluator thread is "main", a Thread.start body
 * "Thread-N", an actor worker "actor worker N"; what an actor worker runs right
 * now is its activity). A raised language exception is reported through
 * exceptionRaised (ExecutionEngine), which stops when the exception filters ask
 * for it.
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
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoObject;
}

namespace protoScala::debug {

struct LineBreakpoint;  // Debugger.cpp: a breakpoint's condition, hit count, log message

// One source file the session compiled while a debugger was attached.
struct Source {
    std::string path;                  // as the session named it
    std::string key;                   // normalised, for matching client paths
    std::vector<std::uint8_t> hasCode; // per line: some instruction maps to it
    std::unique_ptr<std::atomic<std::uint8_t>[]> armed;  // per line: a breakpoint
    std::size_t lineCount = 0;         // size of both arrays
    // The armed line's breakpoint, when it has a condition, a hit condition or
    // a log message (guarded by the registry's mutex).
    std::map<int, std::shared_ptr<LineBreakpoint>> special;
    bool armedAt(int line) const {
        return line > 0 && static_cast<std::size_t>(line) < lineCount &&
               armed[static_cast<std::size_t>(line)].load(std::memory_order_relaxed) != 0;
    }
};

// What the native code that called a frame does with an exception that leaves
// it (NativeBoundary below).
enum class Boundary : std::uint8_t {
    None,      // called by the interpreter, or by a native that lets it pass
    Captures,  // Try, a Future body, a map/flatMap/recover callback: the value
               // becomes a Failure the program may still handle
    Reports,   // a top level that reports and drops it (a future continuation)
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
    Boundary boundary = Boundary::None;  // how native code called this frame
};

enum class StepMode : int { None = 0, In = 1, Over = 2, Out = 3 };

// An exception stop, as the client is told about it (DAP exceptionInfo).
struct ExceptionStop {
    std::string typeName;     // the exception's class, `ArithmeticException`
    std::string message;      // its message (may be empty)
    std::string description;  // what the stop says, `ArithmeticException: / by zero`
    const char* breakMode;    // "always" (the raised filter) or "userUnhandled"
};

// The receiver of stops: the DAP server.
class Controller {
public:
    virtual ~Controller() = default;
    // Called on the stopped thread, inside the interpreter; returns when the
    // thread may continue. `reason` is a DAP stop reason; `ex` is set for
    // "exception"; `note` (may be null) says why a conditional stop stopped
    // anyway (its condition failed). `threadId` is the calling thread's id.
    // `current` is the thread's innermost live context: anything the stop
    // allocates is a child of it, so no live frame is skipped by the root scan.
    virtual void stopped(FrameRecord& top, proto::ProtoContext* current, const char* reason,
                         int threadId, const ExceptionStop* ex, const std::string* note) = 0;
    // Evaluates `expr` in `frame` (its locals and the script's globals), on
    // the calling thread, with `boundName` (if not null) bound to `bound`.
    // `asText`: the result as its toString, not as the REPL shows it. Runs
    // under Suppress (the caller's): nothing it runs stops.
    struct Evaluation {
        bool ok = false;
        std::string text;  // the rendered result, or the error
    };
    virtual Evaluation evaluateHere(proto::ProtoContext* current, FrameRecord& frame,
                                    const std::string& expr, const char* boundName,
                                    const proto::ProtoObject* bound, bool asText) = 0;
    // Text for the client's console (`category` "console", "stderr").
    virtual void output(const char* category, const std::string& text) = 0;
    // A source was registered and the breakpoints asked for it were resolved
    // (called on the thread that compiled it).
    struct Breakpoint {
        int id;
        int line;        // the line the breakpoint is armed on
        bool verified;   // false: no code on or after the requested line
        std::string message;  // why it is not verified ("" when it is)
    };
    virtual void breakpointsResolved(const std::vector<Breakpoint>& bps) = 0;
    // A thread ran interpreted code for the first time / ended (called on that
    // thread, in order).
    virtual void threadStarted(int threadId) = 0;
    virtual void threadExited(int threadId) = 0;
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

// --- Exceptions ------------------------------------------------------------
// The exception filters the client enabled (setExceptionBreakpoints).
enum : unsigned { kBreakRaised = 1u, kBreakUncaught = 2u };
// `conditions[i]`: the condition of filter bit i (raised, uncaught), "" for
// none -- an expression evaluated in the throwing frame with the exception
// bound as `e`.
void setExceptionBreaks(unsigned mask, const std::string& raisedCondition = std::string(),
                        const std::string& uncaughtCondition = std::string());
// For a filter in `filters` that would stop: true when it has no condition,
// its condition holds, or its condition failed (then *note says why). Called
// on the throwing thread; `value` is the exception value (may be null).
bool exceptionConditionHolds(unsigned filters, proto::ProtoContext* current, FrameRecord& top,
                             const proto::ProtoObject* value, std::string* note);
extern std::atomic<unsigned> gExceptionBreaks;
inline unsigned exceptionBreaks() { return gExceptionBreaks.load(std::memory_order_relaxed); }
// True inside Suppress: the debugger's own evaluations never stop.
bool suppressed();
// One exception is reported once, however many frames it passes through and
// however often a handler re-raises it. `value` identifies a language value
// (pinned while it is remembered, so its address cannot be reused), `error` a
// native ScalaError not materialised yet. Both are per thread, and forgotten
// when the thread leaves its outermost frame or reports another exception.
// The `error` a native ScalaError is reported under: between its report and
// the handler that materialises it, nothing else runs on the thread.
inline constexpr char kNativeErrorTag = 0;
inline constexpr const void* kNativeError = &kNativeErrorTag;
bool alreadyReported(const proto::ProtoObject* value, const void* error);
void markReported(proto::ProtoContext* ctx, const proto::ProtoObject* value, const void* error);
// A handler materialised the native error `error` into `value`: if `error`
// was reported, so is `value` (its RETHROW is the same exception).
void noteMaterialised(proto::ProtoContext* ctx, const void* error, const proto::ProtoObject* value);
// Hands the calling thread to the controller for an exception stop at `top`;
// `current` is the thread's innermost live context.
void stopForException(FrameRecord& top, proto::ProtoContext* current, const ExceptionStop& ex,
                      const std::string* note);

// While alive on a native's C++ stack, the frames the native calls are marked
// with `kind` (FrameRecord::boundary): the uncaught-exception filter stops its
// search for a handler there. No-op while detached.
class NativeBoundary {
public:
    explicit NativeBoundary(Boundary kind);
    ~NativeBoundary();
    NativeBoundary(const NativeBoundary&) = delete;
    NativeBoundary& operator=(const NativeBoundary&) = delete;

private:
    bool active_ = false;
    Boundary savedKind_ = Boundary::None;
    const FrameRecord* savedParent_ = nullptr;
    bool savedOn_ = false;
};

// --- Threads ---------------------------------------------------------------
// The name the calling thread is registered under when it first runs
// interpreted code ("main", "Thread-3"). Only before that point; no-op while
// detached.
void nameThread(const std::string& name);
// While alive, what the calling thread is running, shown after its name
// ("actor#2 at x.scala:7"). Set only while attached.
class Activity {
public:
    Activity() = default;
    ~Activity();
    void set(const std::string& text);
    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;

private:
    bool set_ = false;
};
// `x.scala:7`: the innermost source line the calling thread is running, or
// "" when it runs none (detached, or only the prelude).
std::string currentLocation();
// The calling thread's id, 0 when it has not run interpreted code yet.
int currentThreadId();
struct ThreadView {
    int id;
    std::string name;
};
// The registered threads that are still alive, by id.
std::vector<ThreadView> threads();

// --- The session's side ----------------------------------------------------
// Registers `mod` and its blocks as compiled from `path` and arms the
// breakpoints the client set on that file. No-op while detached.
void registerModule(const BytecodeModule& mod, const std::string& path);

// --- The controller's side -------------------------------------------------
// Replaces the breakpoints of `path` (a client path). Answers one entry per
// line, in order; a file not compiled yet answers unverified entries, and
// breakpointsResolved reports them once it is.
// A line breakpoint as the client set it. `condition`: stop only when it is
// true; `hitCondition`: `N`/`== N` (the Nth hit), `>= N`, `> N`, `< N`, `<= N`,
// `% N` (every Nth), counting the hits whose condition held; `logMessage`:
// print it (with `{expr}` parts evaluated, `{{`/`}}` literal braces) instead
// of stopping. An invalid hitCondition answers the breakpoint unverified.
struct BreakpointSpec {
    int line = 0;
    std::string condition;
    std::string hitCondition;
    std::string logMessage;
    bool isLogpoint = false;  // logMessage was given (it may be empty)
};
std::vector<Controller::Breakpoint> setBreakpoints(const std::string& path,
                                                   const std::vector<BreakpointSpec>& specs);
// The stepping request for the CALLING thread (the stopped one), relative to
// the frame `top` it is stopped in. Other threads are not affected.
void setStep(StepMode mode, const FrameRecord& top);
// Stops the first thread that reaches a new line ("entry" for stopOnEntry).
void requestStop(const char* reason);
// Stops thread `threadId` at its next new line ("pause"). False when no such
// thread is alive.
bool requestPause(int threadId);
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
