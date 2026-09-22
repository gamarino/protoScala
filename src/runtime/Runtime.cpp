#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <string>

namespace protoScala {

namespace {
enum RootSlot : unsigned {
    kGlobals, kAny, kInt, kDouble, kBoolean, kChar, kString, kList, kUnitProto,
    kFunction, kFunctionArities, kCell, kLazy, kUnit,
    kAnyRef, kProduct, kSerializable, kWithFilter, kListCompanion, kTuples,
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

    // Phase 2: the class-model prototypes. Tuple2..Tuple22 are pinned together
    // in one list, as the function arities are.
    L.anyRefProto       = pin(kAnyRef, L.anyProto->newChild(ctx, true));
    L.productProto      = pin(kProduct, L.anyRefProto->newChild(ctx, true));
    L.serializableProto = pin(kSerializable, L.anyRefProto->newChild(ctx, true));
    L.withFilterProto   = pin(kWithFilter, L.anyRefProto->newChild(ctx, true));
    L.listCompanion     = pin(kListCompanion, L.anyRefProto->newChild(ctx, true));
    const proto::ProtoObject* tuples[kMaxTupleArity + 1];
    for (unsigned n = 2; n <= kMaxTupleArity; ++n)
        tuples[n] = L.productProto->newChild(ctx, true);
    ctx->setAutomaticLocal(kTuples, ctx->newList(kMaxTupleArity - 1, tuples + 2)->asObject(ctx));
    for (unsigned n = 2; n <= kMaxTupleArity; ++n)
        L.tupleProto[n] = const_cast<proto::ProtoObject*>(tuples[n]);

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
        t->setAttribute(ctx, L.fieldsKey, ctx->newList(n, keys)->asObject(ctx));
    }
    L.withFilterProto->setAttribute(ctx, L.nameKey, makeString(ctx, "WithFilter"));
    L.listCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "List"));

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
