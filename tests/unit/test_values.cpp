#include "runtime/Futures.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace protoScala;

TEST(Values, FormatDoubleMatchesJava) {
    EXPECT_EQ(formatDouble(1.0), "1.0");
    EXPECT_EQ(formatDouble(100.0), "100.0");
    EXPECT_EQ(formatDouble(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(formatDouble(1.0 / 3), "0.3333333333333333");
    EXPECT_EQ(formatDouble(1e7), "1.0E7");
    EXPECT_EQ(formatDouble(1e21), "1.0E21");
    EXPECT_EQ(formatDouble(1e-5), "1.0E-5");
    EXPECT_EQ(formatDouble(0.001), "0.001");
    EXPECT_EQ(formatDouble(-0.0), "-0.0");
    EXPECT_EQ(formatDouble(std::numeric_limits<double>::infinity()), "Infinity");
    EXPECT_EQ(formatDouble(-std::numeric_limits<double>::infinity()), "-Infinity");
    EXPECT_EQ(formatDouble(std::nan("")), "NaN");
}

TEST(Values, ShowAndEquality) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    const RuntimeLayout& L = rt.layout();
    EXPECT_EQ(show(&ctx, L, ctx.fromInteger(42)), "42");
    EXPECT_EQ(show(&ctx, L, ctx.fromString("123456789012345678901234567890", 10)),
              "123456789012345678901234567890");
    EXPECT_EQ(show(&ctx, L, ctx.fromDouble(2.5)), "2.5");
    EXPECT_EQ(show(&ctx, L, PROTO_TRUE), "true");
    EXPECT_EQ(show(&ctx, L, PROTO_NONE), "null");
    EXPECT_EQ(show(&ctx, L, L.unit), "()");
    EXPECT_EQ(show(&ctx, L, ctx.fromUnicodeChar(U'x')), "x");
    EXPECT_EQ(show(&ctx, L, ctx.fromUTF8String("hi")), "hi");
    const proto::ProtoObject* items[] = {ctx.fromInteger(1), ctx.fromInteger(2)};
    EXPECT_EQ(show(&ctx, L, ctx.newList(2, items)->asObject(&ctx)), "List(1, 2)");

    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromInteger(1), ctx.fromDouble(1.0)));
    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromUTF8String("ab"), ctx.fromUTF8String("ab")));
    EXPECT_TRUE(valuesEqual(&ctx, L, ctx.fromUnicodeChar(U'a'), ctx.fromInteger(97)));
    EXPECT_TRUE(valuesEqual(&ctx, L, PROTO_NONE, PROTO_NONE));
    const proto::ProtoObject* nan = ctx.fromDouble(std::nan(""));
    EXPECT_FALSE(valuesEqual(&ctx, L, nan, nan));
    EXPECT_FALSE(valuesEqual(&ctx, L, ctx.fromUTF8String("1"), ctx.fromInteger(1)));

    EXPECT_EQ(typeName(&ctx, L, ctx.fromInteger(1)), "Int");
    EXPECT_EQ(typeName(&ctx, L, ctx.fromDouble(1)), "Double");
    EXPECT_EQ(typeName(&ctx, L, ctx.fromUTF8String("s")), "String");
    EXPECT_EQ(typeName(&ctx, L, PROTO_NONE), "Null");
}

// Outside a run there is no ActiveCallContext, so an instance cannot answer
// through its own toString/equals/hashCode: every one of those falls back to
// the AnyRef behaviour (Session.cpp echoes results in exactly this shape).
TEST(Values, InstancesWithoutAnActiveEngineFallBackToIdentity) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    const RuntimeLayout& L = rt.layout();
    // A class shape as MAKE_CLASS builds it, and two of its instances.
    const proto::ProtoObject* cls = L.anyRefProto->newChild(&ctx, false)
                                        ->setAttribute(&ctx, L.nameKey, makeString(&ctx, "Box"));
    const proto::ProtoObject* a = cls->newChild(&ctx, false);
    const proto::ProtoObject* b = cls->newChild(&ctx, false);
    EXPECT_TRUE(isScalaInstance(&ctx, L, a));
    EXPECT_FALSE(isScalaInstance(&ctx, L, L.cellProto->newChild(&ctx, true)));  // a boxed var
    EXPECT_FALSE(isScalaInstance(&ctx, L, ctx.fromInteger(1)));

    EXPECT_EQ(typeName(&ctx, L, a), "Box");
    EXPECT_EQ(show(&ctx, L, a), defaultToString(&ctx, L, a));
    EXPECT_EQ(show(&ctx, L, a).rfind("Box@", 0), 0u);
    EXPECT_TRUE(valuesEqual(&ctx, L, a, a));   // identity
    EXPECT_FALSE(valuesEqual(&ctx, L, a, b));
    EXPECT_EQ(scalaHash(&ctx, L, a), identityHash(&ctx, a));
    EXPECT_EQ(identityHash(&ctx, a), identityHash(&ctx, a));  // stable
}

TEST(Values, PrimitivePrototypesAreRebound) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    EXPECT_EQ(ctx.fromInteger(1)->getPrototype(&ctx), rt.layout().intProto);
    EXPECT_EQ(ctx.fromUTF8String("abcdefghij")->getPrototype(&ctx), rt.layout().stringProto);
    EXPECT_EQ(ctx.fromDouble(1.5)->getPrototype(&ctx), rt.layout().doubleProto);
    EXPECT_EQ(PROTO_TRUE->getPrototype(&ctx), rt.layout().booleanProto);
}

// The value-kind tests of Values.h are built from protoCore's public API only.
// protoCore aliases several kinds' prototypes to objectPrototype (doubles,
// LargeIntegers, methods, byte buffers), so no kind is decided by getPrototype
// alone when its prototype is an alias, and a value inheriting from a kind
// prototype is not of that kind.
namespace {
const proto::ProtoObject* nopMethod(proto::ProtoContext*, const proto::ProtoObject*,
                                    const proto::ParentLink*, const proto::ProtoArguments*,
                                    const proto::ProtoKeywordArguments*) {
    return PROTO_NONE;
}
}  // namespace

TEST(Values, KindTestsUsePublicApiExactly) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    const proto::ProtoObject* parentless = ctx.newObject(false);
    const proto::ProtoObject* child = parentless->newChild(&ctx, false);
    const proto::ProtoObject* dbl = ctx.fromDouble(2.5);
    const proto::ProtoObject* big = ctx.fromString("123456789012345678901234567890", 10);
    const proto::ProtoObject* method = ctx.fromMethod(const_cast<proto::ProtoObject*>(parentless), nopMethod);
    const proto::ProtoObject* buffer = ctx.newBuffer(8);
    const proto::ProtoObject* list = ctx.newList()->asObject(&ctx);
    const proto::ProtoObject* chr = ctx.fromUnicodeChar(U'x');

    // No value but a bare value of the kind answers a kind test.
    for (const proto::ProtoObject* v : {parentless, child, static_cast<const proto::ProtoObject*>(space.objectPrototype), dbl, big,
                                        method, buffer, ctx.fromInteger(7),
                                        ctx.fromUTF8String("s"), PROTO_NONE, PROTO_TRUE}) {
        EXPECT_FALSE(isListFast(&ctx, v));
        EXPECT_FALSE(isCharFast(&ctx, v));
    }
    EXPECT_FALSE(isDoubleFast(&ctx, parentless));
    EXPECT_FALSE(isLargeIntFast(&ctx, parentless));

    EXPECT_TRUE(isDoubleFast(&ctx, dbl));
    EXPECT_TRUE(isLargeIntFast(&ctx, big));
    EXPECT_FALSE(isLargeIntFast(&ctx, ctx.fromInteger(7)));
    EXPECT_TRUE(isListFast(&ctx, list));
    EXPECT_FALSE(isListFast(&ctx, ctx.fromUTF8String("ab")));
    EXPECT_FALSE(isListFast(&ctx, space.listPrototype->newChild(&ctx, false)));
    EXPECT_TRUE(isCharFast(&ctx, chr));
    EXPECT_EQ(charValueFast(&ctx, chr), U'x');
    EXPECT_FALSE(isCharFast(&ctx, ctx.fromInteger('x')));
    EXPECT_EQ(widenChar(&ctx, chr), ctx.fromInteger('x'));
}

// Language questions are decided by the value's class chain, never by its
// protoCore representation: a Scala instance has a class name on its chain; a
// foreign object is a value of no Scala class (not under Any, no function, not
// null), whatever protoCore kind holds it; a future is a value carrying the
// future state. Each is pinned across value kinds.
TEST(Values, TypeQuestionsAcrossValueKinds) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    const RuntimeLayout& L = rt.layout();
    const proto::ProtoObject* cls = L.anyRefProto->newChild(&ctx, false)
                                        ->setAttribute(&ctx, L.nameKey, makeString(&ctx, "Box"));
    const proto::ProtoObject* instance = cls->newChild(&ctx, false);
    const proto::ProtoObject* items[] = {ctx.fromInteger(1), ctx.fromInteger(2)};
    const proto::ProtoObject* list = ctx.newList(2, items)->asObject(&ctx);
    const proto::ProtoObject* future = futures::create(&ctx, L);

    // Scala values: no foreign object among them.
    const proto::ProtoObject* scalaValues[] = {
        ctx.fromInteger(7),
        ctx.fromString("123456789012345678901234567890", 10),
        ctx.fromDouble(2.5),
        ctx.fromUnicodeChar(U'x'),
        ctx.fromUTF8String("s"),
        ctx.fromUTF8String("a string longer than the inline form"),
        list,
        PROTO_TRUE, PROTO_FALSE, L.unit,
        ctx.fromMethod(nullptr, nopMethod),
        L.cellProto->newChild(&ctx, true),
        instance, future,
    };
    for (const proto::ProtoObject* v : scalaValues) {
        EXPECT_FALSE(isForeignObject(&ctx, L, v)) << typeName(&ctx, L, v);
        EXPECT_EQ(isScalaInstance(&ctx, L, v), v == instance || v == future)
            << typeName(&ctx, L, v);
        EXPECT_EQ(futures::isFuture(&ctx, L, v), v == future) << typeName(&ctx, L, v);
        EXPECT_EQ(seqViewOf(&ctx, L, v).valid, v == list) << typeName(&ctx, L, v);
    }
    EXPECT_FALSE(isForeignObject(&ctx, L, PROTO_NONE));
    EXPECT_FALSE(isScalaInstance(&ctx, L, PROTO_NONE));
    EXPECT_FALSE(futures::isFuture(&ctx, L, PROTO_NONE));
    EXPECT_FALSE(seqViewOf(&ctx, L, PROTO_NONE).valid);

    // Values no Scala class built: objects, parentless or parented, and a
    // module value protoCore does not hold as an object (a byte buffer).
    const proto::ProtoObject* parentless = ctx.newObject(false);
    const proto::ProtoObject* parented = space.objectPrototype->newChild(&ctx, false);
    const proto::ProtoObject* buffer = ctx.newBuffer(8);
    for (const proto::ProtoObject* v : {parentless, parented, parented->newChild(&ctx, false), buffer}) {
        EXPECT_TRUE(isForeignObject(&ctx, L, v));
        EXPECT_FALSE(isScalaInstance(&ctx, L, v));
        EXPECT_FALSE(futures::isFuture(&ctx, L, v));
        EXPECT_FALSE(seqViewOf(&ctx, L, v).valid);
        EXPECT_TRUE(valuesEqual(&ctx, L, v, v));
        EXPECT_FALSE(valuesEqual(&ctx, L, v, parentless == v ? parented : parentless));
    }
    // An object inheriting from the List class is no List.
    EXPECT_FALSE(seqViewOf(&ctx, L, L.listProto->newChild(&ctx, false)).valid);
}
