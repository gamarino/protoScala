#include "EvalHarness.h"
#include "runtime/StackGuard.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;

TEST(Engine, ArithmeticAndPromotion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 + 2 * 3"), "7");
    EXPECT_EQ(h.eval("9007199254740991L + 1"), "9007199254740992");
    EXPECT_EQ(h.eval("-9007199254740992L - 1"), "-9007199254740993");
    EXPECT_EQ(h.eval("100000000000L * 100000000000L"), "10000000000000000000000");
    EXPECT_EQ(h.eval("-(-9007199254740992L)"), "9007199254740992");
    EXPECT_EQ(h.eval("1.5 + 1"), "2.5");
}

TEST(Engine, ComparisonsAndEquality) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1 < 2"), "true");
    EXPECT_EQ(h.eval("2 <= 1"), "false");
    EXPECT_EQ(h.eval("1 == 1.0"), "true");
    EXPECT_EQ(h.eval("\"ab\" == \"a\" + \"b\""), "true");
    EXPECT_EQ(h.eval("1 != 2"), "true");
    EXPECT_EQ(h.eval("\"abc\" < \"abd\""), "true");
    EXPECT_EQ(h.eval("!true"), "false");
}

TEST(Engine, StringAndCharArithmetic) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"a\" + 1 + 2"), "a12");
    EXPECT_EQ(h.eval("1 + 2 + \"a\""), "3a");
    EXPECT_EQ(h.eval("'a' + 1"), "98");
    EXPECT_EQ(h.eval("\"x\" + true + null"), "xtruenull");
}

TEST(Engine, ControlFlow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 < 2 then \"yes\" else \"no\""), "yes");
    EXPECT_EQ(h.eval("if false then 1"), "()");
    EXPECT_EQ(h.eval("{ var i = 0; var s = 0; while i < 10 do { s += i; i += 1 }; s }"), "45");
    EXPECT_EQ(h.eval("true && false || true"), "true");
}

TEST(Engine, FunctionsClosuresAndRecursion) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ def make(n: Int) = (x: Int) => x + n; make(10)(5) }"), "15");
    EXPECT_EQ(h.eval("{ var c = 0; val inc = () => { c += 1; c }; inc(); inc() }"), "2");
    EXPECT_EQ(h.eval("{ def fib(n: Int): Int = if n < 2 then n else fib(n - 1) + fib(n - 2); fib(20) }"),
              "6765");
    EXPECT_EQ(h.eval("{ def even(n: Int): Boolean = if n == 0 then true else odd(n - 1)\n"
                     "  def odd(n: Int): Boolean = if n == 0 then false else even(n - 1)\n"
                     "  even(100) }"),
              "true");
    EXPECT_EQ(h.eval("def add(a: Int)(b: Int) = a + b"), "");
    EXPECT_EQ(h.eval("add(3)(4)"), "7");
}

TEST(Engine, LoopClosuresCaptureTheirOwnValAndShareOuterVars) {
    // Each iteration's `val k` gets a fresh cell (MAKE_CELL per block entry),
    // while `shared`, declared outside the loop, is one cell for every closure.
    EvalHarness h;
    EXPECT_EQ(h.eval("{ var shared = 0; var i = 0\n"
                     "  var f0: () => Int = null; var f1: () => Int = null\n"
                     "  while i < 2 do { val k = i * 10; val g = () => k + shared\n"
                     "    if i == 0 then f0 = g else f1 = g\n"
                     "    i += 1 }\n"
                     "  shared = 5\n"
                     "  f0() + f1() * 100 }"),
              "1505");
}

TEST(Engine, AllocatingLoopsSurviveCollections) {
    // Every iteration allocates strings, a closure and a large integer. A
    // low heap ceiling forces collections; the loop's back-edge safepoints
    // let the collector run while the frame's slots keep the live values.
    EvalHarness h;
    h.space().setHeapLimits(0, 20000);
    const auto cyclesBefore = h.space().getGCCycleCount();
    EXPECT_EQ(h.eval("{ var i = 0; var s = \"\"; var big = 9007199254740991L; var n = 0\n"
                     "  while i < 20000 do {\n"
                     "    val t = \"x\" + i; val f = (y: Int) => { s = t; y + 1 }\n"
                     "    n = f(n); big = big + 1; i += 1 }\n"
                     "  s + \" \" + n + \" \" + big }"),
              "x19999 20000 9007199254760991");
    EXPECT_GT(h.space().getGCCycleCount(), cyclesBefore);
}

TEST(Engine, GlobalsPersistAcrossUnits) {
    EvalHarness h;
    EXPECT_EQ(h.eval("var total = 1"), "");
    EXPECT_EQ(h.eval("total = total + 41"), "()");
    EXPECT_EQ(h.eval("total"), "42");
    EXPECT_EQ(h.eval("res1"), "()");
}

TEST(Engine, LazyValsAndParamlessDefs) {
    EvalHarness h;
    EXPECT_EQ(h.eval("{ var n = 0; lazy val x = { n += 1; 42 }; x + x + n }"), "85");
    EXPECT_EQ(h.eval("{ var n = 0; def tick = { n += 1; n }; tick; tick; tick }"), "3");
}

TEST(Engine, VarargsAndSpread) {
    EvalHarness h;
    EXPECT_EQ(h.eval("def all(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("all(1, 2, 3)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("all()"), "List()");
    EXPECT_EQ(h.eval("{ def fwd(ys: Int*) = all(ys*); fwd(4, 5) }"), "List(4, 5)");
}

TEST(Engine, RuntimeErrors) {
    EvalHarness h;
    EXPECT_EQ(h.eval("if 1 then 2 else 3").rfind("error: ClassCastException", 0), 0u);
    EXPECT_EQ(h.eval("{ val f: Int => Int = null; f(1) }").rfind("error: NullPointerException", 0), 0u);
    EXPECT_EQ(h.eval("1.noSuchMethod").rfind("error: NoSuchMethodError", 0), 0u);
    EXPECT_EQ(h.eval("{ val f = (x: Int) => x; f(1, 2) }")
                  .rfind("error: IllegalArgumentException: wrong number of arguments", 0), 0u);
}

TEST(Engine, StackOverflowIsAnErrorAndTheSessionSurvives) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        const std::string r = h.eval("{ def f(n: Int): Int = f(n + 1) + 1; f(0) }");
        if (r.rfind("error: StackOverflowError", 0) != 0) return 1;
        return h.eval("1 + 1") == "2" ? 0 : 2;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}

TEST(Engine, DeepButBoundedRecursionWorks) {
    const int rc = protoScala::runOnEvaluatorThread([](void*) {
        EvalHarness h;
        return h.eval("{ def sum(n: Int): Int = if n == 0 then 0 else n + sum(n - 1); sum(10000) }") ==
                       "50005000" ? 0 : 1;
    }, nullptr);
    EXPECT_EQ(rc, 0);
}
