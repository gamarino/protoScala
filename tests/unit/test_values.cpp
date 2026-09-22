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

TEST(Values, PrimitivePrototypesAreRebound) {
    proto::ProtoSpace space;
    Runtime rt(space);
    proto::ProtoContext ctx(&space, rt.rootContext());
    EXPECT_EQ(ctx.fromInteger(1)->getPrototype(&ctx), rt.layout().intProto);
    EXPECT_EQ(ctx.fromUTF8String("abcdefghij")->getPrototype(&ctx), rt.layout().stringProto);
    EXPECT_EQ(ctx.fromDouble(1.5)->getPrototype(&ctx), rt.layout().doubleProto);
    EXPECT_EQ(PROTO_TRUE->getPrototype(&ctx), rt.layout().booleanProto);
}
