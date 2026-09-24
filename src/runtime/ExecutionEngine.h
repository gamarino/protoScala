/*
 * ExecutionEngine — the recursive bytecode VM (DESIGN §3.6).
 *
 * execute() recurses natively once per call; each frame owns one
 * ProtoContext (P2) whose automatic locals hold parameters, locals and the
 * operand stack (P1), sized exactly from the module's metadata. The stack
 * guard runs on every call. Native primitives re-enter the VM through
 * activeCallContext()->engine->invoke(), which run() installs on the thread
 * and restores on every exit path (protoClojure src/runtime/Primitives.h:109-170).
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/Opcodes.h"
#include "runtime/Errors.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <string>

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoString;
}

namespace protoScala {

class BytecodeModule;
class ExecutionEngine;

struct ActiveCallContext {
    ExecutionEngine* engine;
    const RuntimeLayout* layout;
};
const ActiveCallContext* activeCallContext();

class ExecutionEngine {
public:
    explicit ExecutionEngine(const RuntimeLayout& layout) : layout_(layout) {}

    // Top-level entry: runs an arity-0 module under a child frame of `parent`.
    // The result is anchored in `parent` (returnValue) but the caller must
    // still root it before allocating. Throws ScalaError.
    const proto::ProtoObject* run(proto::ProtoContext* parent, const BytecodeModule& mod);

    // Calls any Scala callable: compiled function, native method, or any
    // value with an `apply` method (DESIGN §5.1). `args` must be rooted.
    const proto::ProtoObject* invoke(proto::ProtoContext* ctx, const proto::ProtoObject* callable,
                                     const proto::ProtoObject* const* args, unsigned argc);

    // Like invoke, but installs this engine's ActiveCallContext (entry from
    // C++ code that is not itself running inside the VM, e.g. the @main call).
    const proto::ProtoObject* callTopLevel(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* callable,
                                           const proto::ProtoObject* const* args, unsigned argc);

    // Scala method call `receiver.name(args)`; `args` must be rooted.
    const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                   const proto::ProtoString* name,
                                   const proto::ProtoObject* const* args, unsigned argc);

    // new cls(args) through the class's primary constructor (Product.copy).
    // `args` must be rooted.
    const proto::ProtoObject* construct(proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                                        const proto::ProtoObject* const* args, unsigned argc);
    // show() with this engine active, so Scala toString methods run (REPL
    // echo, unit tests). Throws ScalaError.
    std::string showTopLevel(proto::ProtoContext* ctx, const proto::ProtoObject* v);

    // Materialises a lazy holder (a `lazy val`, an `object` singleton) and
    // returns its value; any other value passes through. Public because a module
    // import forces the module's object singleton, which is what runs its top
    // level (Phase 6 D90).
    const proto::ProtoObject* force(proto::ProtoContext* ctx, const proto::ProtoObject* v);

    // Installs `engine`/`layout` as this thread's active call context for the
    // guard's lifetime. A worker thread re-enters the VM from C++ that never
    // came through run(), so it installs the context itself (protoClojure's
    // scheduler blueprint).
    class ActiveCallGuard {
    public:
        ActiveCallGuard(ExecutionEngine* engine, const RuntimeLayout* layout);
        ~ActiveCallGuard();
        ActiveCallGuard(const ActiveCallGuard&) = delete;
        ActiveCallGuard& operator=(const ActiveCallGuard&) = delete;

    private:
        ActiveCallContext saved_;
        bool wasSet_;
    };

    // Native frames between the running bytecode frame and here: 1 while a
    // native method runs, more when a native re-entered the VM. `await` refuses
    // to suspend above 1, because the extra C++ frames cannot be snapshotted (D43).
    static unsigned nativeReentryDepth();
    // Raises that depth for a region of C++ that re-enters the VM without going
    // through callNative (a future continuation, DESIGN §8.3).
    class NativeDepthGuard {
    public:
        NativeDepthGuard();
        ~NativeDepthGuard();
        NativeDepthGuard(const NativeDepthGuard&) = delete;
        NativeDepthGuard& operator=(const NativeDepthGuard&) = delete;
    };

    // Re-materialises a suspended call chain and continues it. `frames` is the
    // actor's snapshot list, outermost first; `injected` is the value the
    // await that suspended the chain must return. Frame `idx` is rebuilt, the
    // inner frames run first, and their result is written where the in-flight
    // call would have left it (DESIGN §8.3).
    // `injected` is the value the suspending await must return; when
    // `injectedThrow` is non-null the await must instead raise it (a failed
    // future, plan A0-7 invariant 5). Exactly one of the two is non-null.
    const proto::ProtoObject* resumeFrames(proto::ProtoContext* parent,
                                           const proto::ProtoList* frames, unsigned idx,
                                           const proto::ProtoObject* injected,
                                           const proto::ProtoObject* injectedThrow = nullptr);

    // A native ScalaError as a prelude Throwable instance (plan A0-6). Public
    // so the actor scheduler can complete a failed ask with a real exception
    // value (plan A0-7 invariant 6).
    const proto::ProtoObject* materialise(proto::ProtoContext* ctx, const ScalaError& e);

private:
    static constexpr unsigned kNoPendingCall = 0xFFFFFFFFu;
    const RuntimeLayout& layout_;

    // The dispatch loop of one frame. On an exception it writes the word index
    // of the faulting instruction to *faultPc and rethrows; runFrame uses it to
    // search the module's handler table (plan A0-3).
    const proto::ProtoObject* runLoop(proto::ProtoContext& frame, const BytecodeModule& mod,
                                      const proto::ProtoObject** slots,
                                      const proto::ProtoObject** sp, const Instr* ip,
                                      std::size_t* faultPc);

    // One frame, with its handler table active. `execute` and `resumeFrames`
    // call this, never runLoop directly (escalation E2): the handler body is
    // entered by `continue`, OUTSIDE the C++ catch block. Two things depend on
    // that, and the second is the load-bearing one:
    //
    //  - the handler body runs with no live C++ handler, an ordinary ip and an
    //    ordinary operand stack, so it suspends cooperatively like any other
    //    bytecode;
    //  - the frame can catch a SECOND exception — one raised by its own handler
    //    body, by the RETHROW a non-matching cascade emits, or by a `finally` —
    //    because the loop re-enters this frame's own try region. Entering the
    //    handler from inside the catch abandons the loop, and the frame's handler
    //    table is then never consulted again: measured, that turns 11 fixtures
    //    red (see docs/DECISIONS-LOG.md, E2).
    const proto::ProtoObject* runFrame(proto::ProtoContext& frame, const BytecodeModule& mod,
                                       const proto::ProtoObject** slots,
                                       const proto::ProtoObject** sp, const Instr* ip);
    // Re-enters a resumed frame as if its in-flight call had thrown `payload`:
    // the handler search runs at the pc of that call instruction (ipOffset - 1,
    // the same index runLoop's own catch reports), and then the ordinary retry
    // loop takes over. This is what makes `try { f.await } catch { ... }` work
    // across a cooperative suspension (plan A0-7 invariant 5, retiring D50).
    const proto::ProtoObject* runFrameRaising(proto::ProtoContext& frame, const BytecodeModule& mod,
                                              const proto::ProtoObject** slots,
                                              std::size_t ipOffset,
                                              const proto::ProtoObject* payload);
    // Enters `h`: resets the operand stack, writes `value` into the handler's
    // slot and sets `*ip`. Returns the new stack pointer.
    const proto::ProtoObject** enterHandler(const BytecodeModule& mod,
                                            const proto::ProtoObject** slots,
                                            const BytecodeModule::Handler& h,
                                            const proto::ProtoObject* value, const Instr** ip);
    // A native ScalaError as a prelude Throwable instance. Allocates, so it is
    // called only once a handler is known to exist: the uncaught path must not
    // allocate, because it may be dying of OutOfMemoryError.
    const proto::ProtoObject* materialiseError(proto::ProtoContext* ctx, const ScalaError& e);

    // base[0] is the receiver, base[1..argc] the arguments; all rooted.
    // `applied`: the call site wrote an argument list (SEND_APPLY), so a member
    // that is not a method is applied instead of selected.
    const proto::ProtoObject* dispatch(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                       const proto::ProtoString* name, unsigned argc,
                                       bool applied = false);
    const proto::ProtoObject* callMember(proto::ProtoContext* ctx, const proto::ProtoObject* member,
                                         const proto::ProtoObject** base, unsigned argc,
                                         bool applied = false);
    // The name a send site uses on this receiver: a private member's
    // class-qualified key, or the site's plain fallback name (D5).
    const proto::ProtoString* siteName(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                       const BytecodeModule::Const& site) const;
    const proto::ProtoObject* callWithReceiver(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                               const proto::ProtoObject** base, unsigned argc,
                                               const proto::ProtoSparseList* keywords = nullptr);
    const proto::ProtoObject* bindMethod(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                         const proto::ProtoObject* receiver);
    const proto::ProtoObject* forceMember(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                          const proto::ProtoObject* receiver);
    const proto::ProtoObject* instantiate(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                          const proto::ProtoString* ctorKey, unsigned argc,
                                          const proto::ProtoSparseList* keywords = nullptr);
    [[gnu::noinline]] const proto::ProtoObject* makeClass(proto::ProtoContext* ctx,
                                                          const BytecodeModule::Const& spec,
                                                          const proto::ProtoObject* const* base);
    const proto::ProtoObject* makeTuple(proto::ProtoContext* ctx, const proto::ProtoObject* const* elems,
                                        unsigned n);
    const proto::ProtoObject* superSend(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                        const BytecodeModule::Const& site);
    const proto::ProtoObject* sendKeywords(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                           const BytecodeModule::Const& site);
    // CALL_KW: `f(x = 1)` where `f` is a function value or a global def, which
    // SEND_KW cannot express (it has no receiver). base[0] is the callee.
    const proto::ProtoObject* callKeywords(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                           const BytecodeModule::Const& site);
    // Binds a keyword ProtoSparseList into a callee frame's parameter slots and
    // fills the rest from the callee's default blocks. `positional` is the
    // number of slots the positional arguments already wrote. Raises
    // IllegalArgumentException for an unknown name, a duplicate and a missing
    // argument — each naming what was expected and what arrived (plan A0-11).
    void bindKeywordsAndDefaults(proto::ProtoContext& frame, const BytecodeModule& mod,
                                 const proto::ProtoSparseList* keywords,
                                 const proto::ProtoObject** slots, unsigned positional);
    bool testType(proto::ProtoContext* ctx, TypeCode code, const proto::ProtoObject* v) const;
    [[noreturn]] void throwMissingMember(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                         const proto::ProtoString* name) const;

    const proto::ProtoObject* execute(proto::ProtoContext* parent, const BytecodeModule& mod,
                                      const proto::ProtoObject* const* args, unsigned argc,
                                      const proto::ProtoObject* captures,
                                      const proto::ProtoSparseList* keywords = nullptr);
    const proto::ProtoObject* callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                         const proto::ProtoObject* self,
                                         const proto::ProtoObject* const* args, unsigned argc);
    [[gnu::noinline]] const proto::ProtoObject* slowBinary(proto::ProtoContext* ctx, Op op,
                                                           const proto::ProtoObject* a,
                                                           const proto::ProtoObject* b);
};

} // namespace protoScala
