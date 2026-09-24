#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Primitives.h"

#include <gtest/gtest.h>

using namespace protoScala;

namespace {

CompiledUnit compile(const std::string& src, GlobalTable& g, UnitMode mode = UnitMode::Script) {
    auto unit = parseSource(src);
    desugar(*unit);
    Compiler c(g);
    return c.compileUnit(*unit, mode, 0);
}

// The globals and types a real session starts with (Session.cpp): `println`
// alone is not enough for `List`, `Nil` and the TupleN companions.
GlobalTable sessionGlobals() {
    GlobalTable g;
    for (const auto& n : builtinGlobalNames()) g.declare(n, BindingKind::Builtin);
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
    return g;
}

std::string listing(const std::string& src, UnitMode mode = UnitMode::Script) {
    GlobalTable g = sessionGlobals();
    return compile(src, g, mode).module->disassemble();
}

std::string compileError(const std::string& src) {
    GlobalTable g = sessionGlobals();
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

TEST(Compiler, QualifiedMainAnnotationCounts) {
    GlobalTable g;
    EXPECT_EQ(compile("@scala.main def go(): Unit = ()", g).mainName, "go");
    GlobalTable h;
    EXPECT_EQ(compile("@deprecated def notMain(): Unit = ()", h).mainName, "");
}

TEST(Compiler, MainRestrictions) {
    EXPECT_TRUE(has(compileError("@main def a() = 1\n@main def b() = 2"), "only one @main"));
    EXPECT_TRUE(has(compileError("@main def a(x: Int) = x"), "typed @main parameters"));
    EXPECT_TRUE(has(compileError("@main def a(xs: Int*) = xs"), "@main methods take"));
    EXPECT_TRUE(has(compileError("@main def a(a: String*)(b: Int) = b"), "@main methods take"));
}

TEST(Compiler, ReturnInsideACurriedDefBody) {
    EXPECT_EQ(compileError("def f(a: Int)(b: Int): Int = { if a > b then return a; b }"), "");
    EXPECT_TRUE(has(compileError("def f(a: Int)(b: Int): Int = { val g = () => return a; b }"),
                    "return inside a lambda"));
}

TEST(Compiler, UnitResultDiscardsTheBodyValue) {
    const auto l = listing("def u(): Unit = 5");
    const auto fn = l.substr(l.find("function u"));
    EXPECT_TRUE(has(fn, "POP"));
    EXPECT_TRUE(has(fn, "PUSH_UNIT"));
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
    EXPECT_EQ(cu.definitions[0].text, "val a");
    EXPECT_EQ(cu.definitions[0].key, "a");
    EXPECT_EQ(g.find("res0"), BindingKind::Val);
}

TEST(Compiler, SemanticErrors) {
    EXPECT_TRUE(has(compileError("val a = 1\na = 2"), "Reassignment to val a"));
    EXPECT_TRUE(has(compileError("val b = zz"), "Not found: zz"));
    EXPECT_TRUE(has(compileError("val f = () => return 1"), "return inside a lambda"));
    EXPECT_TRUE(has(compileError("return 1"), "return outside"));
    EXPECT_TRUE(has(listing("val t = (1, 2)"), "MAKE_TUPLE 2"));
    // Phase 3 implements `s`, `raw` and `f`; an unknown interpolator is rejected
    // while desugaring (D56, CompilerInterpolation), and a bad f specifier is a
    // compile error at the interpolation's position (A0-2, D55).
    EXPECT_TRUE(has(compileError("val n = 1\nval s = f\"$n%q\""),
                    "unsupported format specifier '%q'"));
    EXPECT_TRUE(has(compileError("val n = 1\nval s = f\"50% of $n\""),
                    "conversions must follow a splice"));
    EXPECT_TRUE(has(compileError("def f(x: Int) = x\nval y = f(x = 1)"), "named arguments"));
    EXPECT_TRUE(has(compileError("def f(x: Int = 1) = x"), "default parameter values"));
    // A by-name parameter is thunked at a call site that names its declaration
    // and forced on every read in the body (D47).
    EXPECT_TRUE(has(listing("def f(x: => Int) = x\nval y = f(1)"), "function <by-name>"));
    EXPECT_TRUE(has(listing("def f(x: => Int) = x\nval y = f(1)"), "FORCE_THUNK"));
    // A call the compiler cannot resolve evaluates its argument (D53).
    EXPECT_FALSE(has(listing("class C:\n  def f(x: => Int) = x\nval y = new C().f(1)"),
                     "function <by-name>"));
    EXPECT_TRUE(has(compileError("val g = (x: => Int) => x"), "not supported on a function literal"));
    EXPECT_TRUE(has(compileError("case class W(v: => Int)"), "may not be by-name"));
    EXPECT_TRUE(has(compileError("def f(xs: => Int*) = 1"), "repeated parameter cannot be by-name"));
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
    const auto l = listing("def f() = {\n  val y = 1\n  def g() = y\n  g()\n}");
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
    EXPECT_TRUE(has(listing("val o = 1\no.x = 2"), "; x_=/1"));  // o.x_=(2)
    EXPECT_TRUE(has(compileError("def f(x: Int) = { x = 1 }"), "Reassignment to val x"));
    const auto l = listing("def f() = { var i = 0; i = i + 1; i }");
    EXPECT_TRUE(has(l, "STORE_LOCAL"));
    EXPECT_FALSE(has(l, "MAKE_CELL"));
}

TEST(CompilerPatterns, CascadeShape) {
    const auto l = listing("case class P(x: Int)\ndef f(v: Any) = v match { case P(1) => 1; case s: String => 2; case _ => 3 }");
    EXPECT_TRUE(has(l, "TEST_PROTO"));
    EXPECT_TRUE(has(l, "UNAPPLY_FIELDS"));
    EXPECT_TRUE(has(l, "TEST_TYPE 4 ; String"));
    EXPECT_TRUE(has(l, "MATCH_ERROR"));
    EXPECT_TRUE(has(listing("def f(xs: List[Int]) = xs match { case h :: t => h; case Nil => 0 }"), "UNCONS"));
}

TEST(CompilerPatterns, Errors) {
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case x: Int | x: String => 1 }"),
                    "Illegal variable x in pattern alternative"));
    EXPECT_TRUE(has(compileError("case class P(x: Int)\ndef f(v: Any) = v match { case P(a, b) => 1 }"),
                    "wrong number of arguments for pattern P"));
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case q: Nope => 1 }"), "Not found: type Nope"));
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case Zork(a) => 1 }"), "Not found: Zork"));
}

TEST(CompilerTemplates, ClassesCompileToMakeClassNewAndMethods) {
    const auto l = listing("class P(val x: Int) { def twice = x * 2 }\nval p = new P(3)");
    EXPECT_TRUE(has(l, "class P @P parents=2 members=[twice,<init>]"));
    EXPECT_TRUE(has(l, "; @P"));
    EXPECT_TRUE(has(l, "NEW"));
    EXPECT_TRUE(has(l, "function twice arity=1 method"));
    EXPECT_TRUE(has(l, "function P.<init> arity=2 method"));
    EXPECT_TRUE(has(l, "STORE_FIELD"));
    const auto t = listing("trait T\nclass C extends T\nobject O extends C");
    EXPECT_TRUE(has(t, "class C @C parents=3"));        // [T, AnyRef, Any]
    EXPECT_TRUE(has(t, "class O @O.type parents=4"));   // [C, T, AnyRef, Any]
    EXPECT_TRUE(has(t, "MAKE_LAZY"));                   // the singleton holder
}

// `c.f()` (an explicit argument list) applies a function-valued member;
// `c.f` selects it. The two send sites must differ (SEND_APPLY vs SEND).
TEST(CompilerTemplates, ExplicitApplicationHasItsOwnSendSite) {
    const auto l = listing("class H(val f: () => Int)\nval h = new H(() => 1)\n"
                           "val a = h.f()\nval b = h.f");
    EXPECT_TRUE(has(l, "SEND_APPLY"));   // h.f()
    EXPECT_TRUE(has(l, "  SEND "));      // h.f  (two spaces: not SEND_APPLY)
    EXPECT_TRUE(has(l, "; f/0"));
}

// D5: a private member is addressed by its class-qualified key, with the plain
// name as the site's fallback, so a foreign receiver still finds its own member.
TEST(CompilerTemplates, PrivateMemberSendCarriesAPlainFallback) {
    const auto l = listing("class C(val value: Int)\n"
                           "class B(private val value: Int) { def r(c: C) = c.value }");
    EXPECT_TRUE(has(l, "B::value/0 or value"));
}

// A named-argument send on a private member carries the same fallback.
TEST(CompilerTemplates, PrivateKeywordSendCarriesAPlainFallback) {
    const auto l = listing("class B { private def m(a: Int) = a\n def f(o: B) = o.m(a = 1) }");
    EXPECT_TRUE(has(l, "B::m/0(a=) or m"));
}

// `def p = e` is paramless: the module records it so `obj.p()` can be rejected.
TEST(CompilerTemplates, ParamlessMethodsAreMarked) {
    GlobalTable g;
    g.declare("println", BindingKind::Builtin);
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
    const auto cu = compile("class H { def p = 1\n def q() = 2 }", g);
    bool sawParamless = false, sawEmptyParens = false;
    for (std::size_t k = 0; k < cu.module->blockCount(); ++k) {
        const BytecodeModule& b = cu.module->block(k);
        if (b.name() == "p") sawParamless = b.isParamless();
        if (b.name() == "q") sawEmptyParens = !b.isParamless();
    }
    EXPECT_TRUE(sawParamless);
    EXPECT_TRUE(sawEmptyParens);
}

TEST(CompilerTemplates, StaticErrors) {
    EXPECT_TRUE(has(compileError("trait T\nval t = new T"), "T is a trait; it cannot be instantiated"));
    EXPECT_TRUE(has(compileError("abstract class A\nval a = new A"), "A is abstract; it cannot be instantiated"));
    EXPECT_TRUE(has(compileError("trait S { def area: Double }\nclass C extends S"),
                    "class C needs to be abstract, since def area is not defined"));
    EXPECT_TRUE(has(compileError("final class F\nclass G extends F"), "cannot extend final class F"));
    EXPECT_TRUE(has(compileError("class A\nclass B\nclass C extends A with B"), "class B is not a trait"));
    EXPECT_TRUE(has(compileError("class A\nclass B\ntrait T extends A\nclass C extends B with T"),
                    "illegal trait inheritance: superclass B does not derive from "
                    "trait T's superclass A"));
    // A trait's superclass may come from the traits alone (dotty ensureFirstIsClass).
    EXPECT_EQ(compileError("class A\ntrait U\ntrait T extends A\nclass C extends U with T"), "");
    EXPECT_TRUE(has(compileError("class A(x: Int) { def this(y: Int, z: Int) = this(y, z) }"),
                    "must call a preceding constructor, not itself"));
    EXPECT_TRUE(has(compileError("trait S { def area: Double }\nclass C extends S "
                                 "{ private def area = 1.0 }"),
                    "weaker access privileges"));
    EXPECT_TRUE(has(compileError("class B(val a: Int, val xs: Int*)\ndef f(ys: Int*) = "
                                 "new B(1, 2, ys*)"),
                    "before the splice, expected 1"));
    EXPECT_TRUE(has(compileError("class A extends B\nclass B extends A"), "cyclic inheritance"));
    EXPECT_TRUE(has(compileError("case class A(x: Int)\ncase class B(y: Int) extends A(y)"),
                    "case-to-case inheritance is prohibited"));
    EXPECT_TRUE(has(compileError("class A(val x: Int) { def f = { x = 1 } }"), "Reassignment to val x"));
    EXPECT_TRUE(has(compileError("class A { def f = 1; def f = 2 }"), "f is already defined in A"));
    EXPECT_TRUE(has(compileError("class A(x: Int) { def this(y: Int) = this(y) }"),
                    "differ in their number of parameters"));
    EXPECT_TRUE(has(compileError("class A(x: Int) { def this() = println(1) }"),
                    "must begin with a call to another constructor"));
    EXPECT_TRUE(has(compileError("def f = { class Local; 1 }"), "must be defined at the top level"));
    EXPECT_TRUE(has(compileError("val x = this"), "this can be used only inside"));
    EXPECT_TRUE(has(compileError("class A(x: Int)\nval a = new A(1, 2)"),
                    "wrong number of arguments for the constructor of A"));
    EXPECT_TRUE(has(compileError("class A extends Nope"), "Not found: type Nope"));
    EXPECT_TRUE(has(compileError("object O { @main def m() = 1 }"), "@main methods must be top-level"));
    EXPECT_TRUE(has(compileError("trait G(val g: Int)\ntrait H extends G\nclass C extends H"),
                    "parameterized trait G"));
}

// The override rules, each checked against scalac 3.9.0.
TEST(CompilerTemplates, OverrideRules) {
    // Implementing an abstract member needs no `override`, whatever its kind.
    EXPECT_EQ(compileError("trait T { var v: Int }\nclass B extends T { var v = 5 }"), "");
    EXPECT_EQ(compileError("abstract class A { var v: Int }\nclass B extends A { var v = 5 }"), "");
    EXPECT_EQ(compileError("trait T { var v: Int }\nclass B(var v: Int) extends T"), "");
    EXPECT_EQ(compileError("trait T { def d: Int }\nclass B extends T { val d = 5 }"), "");
    EXPECT_EQ(compileError("trait T { def d: Int }\nclass B extends T { var d = 5 }"), "");
    // Redefining a concrete member needs `override`; val over def is allowed.
    EXPECT_EQ(compileError("class A { def d = 1 }\nclass B extends A { override val d = 2 }"), "");
    EXPECT_TRUE(has(compileError("class A { def d = 1 }\nclass B extends A { val d = 2 }"),
                    "needs an `override` modifier"));
    // A var never overrides a var, with or without the modifier.
    EXPECT_TRUE(has(compileError("class A { var v = 1 }\nclass B extends A { override var v = 2 }"),
                    "cannot override a mutable variable"));
    EXPECT_TRUE(has(compileError("class A { var v = 1 }\nclass B extends A { var v = 2 }"),
                    "cannot override a mutable variable"));
    EXPECT_TRUE(has(compileError("trait T { var v: Int }\nclass B extends T { override var v = 5 }"),
                    "cannot override a mutable variable"));
    // Only a stable member may take the place of a val or a var.
    EXPECT_TRUE(has(compileError("class A { val v = 1 }\nclass B extends A { override def v = 2 }"),
                    "needs to be a stable, immutable value"));
    EXPECT_TRUE(has(compileError("class A { val v = 1 }\nclass B extends A { override var v = 2 }"),
                    "needs to be a stable, immutable value"));
    EXPECT_TRUE(has(compileError("trait T { val v: Int }\nclass B extends T { def v = 5 }"),
                    "needs to be a stable, immutable value"));
    // An abstract var declares an abstract setter, which a val cannot supply.
    EXPECT_TRUE(has(compileError("trait T { var v: Int }\nclass B extends T { val v = 5 }"),
                    "needs to be abstract, since def v_= is not defined"));
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

// --- Fix round 1 -------------------------------------------------------------

// Scala 3 block rule (SLS 6.11, dotty ForwardDepChecks): a reference from
// statement i to a definition at j >= i of the same block is illegal when a
// strict val or var lies in [i, j].
TEST(Compiler, ForwardReferenceFromLambdaIsRejected) {
    EXPECT_TRUE(has(compileError("def f() = { val g = () => y; val y = 1; g() }"),
                    "forward reference to value y extends over the definition of value g"));
}

TEST(Compiler, ForwardReferenceFromLazyThunkIsRejected) {
    EXPECT_TRUE(has(compileError("def f() = { lazy val a = b; val b = 1; a }"),
                    "forward reference to value b extends over the definition of value b"));
}

TEST(Compiler, DirectForwardReferencesAreRejected) {
    EXPECT_TRUE(has(compileError("def f() = { val a = b; val b = 1; a }"),
                    "forward reference to value b extends over the definition of value a"));
    EXPECT_TRUE(has(compileError("def f() = { c = 1; var c = 0; c }"),
                    "forward reference to variable c extends over the definition of variable c"));
    EXPECT_TRUE(has(compileError("def f() = { val a = z; lazy val z = 1; a }"),
                    "forward reference to lazy value z extends over the definition of value a"));
    EXPECT_TRUE(has(compileError("def f() = { val a = { val q = 1; b }; val b = 1; a }"),
                    "forward reference to value b extends over the definition of value a"));
}

TEST(Compiler, ForwardReferencesThroughDefsExtendOverStrictVals) {
    EXPECT_TRUE(has(compileError("def m() = { def f = y; val x = f; val y = 1; x }"),
                    "forward reference to value y extends over the definition of value x"));
    EXPECT_TRUE(has(compileError("def m() = { def g() = z + 1; val r = g(); val z = 1; r }"),
                    "forward reference to value z extends over the definition of value r"));
    EXPECT_TRUE(has(compileError("def m() = { def f = y; val y = 1; f }"),
                    "forward reference to value y extends over the definition of value y"));
    EXPECT_TRUE(has(compileError("def f() = {\n  def inc() = c += 1\n  var c = 0\n  inc()\n  c\n}"),
                    "forward reference to variable c"));
    EXPECT_TRUE(has(compileError("def m() = { def f = g; val x = 1; def g = 2; f }"),
                    "forward reference to method g extends over the definition of value x"));
}

TEST(Compiler, LegalForwardReferences) {
    // Mutual recursion between local defs; lazy vals with no strict val between.
    EXPECT_EQ(compileError("def m() = { def a(n: Int): Int = b(n); def b(n: Int): Int = a(n); a(1) }"), "");
    EXPECT_EQ(compileError("def m() = { def f = z; lazy val z = 1; f }"), "");
    EXPECT_EQ(compileError("def m() = { lazy val a = z; lazy val z = 1; a }"), "");
    EXPECT_EQ(compileError("def m() = { println(z); lazy val z = 1; z }"), "");
    EXPECT_EQ(compileError("def m() = { val x = 1; def f = x; val y = f; y }"), "");
}

TEST(Compiler, LazyValsAreHoistedLikeDefs) {
    // The thunk is created before the statements run, so a strict val it reads
    // lives in a Cell (like a val read by a hoisted def).
    const auto l = listing("def f() = { val x = 1; lazy val z = x + 1; z }");
    EXPECT_TRUE(has(l, "MAKE_CELL"));
    const auto body = l.substr(l.find("function f"));
    EXPECT_LT(body.find("MAKE_LAZY"), body.find("PUSH_CONST"));  // before x = 1 runs
}

TEST(Compiler, DuplicateNamesInOneBlockAreRejected) {
    EXPECT_TRUE(has(compileError("def f() = { val x = 1; val x = 2; x }"), "x is already defined"));
    EXPECT_TRUE(has(compileError("def f() = { def g() = 1; val g = 2; g }"), "g is already defined"));
    EXPECT_EQ(compileError("def f() = { val x = 1; { val x = 2; x } }"), "");
}

TEST(Compiler, VarInsideWhileBodyGetsAFreshCellPerIteration) {
    GlobalTable g;
    auto cu = compile("def f() = {\n  var i = 0\n  while i < 3 do {\n    var j = i\n"
                      "    val h = () => j\n    i += 1\n  }\n}", g);
    const std::string l = cu.module->block(0).disassemble();
    const auto body = l.substr(0, l.find("function <lambda>"));
    const auto test = body.find("JUMP_IF_FALSE");
    const auto cell = body.find("MAKE_CELL");
    const auto back = body.find("JUMP_BACK");
    ASSERT_NE(cell, std::string::npos);
    EXPECT_LT(test, cell);
    EXPECT_LT(cell, back);
    EXPECT_EQ(body.find("MAKE_CELL", cell + 1), std::string::npos);  // i is not boxed
}

TEST(Compiler, MaxStackOfAWhileLoopIsExact) {
    GlobalTable g;
    auto cu = compile("def f() = { var i = 0; while i < 3 do i += 1; i }", g);
    EXPECT_EQ(cu.module->block(0).maxStack(), 2);
}

TEST(Compiler, MaxStackOfNestedShortCircuitsIsExact) {
    GlobalTable g;
    // x stays on the stack under the condition: 1 + (x < y) = 3 at the deepest point.
    auto cu = compile("def f(x: Int, y: Int) = x + (if x < y && (y < x || x == y) then 1 else 2)", g);
    EXPECT_EQ(cu.module->block(0).maxStack(), 3);
}

TEST(Compiler, ReplRedefinitionGetsAFreshGlobalKey) {
    GlobalTable g;
    compile("val x = 1\ndef f = x", g, UnitMode::Repl);
    const auto cu = compile("val x = 2", g, UnitMode::Repl);
    ASSERT_EQ(cu.definitions.size(), 1u);
    EXPECT_EQ(cu.definitions[0].key, "x#1");
    EXPECT_EQ(g.binding("x")->key, "x#1");
    EXPECT_EQ(g.binding("f")->key, "f");
    // Within one unit a repeated definition keeps the unit's key (D25).
    const auto cu2 = compile("val y = 1\nval y = 2", g, UnitMode::Repl);
    ASSERT_EQ(cu2.definitions.size(), 2u);
    EXPECT_EQ(cu2.definitions[1].key, "y");
    EXPECT_EQ(GlobalTable::nameOfKey("x#1"), "x");
    EXPECT_EQ(GlobalTable::nameOfKey("##"), "##");
    EXPECT_EQ(GlobalTable::nameOfKey("##12"), "#");
}

TEST(GlobalTableTypes, KeysShadowAcrossUnitsAndStayFindable) {
    GlobalTable g;
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
    g.beginUnit();
    const std::string k1 = g.declareType("Point");
    EXPECT_EQ(k1, "@Point");
    ClassInfo p;
    p.name = "Point";
    p.key = k1;
    g.defineType(p);
    g.beginUnit();
    const std::string k2 = g.declareType("Point");
    EXPECT_EQ(k2, "@Point#1");
    ClassInfo p2 = p;
    p2.key = k2;
    g.defineType(p2);
    EXPECT_EQ(g.findType("Point")->key, "@Point#1");
    ASSERT_NE(g.findTypeByKey("@Point"), nullptr);
    EXPECT_EQ(g.findType("Tuple3")->fields.size(), 3u);
    EXPECT_EQ(g.findType("Product")->kind, ClassKind::Trait);
    const GlobalTable copy = g;  // a REPL trial copy shares the counters
    GlobalTable g2 = copy;
    g2.beginUnit();
    EXPECT_EQ(g2.declareType("Point"), "@Point#2");
}

// --- Phase 3: string interpolation lowers to CONCAT (A0-1, D54) ----------------

TEST(CompilerInterpolation, EmitsOneConcatAndNoStringContextSend) {
    const std::string asm_ = listing(R"(val a = 1
val s = s"x${a}y")");
    EXPECT_TRUE(has(asm_, "CONCAT 3")) << asm_;
    EXPECT_FALSE(has(asm_, "StringContext")) << asm_;
}

TEST(CompilerInterpolation, AdjacentHolesEmitNoEmptyLiterals) {
    const std::string asm_ = listing(R"(val a = 1
val b = 2
val s = s"$a$b")");
    EXPECT_TRUE(has(asm_, "CONCAT 2")) << asm_;
}

TEST(CompilerInterpolation, ALoneHoleStillGoesThroughConcatToConvert) {
    // s"$a" must convert, so it pushes an empty literal and joins two pieces.
    const std::string asm_ = listing(R"(val a = 1
val s = s"$a")");
    EXPECT_TRUE(has(asm_, "CONCAT 2")) << asm_;
}

TEST(CompilerInterpolation, AnUnknownInterpolatorIsRejectedWhileDesugaring) {
    // The desugarer raises a ParseError, not a CompileError (D56).
    EXPECT_THROW(listing(R"(val s = json"$x")"), ParseError);
}

// --- Exceptions (Phase 4, DESIGN §7) ---------------------------------------

// These listings use `case e =>` and a bare `throw e` rather than a named
// exception class: this fixture compiles against the builtin type table only,
// without the prelude, so `RuntimeException` does not exist here.
TEST(Bytecode, TryEmitsAHandlerEntryCoveringOnlyTheTryBody) {
    const std::string asm_ = listing("def f(): Int = try 1 catch case e => 2");
    EXPECT_TRUE(has(asm_, "handler [")) << asm_;
    EXPECT_TRUE(has(asm_, "catch")) << asm_;
    EXPECT_TRUE(has(asm_, "RETHROW")) << asm_;
}

TEST(Bytecode, ThrowEmitsTheThrowOpcode) {
    const std::string asm_ = listing("def f(e: Any): Int = throw e");
    EXPECT_TRUE(has(asm_, "THROW")) << asm_;
}

TEST(Bytecode, FinallyEmitsTwoCopiesAndAFinallyEntry) {
    const std::string asm_ = listing("def f(): Int = try 1 finally 2");
    EXPECT_TRUE(has(asm_, "finally")) << asm_;   // the handler-table line
    EXPECT_TRUE(has(asm_, "RETHROW")) << asm_;
    // The cleanup appears twice: once as the handler body, once inline on the
    // normal path, so `try 1 finally 2` pushes three constants in all.
    std::size_t count = 0, at = 0;
    while ((at = asm_.find("PUSH_CONST", at)) != std::string::npos) { ++count; ++at; }
    EXPECT_GE(count, 3u) << asm_;
}

TEST(Bytecode, ReturnInsideTryFinallyEmitsTheCleanupBeforeTheReturn) {
    const std::string whole = listing("def f(): Int = try { return 7 } finally { 9 }");
    // Search inside f's own listing: the enclosing <top> module has a RETURN of
    // its own, which would come first.
    const std::size_t body = whole.find("function f ");
    ASSERT_NE(body, std::string::npos) << whole;
    const std::string asm_ = whole.substr(body);
    const std::size_t nine = asm_.find("; 9");
    const std::size_t ret = asm_.find("RETURN");
    ASSERT_NE(nine, std::string::npos) << asm_;
    ASSERT_NE(ret, std::string::npos) << asm_;
    EXPECT_LT(nine, ret) << asm_;
}

// The inlined cleanup a `return` emits must be EXCLUDED from the try's own
// Finally range: the try has already been left, so re-entering the handler
// would re-raise a value it never saved. The table therefore holds two
// non-overlapping Finally pieces rather than one.
TEST(Bytecode, AReturnPunchesAHoleInItsOwnFinallyRange) {
    const std::string asm_ = listing("def f(): Int = try { return 7 } finally { 9 }");
    std::size_t entries = 0, at = 0;
    while ((at = asm_.find("finally", at)) != std::string::npos) { ++entries; ++at; }
    EXPECT_GE(entries, 2u) << asm_;
}
