#include "runtime/Runtime.h"
#include "protoCore.h"

namespace protoScala {

namespace {
enum RootSlot : unsigned {
    kGlobals, kAny, kInt, kDouble, kBoolean, kChar, kString, kList, kUnitProto,
    kFunction, kFunctionArities, kCell, kLazy, kUnit, kRootSlotCount
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
