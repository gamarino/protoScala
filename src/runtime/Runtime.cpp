#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <string>

namespace protoScala {

namespace {
enum RootSlot : unsigned {
    kGlobals, kAny, kInt, kDouble, kBoolean, kChar, kString, kList, kUnitProto,
    kFunction, kFunctionArities, kCell, kLazy, kUnit,
    kAnyRef, kProduct, kSerializable, kWithFilter, kListCompanion, kTuples, kTupleCompanions,
    kActorProto, kFutureProto, kEnvelopeProto, kThreadProto, kFrameProto, kSpawnPartialProto,
    kActorRegistry, kActorCompanion, kPriorityCompanion, kFutureCompanion, kThreadCompanion,
    kSystemCompanion,
    kRangeProto, kVectorProto, kMapProto, kSetProto, kFoldPartialProto,
    kVectorCompanion, kMapCompanion, kSetCompanion,
    kEnumProto,
    kRootSlotCount
};
} // namespace

Runtime::Runtime(proto::ProtoSpace& space) : space_(space) {
    proto::ProtoContext* ctx = space.rootContext;
    ctx->resizeAutomaticLocals(kRootSlotCount);
    auto pin = [&](unsigned slot, const proto::ProtoObject* v) {
        ctx->setAutomaticLocal(slot, v);
        return const_cast<proto::ProtoObject*>(v);
    };
    RuntimeLayout& L = layout_;
    L.globals  = pin(kGlobals, space.objectPrototype->newChild(ctx, /*isMutable=*/true));
    L.anyProto = pin(kAny, space.objectPrototype->newChild(ctx, true));
    auto child = [&](unsigned slot) { return pin(slot, L.anyProto->newChild(ctx, true)); };
    L.intProto      = child(kInt);
    L.doubleProto   = child(kDouble);
    L.booleanProto  = child(kBoolean);
    L.charProto     = child(kChar);
    L.stringProto   = child(kString);
    L.listProto     = child(kList);
    L.unitProto     = child(kUnitProto);
    L.functionProto = child(kFunction);
    L.cellProto     = child(kCell);
    L.lazyProto     = child(kLazy);
    L.unit = pin(kUnit, L.unitProto->newChild(ctx));
    // Function0..Function22 and FunctionXXL, pinned together in one list.
    const proto::ProtoObject* arities[kMaxFunctionArity + 2];
    for (unsigned n = 0; n < kMaxFunctionArity + 2; ++n)
        arities[n] = L.functionProto->newChild(ctx, true);
    const proto::ProtoObject* list = ctx->newList(kMaxFunctionArity + 2, arities)->asObject(ctx);
    ctx->setAutomaticLocal(kFunctionArities, list);
    for (unsigned n = 0; n < kMaxFunctionArity + 2; ++n)
        L.functionArity[n] = const_cast<proto::ProtoObject*>(arities[n]);

    L.codeKey     = proto::ProtoString::createSymbol(ctx, "__code__");
    L.capturesKey = proto::ProtoString::createSymbol(ctx, "__captures__");
    L.valueKey    = proto::ProtoString::createSymbol(ctx, "__value__");
    L.thunkKey    = proto::ProtoString::createSymbol(ctx, "__thunk__");
    L.applyName   = proto::ProtoString::createSymbol(ctx, "apply");
    // Phase 3: the operator names ExecutionEngine sends. `unary_-` and `unary_!`
    // are seven bytes, so interning them on every execution also leaked (R4).
    L.unaryMinusName = proto::ProtoString::createSymbol(ctx, "unary_-");
    L.unaryNotName   = proto::ProtoString::createSymbol(ctx, "unary_!");
    {
        static const char* const ops[7] = {"+", "-", "*", "<", "<=", ">", ">="};
        for (unsigned k = 0; k < 7; ++k)
            L.binaryOpName[k] = proto::ProtoString::createSymbol(ctx, ops[k]);
    }

    // Phase 2: the class-model prototypes. Tuple2..Tuple22 are pinned together
    // in one list, as the function arities are.
    L.anyRefProto       = pin(kAnyRef, L.anyProto->newChild(ctx, true));
    L.productProto      = pin(kProduct, L.anyRefProto->newChild(ctx, true));
    L.serializableProto = pin(kSerializable, L.anyRefProto->newChild(ctx, true));
    L.withFilterProto   = pin(kWithFilter, L.anyRefProto->newChild(ctx, true));
    L.listCompanion     = pin(kListCompanion, L.anyRefProto->newChild(ctx, true));
    const proto::ProtoObject* tuples[kMaxTupleArity + 1];
    const proto::ProtoObject* companions[kMaxTupleArity + 1];
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        tuples[n] = L.productProto->newChild(ctx, true);
        companions[n] = L.anyRefProto->newChild(ctx, true);  // the TupleN companion object
    }
    ctx->setAutomaticLocal(kTuples, ctx->newList(kMaxTupleArity - 1, tuples + 2)->asObject(ctx));
    ctx->setAutomaticLocal(kTupleCompanions,
                           ctx->newList(kMaxTupleArity - 1, companions + 2)->asObject(ctx));
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        L.tupleProto[n] = const_cast<proto::ProtoObject*>(tuples[n]);
        L.tupleCompanion[n] = const_cast<proto::ProtoObject*>(companions[n]);
    }

    auto key = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };
    L.nameKey = key("__name__");
    L.prefixKey = key("__prefix__");
    L.fieldsKey = key("__fields__");
    L.tupleKey = key("__tuple__");
    L.mutableKey = key("__mutable__");
    L.selfKey = key("__self__");
    L.listKey = key("__list__");
    L.predsKey = key("__preds__");
    L.initKey = key(kPrimaryCtorKey);
    L.toStringName = key("toString");
    L.equalsName = key("equals");
    L.hashCodeName = key("hashCode");
    L.canEqualName = key("canEqual");
    L.isEmptyName = key("isEmpty");
    L.getName = key("get");
    for (unsigned k = 1; k <= kMaxTupleArity; ++k)
        L.tupleFieldKey[k] = key(("_" + std::to_string(k)).c_str());

    // The built-in types: bound in the globals under their type keys (MAKE_CLASS
    // pushes them as parents) and marked on themselves (TEST_PROTO, Design note 5).
    // Everything below AnyRef has a __name__ (a class name, Task 6's
    // isScalaInstance); Any does not, so Cells, lazy holders, function objects
    // and the () singleton - children of Any - never pass for instances.
    auto bindType = [&](proto::ProtoObject* proto, const std::string& typeKey, const char* name) {
        const auto* k = key(typeKey.c_str());
        L.globals->setAttribute(ctx, k, proto);   // mutable: in place
        proto->setAttribute(ctx, k, PROTO_TRUE);
        if (name) proto->setAttribute(ctx, L.nameKey, makeString(ctx, name));
    };
    bindType(L.anyProto, kAnyKey, nullptr);
    bindType(L.anyRefProto, kAnyRefKey, "AnyRef");
    bindType(L.productProto, kProductKey, "Product");
    bindType(L.serializableProto, kSerializableKey, "Serializable");
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        const std::string name = "Tuple" + std::to_string(n);
        proto::ProtoObject* t = L.tupleProto[n];
        bindType(t, tupleTypeKey(n), name.c_str());
        t->setAttribute(ctx, L.prefixKey, makeString(ctx, name));
        t->setAttribute(ctx, L.tupleKey, PROTO_TRUE);
        const proto::ProtoObject* keys[kMaxTupleArity];
        for (unsigned k = 1; k <= n; ++k) keys[k - 1] = L.tupleFieldKey[k]->asObject(ctx);
        const proto::ProtoObject* fields = ctx->newList(n, keys)->asObject(ctx);
        t->setAttribute(ctx, L.fieldsKey, fields);
        // The companion carries the same element keys: its native `apply`
        // reads its arity from them (ProductPrimitives.cpp).
        proto::ProtoObject* companion = L.tupleCompanion[n];
        companion->setAttribute(ctx, L.nameKey, makeString(ctx, name));
        companion->setAttribute(ctx, L.fieldsKey, fields);
    }
    L.withFilterProto->setAttribute(ctx, L.nameKey, makeString(ctx, "WithFilter"));
    L.listCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "List"));

    // Phase 5: the concurrency prototypes (DESIGN §8). The registry is mutable
    // and anchors every actor for the session (D46), so the GC reaches an actor,
    // its mailboxes and every queued message from a root slot -- the ready
    // stacks are C++ and invisible to it.
    L.actorProto        = pin(kActorProto, L.anyRefProto->newChild(ctx, true));
    L.futureProto       = pin(kFutureProto, L.anyRefProto->newChild(ctx, true));
    L.envelopeProto     = pin(kEnvelopeProto, L.anyProto->newChild(ctx, true));
    L.threadProto       = pin(kThreadProto, L.anyRefProto->newChild(ctx, true));
    L.frameProto        = pin(kFrameProto, L.anyProto->newChild(ctx, true));
    L.spawnPartialProto = pin(kSpawnPartialProto, L.anyProto->newChild(ctx, true));
    L.actorRegistry     = pin(kActorRegistry, L.anyProto->newChild(ctx, true));
    L.actorCompanion    = pin(kActorCompanion, L.anyRefProto->newChild(ctx, true));
    L.priorityCompanion = pin(kPriorityCompanion, L.anyRefProto->newChild(ctx, true));
    L.futureCompanion   = pin(kFutureCompanion, L.anyRefProto->newChild(ctx, true));
    L.threadCompanion   = pin(kThreadCompanion, L.anyRefProto->newChild(ctx, true));
    L.systemCompanion   = pin(kSystemCompanion, L.anyRefProto->newChild(ctx, true));
    L.handlerKey    = key("__handler__");
    L.actorStateKey = key("__astate__");
    L.stateRefKey   = key("__sched_ref__");
    for (unsigned b = 0; b < 3; ++b) {
        L.mailboxKey[b] = key(("__mbox" + std::to_string(b) + "__").c_str());
        L.pendingKey[b] = key(("__pend" + std::to_string(b) + "__").c_str());
    }
    L.actorsKey     = key("__actors__");
    L.threadsKey    = key("__threads__");
    L.msgKey        = key("__msg__");
    L.futureKey     = key("__future__");
    L.snapshotKey   = key("__snapshot__");
    L.waitingOnKey  = key("__waiting_on__");
    L.turnFutureKey = key("__turn_future__");
    L.fstateKey     = key("__fstate__");
    L.fvalueKey     = key("__fvalue__");
    L.ferrorKey     = key("__ferror__");
    L.waitersKey    = key("__waiters__");
    L.contsKey      = key("__conts__");
    L.modKey        = key("__mod__");
    L.ipKey         = key("__ip__");
    L.fbaseKey      = key("__fbase__");
    L.fslotsKey     = key("__fslots__");
    L.threadRefKey  = key("__thread__");
    L.bodyKey       = key("__body__");
    L.tuple2Key     = key(tupleTypeKey(2).c_str());
    // Phase 3: the collection prototypes and their companions (DESIGN §6,
    // plan A0-4). Each payload lives in one attribute of an ordinary object.
    L.rangeProto       = pin(kRangeProto, L.anyRefProto->newChild(ctx, true));
    L.vectorProto      = pin(kVectorProto, L.anyRefProto->newChild(ctx, true));
    L.mapProto         = pin(kMapProto, L.anyRefProto->newChild(ctx, true));
    L.setProto         = pin(kSetProto, L.anyRefProto->newChild(ctx, true));
    L.foldPartialProto = pin(kFoldPartialProto, L.anyProto->newChild(ctx, true));
    L.vectorCompanion  = pin(kVectorCompanion, L.anyRefProto->newChild(ctx, true));
    L.mapCompanion     = pin(kMapCompanion, L.anyRefProto->newChild(ctx, true));
    L.setCompanion     = pin(kSetCompanion, L.anyRefProto->newChild(ctx, true));
    L.rangeStartKey     = key("__start__");
    L.rangeEndKey       = key("__end__");
    L.rangeStepKey      = key("__step__");
    L.rangeInclusiveKey = key("__inclusive__");
    // `__list__` is NOT reused: it is already the WithFilter's source key.
    L.vecDataKey  = key("__vec__");
    L.mapDataKey  = key("__map__");
    L.foldSrcKey  = key("__fold_src__");
    L.foldSeedKey = key("__fold_seed__");
    L.foldLeftKey = key("__fold_left__");

    // Phase 4: exceptions and enums (DESIGN §7, §4.5). `@Throwable` is the
    // per-class marker the THROW opcode tests, exactly as `case p: Point` tests
    // `@Point` — a Phase 2 marker attribute, never protoCore's isInstanceOf (R3).
    L.throwableKey = key(kThrowableKey);
    L.ordinalKey   = key("__ordinal__");
    L.enumNameKey  = key("__enumname__");
    L.enumProto    = pin(kEnumProto, L.anyRefProto->newChild(ctx, true));

    L.actorRegistry->setAttribute(ctx, L.actorsKey, ctx->newList()->asObject(ctx));
    L.actorRegistry->setAttribute(ctx, L.threadsKey, ctx->newList()->asObject(ctx));
    bindType(L.actorProto, kActorKey, "Actor");
    bindType(L.futureProto, kFutureKey, "Future");
    L.threadProto->setAttribute(ctx, L.nameKey, makeString(ctx, "Thread"));
    L.actorCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Actor"));
    L.priorityCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Priority"));
    L.futureCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Future"));
    L.threadCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Thread"));
    L.systemCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "System"));
    bindType(L.rangeProto, kRangeKey, "Range");
    bindType(L.vectorProto, kVectorKey, "Vector");
    bindType(L.mapProto, kMapKey, "Map");
    bindType(L.setProto, kSetKey, "Set");
    L.vectorCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Vector"));
    L.mapCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Map"));
    L.setCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "Set"));
    bindType(L.enumProto, kEnumKey, "Enum");

    // Rebind the primitive prototypes (see the header comment).
    space.smallIntegerPrototype = L.intProto;
    space.largeIntegerPrototype = L.intProto;
    space.doublePrototype       = L.doubleProto;
    space.floatPrototype        = L.doubleProto;
    space.booleanPrototype      = L.booleanProto;
    space.unicodeCharPrototype  = L.charProto;
    space.stringPrototype       = L.stringProto;
    space.listPrototype         = L.listProto;
    space.methodPrototype       = L.functionProto;  // native functions are Scala functions
}

proto::ProtoContext* Runtime::rootContext() const { return space_.rootContext; }

} // namespace protoScala
