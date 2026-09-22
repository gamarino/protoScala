#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {

CompiledUnit compile(const std::string& src, GlobalTable& g, UnitMode mode = UnitMode::Script) {
    auto unit = parseSource(src);
    desugar(*unit);
    Compiler c(g);
    return c.compileUnit(*unit, mode, 0);
}

std::string listing(const std::string& src, UnitMode mode = UnitMode::Script) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    return compile(src, g, mode).module->disassemble();
}

std::string compileError(const std::string& src) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    try {
        compile(src, g);
    } catch (const CompileError& e) {
        return e.what();
    }
    return "";
}

bool has(const std::string& text, const std::string& piece) {
    return text.find(piece) != std::string::npos;
}

} // namespace

TEST(Compiler, ArithmeticUsesFastPaths) {
    const auto l = listing("val x = 1 + 2 * 3");
    EXPECT_TRUE(has(l, "MUL"));
    EXPECT_TRUE(has(l, "ADD"));
    EXPECT_TRUE(has(l, "STORE_GLOBAL"));
    EXPECT_FALSE(has(l, "SEND"));
}

TEST(Compiler, OtherOperatorsAndMethodsAreSends) {
    const auto l = listing("val x = 7 / 2\nval n = \"abc\".length\nval m = 3.max(4)");
    EXPECT_TRUE(has(l, "; //1"));
    EXPECT_TRUE(has(l, "; length/0"));
    EXPECT_TRUE(has(l, "; max/1"));
}

TEST(Compiler, MainIsRecordedAndDefsAreHoisted) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    auto cu = compile("@main def m(): Unit = helper()\ndef helper() = println(\"hi\")", g);
    EXPECT_EQ(cu.mainName, "m");
    EXPECT_FALSE(cu.mainTakesArgs);
    const auto l = cu.module->disassemble();
    // Both MAKE_FN come before anything else in the top-level code.
    EXPECT_LT(l.find("MAKE_FN"), l.find("PUSH_UNIT"));
    EXPECT_TRUE(has(l, "CALL 1"));
    EXPECT_EQ(g.find("helper"), BindingKind::Def);
}

TEST(Compiler, MainWithStringVarargs) {
    GlobalTable g;
    auto cu = compile("@main def run(args: String*): Unit = ()", g);
    EXPECT_TRUE(cu.mainTakesArgs);
}

TEST(Compiler, MainRestrictions) {
    EXPECT_TRUE(has(compileError("@main def a() = 1\n@main def b() = 2"), "only one @main"));
    EXPECT_TRUE(has(compileError("@main def a(x: Int) = x"), "@main methods take"));
}

TEST(Compiler, CapturedVarIsBoxed) {
    const auto l = listing("def f() = { var c = 0; () => { c += 1; c } }");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
    EXPECT_TRUE(has(l, "STORE_CELL"));
    EXPECT_TRUE(has(l, "PUSH_CELL"));
}

TEST(Compiler, CapturedValIsCopied) {
    const auto l = listing("def f(x: Int) = { val y = x; () => y }");
    EXPECT_FALSE(has(l, "MAKE_CELL"));
    EXPECT_TRUE(has(l, "captures=1"));
}

TEST(Compiler, LocalDefsAreBoxedAndHoisted) {
    const auto l = listing(
        "def f(n: Int): Boolean = {\n"
        "  def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)\n"
        "  def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)\n"
        "  even(n)\n"
        "}");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
}

TEST(Compiler, ParamlessDefReferenceIsACall) {
    const auto l = listing("def pi = 3.14\nval x = pi");
    EXPECT_TRUE(has(l, "CALL 0"));
}

TEST(Compiler, LazyValUsesMakeLazyAndForce) {
    const auto l = listing("lazy val z = 1 + 1\nval w = z");
    EXPECT_TRUE(has(l, "MAKE_LAZY"));
    EXPECT_TRUE(has(l, "FORCE"));
}

TEST(Compiler, ShortCircuitBooleans) {
    const auto l = listing("val a = true\nval b = a && false || a");
    EXPECT_TRUE(has(l, "JUMP_IF_FALSE"));
    EXPECT_TRUE(has(l, "JUMP_IF_TRUE"));
    EXPECT_FALSE(has(l, "; &&/1"));
}

TEST(Compiler, WhileLoopsJumpBack) {
    const auto l = listing("var i = 0\nwhile i < 3 do i += 1");
    EXPECT_TRUE(has(l, "JUMP_BACK"));
}

TEST(Compiler, SpliceUsesCallSpread) {
    const auto l = listing("def f(xs: Int*) = xs\ndef g(ys: Int*) = f(ys*)");
    EXPECT_TRUE(has(l, "CALL_SPREAD 0"));
    EXPECT_TRUE(has(l, "arity=1 variadic"));
}

TEST(Compiler, MaxStackCoversTheDeepestCall) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    auto cu = compile("def f(a: Int, b: Int, c: Int) = a\nval x = f(1, 2, 3)", g);
    EXPECT_GE(cu.module->maxStack(), 4);
}

TEST(Compiler, ReplModeBindsResult) {
    GlobalTable g;
    auto cu = compile("val a = 1\na + 1", g, UnitMode::Repl);
    EXPECT_EQ(cu.resultName, "res0");
    ASSERT_EQ(cu.definitions.size(), 1u);
    EXPECT_EQ(cu.definitions[0], "val a");
    EXPECT_EQ(g.find("res0"), BindingKind::Val);
}

TEST(Compiler, SemanticErrors) {
    EXPECT_TRUE(has(compileError("val a = 1\na = 2"), "Reassignment to val a"));
    EXPECT_TRUE(has(compileError("val b = zz"), "Not found: zz"));
    EXPECT_TRUE(has(compileError("val f = () => return 1"), "return inside a lambda"));
    EXPECT_TRUE(has(compileError("return 1"), "return outside"));
    EXPECT_TRUE(has(compileError("val t = (1, 2)"), "tuples are not implemented yet"));
    EXPECT_TRUE(has(compileError("val s = s\"x\""), "string interpolation is not implemented yet"));
    EXPECT_TRUE(has(compileError("def f(x: Int) = x\nval y = f(x = 1)"), "named arguments"));
    EXPECT_TRUE(has(compileError("def f(x: Int = 1) = x"), "default parameter values"));
    EXPECT_TRUE(has(compileError("def f(x: => Int) = x"), "by-name parameters"));
}

// --- Additional coverage (Task 7 implementation) ----------------------------

TEST(Compiler, CapturesChainThroughIntermediateFunctions) {
    const auto l = listing("def f(x: Int) = () => () => x");
    // f captures nothing; both lambdas capture x (the outer one only to pass it on).
    EXPECT_TRUE(has(l, "function f arity=1 locals=0 stack=1 captures=0"));
    EXPECT_TRUE(has(l, "function <lambda> arity=0 locals=1 stack=1 captures=1"));
    EXPECT_TRUE(has(l, "function <lambda> arity=0 locals=1 stack=1 captures=1"));
    EXPECT_FALSE(has(l, "MAKE_CELL"));
}

TEST(Compiler, LocalValReadByLocalDefIsBoxed) {
    // The def is hoisted above the val's initialiser, so it must share a Cell.
    const auto l = listing("def f() = {\n  def g() = y\n  val y = 1\n  g()\n}");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
    EXPECT_TRUE(has(l, "STORE_CELL"));
}

TEST(Compiler, ReturnIsAllowedInDefBodies) {
    const auto l = listing("def f(x: Int): Int = { if x < 0 then return 0 else (); x }");
    EXPECT_TRUE(has(l, "RETURN"));
}

TEST(Compiler, ReturnInsideLambdaInsideDefIsRejected) {
    EXPECT_TRUE(has(compileError("def f() = () => return 1"), "return inside a lambda"));
}

TEST(Compiler, AssignmentTargetsAndVarWrites) {
    EXPECT_TRUE(has(compileError("val o = 1\no.x = 2"),
                    "assignment to fields and indexed elements is not implemented yet"));
    EXPECT_TRUE(has(compileError("def f(x: Int) = { x = 1 }"), "Reassignment to val x"));
    const auto l = listing("def f() = { var i = 0; i = i + 1; i }");
    EXPECT_TRUE(has(l, "STORE_LOCAL"));
    EXPECT_FALSE(has(l, "MAKE_CELL"));
}

TEST(Compiler, SpliceOutsideAFunctionCallIsRejected) {
    EXPECT_TRUE(has(compileError("def f(xs: Int*) = xs.foo(xs*)"),
                    "splices are only supported in function calls"));
}

TEST(Compiler, UnaryOperatorsUseOpcodes) {
    const auto l = listing("val a = 1\nval b = -a\nval c = !true");
    EXPECT_TRUE(has(l, "NEG"));
    EXPECT_TRUE(has(l, "NOT"));
}

TEST(Compiler, FailedCompilationLeavesGlobalsUntouched) {
    GlobalTable g;
    try {
        compile("val a = 1\nval b = zz", g);
        FAIL() << "expected a CompileError";
    } catch (const CompileError&) {
    }
    EXPECT_FALSE(g.find("a").has_value());
    EXPECT_FALSE(g.find("b").has_value());
}

TEST(Compiler, StackDepthIsBalancedAcrossBranches) {
    GlobalTable g;
    // Each branch leaves exactly one value; the if itself needs at most 2 slots.
    auto cu = compile("def f(a: Int) = if a < 1 then 1 else a * 2", g);
    const auto& fn = cu.module->block(0);
    EXPECT_EQ(fn.maxStack(), 2);
    EXPECT_EQ(cu.module->maxStack(), 1);
}
