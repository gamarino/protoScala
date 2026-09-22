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
#include "compiler/Opcodes.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

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

    // Scala method call `receiver.name(args)`; `args` must be rooted.
    const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                   const proto::ProtoString* name,
                                   const proto::ProtoObject* const* args, unsigned argc);

private:
    const RuntimeLayout& layout_;

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
