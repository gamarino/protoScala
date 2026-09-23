// Facts about protoCore's object model that protoScala's classes rely on
// (Phase 2 plan, Design notes 1, 2 and 5). The first three exercise protoCore
// only; the last one checks the built-in types the Runtime binds on top of it.
// If one of them fails after a protoCore change, revisit the class model
// (MAKE_CLASS in ExecutionEngine.cpp) before anything else.
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <string>

namespace {
const proto::ProtoString* sym(proto::ProtoContext* c, const char* s) {
    return proto::ProtoString::createSymbol(c, s);
}
long long intAttr(proto::ProtoContext* c, const proto::ProtoObject* o, const char* name) {
    return proto::asSmallInt(o->getAttribute(c, sym(c, name)));
}
} // namespace

TEST(ObjectModel, InstancesOfAnImmutableShapeWalkItsWholeChain) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    const proto::ProtoObject* root = space.objectPrototype->newChild(c, false);
    const proto::ProtoObject* b = root->newChild(c)
                                      ->setAttribute(c, sym(c, "who"), c->fromInteger(1))
                                      ->setAttribute(c, sym(c, "onlyB"), c->fromInteger(10));
    const proto::ProtoObject* t1 = root->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(2));
    const proto::ProtoObject* t2 = root->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(3));
    // class C extends B with T1 with T2: its chain is L(C) without C = [T2, T1, B].
    const proto::ProtoObject* items[] = {t2, t1, b};
    const proto::ProtoObject* cls = root->newChild(c, false)
                                        ->setParents(c, c->newList(3, items))
                                        ->setAttribute(c, sym(c, "own"), c->fromInteger(4))
                                        ->setAttribute(c, sym(c, "@C"), PROTO_TRUE);
    const proto::ProtoObject* inst = cls->newChild(c, false)->setAttribute(c, sym(c, "field"), c->fromInteger(5));
    EXPECT_EQ(intAttr(c, inst, "field"), 5);   // own attribute (a field)
    EXPECT_EQ(intAttr(c, inst, "own"), 4);     // the class
    EXPECT_EQ(intAttr(c, inst, "who"), 3);     // T2 comes first in the linearization
    EXPECT_EQ(intAttr(c, inst, "onlyB"), 10);  // the last parent is reached
    EXPECT_EQ(inst->getAttribute(c, sym(c, "@C")), PROTO_TRUE);  // the membership marker
    EXPECT_EQ(b->newChild(c)->getAttribute(c, sym(c, "@C")), PROTO_NONE);
    const proto::ProtoList* parents = inst->getParents(c);
    // protoCore 2.0.0 flattens in setParents: the listed parents keep their order and
    // every missing ancestor is appended after them, so the chain is
    // [C, T2, T1, B, root, objectPrototype] rather than the four entries the list named.
    // The linearization protoScala installs is unchanged, and so is lookup order.
    ASSERT_EQ(parents->getSize(c), 6u);
    EXPECT_EQ(parents->getAt(c, 0), cls);
    EXPECT_EQ(parents->getAt(c, 1), t2);
    EXPECT_EQ(parents->getAt(c, 2), t1);
    EXPECT_EQ(parents->getAt(c, 3), b);
    EXPECT_EQ(parents->getAt(c, 4), root);
    EXPECT_EQ(inst->getPrototype(c), cls);
    // A mutable instance of the same class walks the same chain and keeps its identity.
    const proto::ProtoObject* m = cls->newChild(c, true);
    EXPECT_EQ(m->setAttribute(c, sym(c, "field"), c->fromInteger(6)), m);
    EXPECT_EQ(intAttr(c, m, "field"), 6);
    EXPECT_EQ(intAttr(c, m, "who"), 3);
    EXPECT_EQ(m->getPrototype(c), cls);
    // An immutable instance changes identity on every field write (Design note 10).
    EXPECT_NE(inst->setAttribute(c, sym(c, "field"), c->fromInteger(7)), inst);
}

TEST(ObjectModel, ChildrenOfAMutableObjectCaptureItsChainAtCreationTime) {
    // protoCore 2.0.0: newChild reads a mutable prototype's CURRENT snapshot, so a
    // child created AFTER a setParents sees the new chain (under 1.x it saw none of
    // it). A child created BEFORE keeps the chain it captured. Class prototypes stay
    // immutable shapes (Design note 2) because that makes the capture point moot.
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    const proto::ProtoObject* t = space.objectPrototype->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(1));
    const proto::ProtoObject* items[] = {t};
    const proto::ProtoObject* mutableClass = space.objectPrototype->newChild(c, true);
    const proto::ProtoObject* before = mutableClass->newChild(c, false);
    mutableClass->setParents(c, c->newList(1, items));
    EXPECT_EQ(intAttr(c, mutableClass, "who"), 1);  // the class itself sees the new chain
    const proto::ProtoObject* after = mutableClass->newChild(c, false);
    EXPECT_EQ(intAttr(c, after, "who"), 1);                         // created after: sees it
    EXPECT_EQ(before->getAttribute(c, sym(c, "who")), PROTO_NONE);  // created before: does not
}

TEST(ObjectModel, DeepChainsResolveThroughTheMarker) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    ctx.resizeAutomaticLocals(1);
    // L0 <- L1 <- ... <- L30, each an immutable shape whose chain is its full linearization.
    const proto::ProtoObject* chain = c->newList()->asObject(c);
    const proto::ProtoObject* cls = nullptr;
    for (int k = 0; k <= 30; ++k) {
        const std::string marker = "@L" + std::to_string(k);
        cls = space.objectPrototype->newChild(c, false)
                  ->setParents(c, chain->asList(c))
                  ->setAttribute(c, sym(c, marker.c_str()), PROTO_TRUE);
        chain = chain->asList(c)->insertAt(c, 0, cls)->asObject(c);
        ctx.setAutomaticLocal(0, chain);
    }
    const proto::ProtoObject* inst = cls->newChild(c, false);
    for (int k = 0; k <= 30; ++k) {
        const std::string marker = "@L" + std::to_string(k);
        EXPECT_EQ(inst->getAttribute(c, sym(c, marker.c_str())), PROTO_TRUE) << marker;
    }
}

// The runtime roots the class model is built on (Phase 2 plan, Step 5).
TEST(ObjectModel, RuntimeBindsTheBuiltInTypes) {
    proto::ProtoSpace space;
    protoScala::Runtime rt(space);
    const protoScala::RuntimeLayout& L = rt.layout();
    proto::ProtoContext ctx(&space, rt.rootContext());
    proto::ProtoContext* c = &ctx;

    EXPECT_EQ(L.anyRefProto->getPrototype(c), L.anyProto);
    EXPECT_EQ(L.productProto->getPrototype(c), L.anyRefProto);
    EXPECT_EQ(L.tupleProto[2]->getPrototype(c), L.productProto);
    EXPECT_EQ(L.tupleProto[22]->getPrototype(c), L.productProto);

    // The globals bind every built-in type under its type key, so MAKE_CLASS
    // can push them as parents.
    EXPECT_EQ(L.globals->getAttribute(c, sym(c, protoScala::kAnyKey)), L.anyProto);
    EXPECT_EQ(L.globals->getAttribute(c, sym(c, protoScala::kAnyRefKey)), L.anyRefProto);
    EXPECT_EQ(L.globals->getAttribute(c, sym(c, protoScala::kProductKey)), L.productProto);
    EXPECT_EQ(L.globals->getAttribute(c, sym(c, protoScala::kSerializableKey)),
              L.serializableProto);
    EXPECT_EQ(L.globals->getAttribute(c, sym(c, "@Tuple3")), L.tupleProto[3]);

    // Each carries its own marker, so an instance answers the whole chain.
    const proto::ProtoObject* t3 = L.tupleProto[3]->newChild(c, false);
    EXPECT_EQ(t3->getAttribute(c, sym(c, "@Tuple3")), PROTO_TRUE);
    EXPECT_EQ(t3->getAttribute(c, sym(c, protoScala::kProductKey)), PROTO_TRUE);
    EXPECT_EQ(t3->getAttribute(c, sym(c, protoScala::kAnyRefKey)), PROTO_TRUE);
    EXPECT_EQ(t3->getAttribute(c, sym(c, protoScala::kAnyKey)), PROTO_TRUE);
    EXPECT_EQ(t3->getAttribute(c, sym(c, "@Tuple2")), PROTO_NONE);

    // Names and product metadata.
    EXPECT_EQ(protoScala::show(c, L, L.tupleProto[3]->getAttribute(c, L.nameKey)), "Tuple3");
    EXPECT_EQ(protoScala::show(c, L, L.tupleProto[3]->getAttribute(c, L.prefixKey)), "Tuple3");
    EXPECT_EQ(L.tupleProto[3]->getAttribute(c, L.tupleKey), PROTO_TRUE);
    const proto::ProtoList* fields = L.tupleProto[3]->getAttribute(c, L.fieldsKey)->asList(c);
    ASSERT_EQ(fields->getSize(c), 3u);
    EXPECT_EQ(fields->getAt(c, 0), L.tupleFieldKey[1]->asObject(c));
    EXPECT_EQ(fields->getAt(c, 2), L.tupleFieldKey[3]->asObject(c));
    EXPECT_EQ(L.tupleFieldKey[22], sym(c, "_22"));
    EXPECT_EQ(L.initKey, sym(c, "<init>"));

    // Any has no __name__: children of Any that are not Scala instances
    // (cells, lazy holders, functions, the () singleton) never pass for one.
    EXPECT_EQ(L.anyProto->getAttribute(c, L.nameKey), PROTO_NONE);
    EXPECT_EQ(protoScala::show(c, L, L.anyRefProto->getAttribute(c, L.nameKey)), "AnyRef");
    EXPECT_EQ(protoScala::show(c, L, L.withFilterProto->getAttribute(c, L.nameKey)), "WithFilter");
    EXPECT_EQ(protoScala::show(c, L, L.listCompanion->getAttribute(c, L.nameKey)), "List");
}
