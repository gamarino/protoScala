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

private:
    const RuntimeLayout& layout_;

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
                                               const proto::ProtoObject** base, unsigned argc);
    const proto::ProtoObject* bindMethod(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                         const proto::ProtoObject* receiver);
    const proto::ProtoObject* forceMember(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                          const proto::ProtoObject* receiver);
    const proto::ProtoObject* instantiate(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                          const proto::ProtoString* ctorKey, unsigned argc);
    [[gnu::noinline]] const proto::ProtoObject* makeClass(proto::ProtoContext* ctx,
                                                          const BytecodeModule::Const& spec,
                                                          const proto::ProtoObject* const* base);
    const proto::ProtoObject* makeTuple(proto::ProtoContext* ctx, const proto::ProtoObject* const* elems,
                                        unsigned n);
    const proto::ProtoObject* superSend(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                        const BytecodeModule::Const& site);
    const proto::ProtoObject* sendKeywords(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                           const BytecodeModule::Const& site);
    bool testType(proto::ProtoContext* ctx, TypeCode code, const proto::ProtoObject* v) const;
    [[noreturn]] void throwMissingMember(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                         const proto::ProtoString* name) const;

    const proto::ProtoObject* execute(proto::ProtoContext* parent, const BytecodeModule& mod,
                                      const proto::ProtoObject* const* args, unsigned argc,
                                      const proto::ProtoObject* captures);
    const proto::ProtoObject* callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                         const proto::ProtoObject* self,
                                         const proto::ProtoObject* const* args, unsigned argc);
    const proto::ProtoObject* force(proto::ProtoContext* ctx, const proto::ProtoObject* v);
    [[gnu::noinline]] const proto::ProtoObject* slowBinary(proto::ProtoContext* ctx, Op op,
                                                           const proto::ProtoObject* a,
                                                           const proto::ProtoObject* b);
};

} // namespace protoScala
