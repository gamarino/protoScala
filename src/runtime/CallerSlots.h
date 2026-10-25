/*
 * CallerSlots — how a native roots what it holds without a context.
 *
 * Adapted from protoClojure's src/runtime/CallerSlots.h (its main, 849f91f),
 * so the two runtimes follow one pattern.
 *
 * The maintainer's context rule: a ProtoContext is created only when a method,
 * interpreted code with its own locals, is activated. A native method (a C++
 * ProtoMethod) and every helper it calls run in the caller's ProtoContext: the
 * frame of the Scala body that called it, or the context `invoke` was given.
 *
 * What protects a native's values, then:
 *
 *  - Its arguments are in traced slots: the caller frame's operand stack, or
 *    storage the caller of `invoke` roots.
 *  - What it allocates is a young cell of the caller's context, and a young
 *    cell stays live until that context's chain is submitted, by its
 *    `safepoint()` or its destruction (protoCore EMBEDDER-CONFORMANCE rules 1
 *    and 3; ProtoContext::safepoint documents the young chain as a root).
 *  - A callback's result is anchored in the caller's context: the callee
 *    frame's destructor places a ReturnReference in its `previous`.
 *  - While a native runs, nothing submits the caller's chain except
 *    CallerSlots::safepoint, and that does so only under a SafepointPermit
 *    (below). The frame's dispatch loop is suspended at the call, and every
 *    callback runs in a frame of its own, whose safepoints submit only that
 *    frame's chain.
 *
 * So without a permit a native needs no slots at all for correctness, except
 * for a value read from shared mutable state (a registry another thread
 * updates) that another thread may drop while the native still uses it: `pin`
 * it.
 *
 * Bounded memory needs more. A native that loops for long (foreach, map,
 * foldLeft over a large collection) produces garbage, and the caller's chain
 * keeps all of it until it is submitted: at least one ReturnReference per
 * callback. A loop hands it to the collector by calling `tick()`, which submits
 * the caller frame's chain every kTickStride calls, and that is safe only when
 * every value alive on the frame is in a traced slot. The frame's own values
 * are (the VM holds nothing in C++ locals at a call); the native's must be in
 * CallerSlots or in its arguments. Two conditions make that checkable:
 *
 *  - The native is the frame's outermost native activation (depth 1 of the
 *    PrimitiveWindow): a native called through `invoke` by another one never
 *    submits, because its caller may hold C++ locals.
 *  - The native's body holds a SafepointPermit: its promise that, while the
 *    permit lives, it keeps every value it needs across a tick in CallerSlots
 *    or in its arguments. A helper it calls (ListBuilder) may then tick; each
 *    helper keeps its own values in CallerSlots and returns its result through
 *    `ret()`, which anchors the result in the caller's chain when a submission
 *    happened while the helper ran.
 *
 * The slots: every VM frame (ExecutionEngine::execute, resumeFrames, and a
 * transpiled gen::Frame) keeps kPrimitiveSlots spare slots above its operand
 * stack and publishes them with a FrameSpareScope; callNative installs a
 * PrimitiveWindow over them around each native call (NativeCallScope). Taking
 * slots is a pointer bump; releasing clears them. With no window (a native
 * called from host code: an actor turn, a thread entry, the REPL) or with the
 * window used up, CallerSlots holds the values in its own C++ array, which is
 * correct because nothing submits the caller's chain then (tick does nothing;
 * `pin` anchors the value in the caller's young chain with one list cell).
 */
#pragma once

#include <cstddef>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoObject;
}

namespace protoScala {

// Spare slots every VM frame keeps above its operand stack for the natives it
// calls (CallerSlots).
constexpr unsigned int kPrimitiveSlots = 8;

// The free traced slots of the frame whose native is running.
struct PrimitiveWindow {
    proto::ProtoContext*        frame;  // the frame that owns the slots
    const proto::ProtoObject**  next;   // first free slot
    const proto::ProtoObject**  end;    // one past the last slot
    unsigned int                depth;  // native activations sharing it
    unsigned int                local = 0;        // live CallerSlots outside the window
    unsigned int                permits = 0;      // live SafepointPermits
    unsigned int                submissions = 0;  // safepoint() calls made
};

// The window of the native call in progress on this thread, or nullptr.
PrimitiveWindow*& currentPrimitiveWindow();

// The spare slots of the innermost VM frame running on this thread. The
// thread-local itself lives in ExecutionEngine.cpp, next to `execute`, which
// publishes and restores it on every activation: one translation unit, so the
// access is a direct thread-local access rather than a call (measured: an
// out-of-line accessor cost fib(30) 12 %).
struct FrameSpare {
    proto::ProtoContext*       frame = nullptr;
    const proto::ProtoObject** slots = nullptr;  // kPrimitiveSlots slots
};
FrameSpare& currentFrameSpare();

// Installed by ExecutionEngine::callNative around every native call made in
// `ctx`:
//  - a native called by another native on the same frame (through `invoke`)
//    is one more activation on the current window, so it never submits a
//    chain its caller relies on;
//  - a native called by the innermost VM frame gets a window of depth 1 over
//    that frame's spare slots;
//  - a native called in any other context (host code) runs with no window.
// Restores the previous window on every exit path.
class NativeCallScope {
public:
    // `spare` is the innermost VM frame's (currentFrameSpare()).
    NativeCallScope(proto::ProtoContext* ctx, const FrameSpare& spare);
    ~NativeCallScope();
    NativeCallScope(const NativeCallScope&) = delete;
    NativeCallScope& operator=(const NativeCallScope&) = delete;
private:
    PrimitiveWindow  own_{};
    PrimitiveWindow* saved_;
    bool             nested_ = false;
};

// A native body's promise, for as long as it lives, that every value it holds
// across a tick is in CallerSlots or in its arguments, so the helpers it calls
// may submit the caller frame's chain (CallerSlots::tick). Takes effect only
// for the frame's outermost native activation.
class SafepointPermit {
public:
    explicit SafepointPermit(proto::ProtoContext* ctx);
    ~SafepointPermit();
    SafepointPermit(const SafepointPermit&) = delete;
    SafepointPermit& operator=(const SafepointPermit&) = delete;
private:
    PrimitiveWindow* w_;
};

// `n` traced slots (at most kMaxSlots) for code running in `ctx` on behalf of
// a native, set to PROTO_NONE. Released, cleared, at scope exit; they are a
// stack, released in reverse order of acquisition.
class CallerSlots {
public:
    static constexpr unsigned int kMaxSlots = 8;
    static constexpr unsigned int kTickStride = 64;

    CallerSlots(proto::ProtoContext* ctx, unsigned int n);
    ~CallerSlots();
    CallerSlots(const CallerSlots&) = delete;
    CallerSlots& operator=(const CallerSlots&) = delete;

    proto::ProtoContext* ctx() const { return ctx_; }
    const proto::ProtoObject* get(unsigned int i) const { return slots_[i]; }
    void set(unsigned int i, const proto::ProtoObject* v) { slots_[i] = v; }
    // set() for a value read from shared mutable state, which another thread
    // may drop: outside a window it is also anchored in the caller's chain.
    void pin(unsigned int i, const proto::ProtoObject* v);
    // The slots as a contiguous array (argument lists for `invoke`).
    const proto::ProtoObject** data() const { return slots_; }
    // True when the slots are traced (in a PrimitiveWindow).
    bool traced() const { return inWindow_; }

    // One iteration of a loop: every kTickStride calls, safepoint(). Call it
    // only where everything the code still needs is in these slots, in
    // CallerSlots of its callers, or in the native's arguments.
    void tick() { if (++ticks_ % kTickStride == 0) safepoint(); }
    // Hands the caller frame's young generation to the collector (as
    // ~ProtoContext does: the chain is detached and submitted, then
    // ProtoContext::safepoint parks for a pending stop-the-world) when that is
    // safe: these slots are in the
    // window of the frame `ctx`, the native is the frame's outermost
    // activation, its body holds a SafepointPermit and no CallerSlots of it is
    // outside the window. Otherwise it does nothing.
    void safepoint();
    // `v`, a result about to leave this scope, anchored in the caller's chain
    // if a submission happened since this scope began (it may then be held
    // only by these slots).
    const proto::ProtoObject* ret(const proto::ProtoObject* v);

private:
    proto::ProtoContext*        ctx_;
    PrimitiveWindow*            window_;   // current at construction, or nullptr
    const proto::ProtoObject**  slots_;
    unsigned int                n_;
    unsigned int                ticks_ = 0;
    unsigned int                submissionsAtStart_ = 0;
    bool                        inWindow_ = false;
    const proto::ProtoObject*   local_[kMaxSlots];
};

// Anchors `v` in `ctx`'s young chain with one list cell, so it stays live
// until that chain is submitted: for a value the caller must keep after it
// became unreachable from shared state (a batch taken from a mailbox).
void anchorInContext(proto::ProtoContext* ctx, const proto::ProtoObject* v);

// The contiguous argument array of a call assembled in C++ (a bound method's
// [self, args], a spread call's arguments, `send`'s [receiver, args], a
// native's re-packed ProtoArguments).
// It needs no traced storage and no context: every value in it is rooted by
// the caller (in its slots, or reachable from them) or is a young cell of the
// caller's context, and nothing submits that context's young chain while the
// call runs (CallerSlots.h). A callee frame copies it into its own slots.
class ArgBuffer {
public:
    explicit ArgBuffer(std::size_t n) : p_(inline_) {
        if (n > kInline) {
            more_.resize(n);
            p_ = more_.data();
        }
    }
    ArgBuffer(const ArgBuffer&) = delete;
    ArgBuffer& operator=(const ArgBuffer&) = delete;
    const proto::ProtoObject** data() { return p_; }
    const proto::ProtoObject*& operator[](std::size_t i) { return p_[i]; }

private:
    static constexpr std::size_t kInline = 8;
    const proto::ProtoObject* inline_[kInline];
    std::vector<const proto::ProtoObject*> more_;
    const proto::ProtoObject** p_;
};

} // namespace protoScala
