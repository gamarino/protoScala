#include "EvalHarness.h"
#include "frontend/AST.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using namespace protoScala;
using protoScala::test::EvalHarness;

namespace {
std::string d(const std::string& src) { return dump(*desugarExpr(parseExpressionSource(src))); }
std::string du(const std::string& src) {
    auto unit = parseSource(src);
    desugar(*unit);
    return dump(*unit);
}
} // namespace

TEST(Desugar, InfixBecomesMethodCall) {
    EXPECT_EQ(d("1 + 2"), "(apply (. (int 1) +) (int 2))");
    EXPECT_EQ(d("a + b * c"), "(apply (. a +) (apply (. b *) c))");
    EXPECT_EQ(d("a max b"), "(apply (. a max) b)");
    EXPECT_EQ(d("a && b"), "(apply (. a &&) b)");
}

TEST(Desugar, RightAssociativeKeepsLeftToRightEvaluation) {
    EXPECT_EQ(d("a :: b"), "(apply (. b ::) a)");
    EXPECT_EQ(d("f(x) :: b"), "(block (val <ra0> (apply f x)) (apply (. b ::) <ra0>))");
}

TEST(Desugar, PrefixOperators) {
    EXPECT_EQ(d("-x"), "(. x unary_-)");
    EXPECT_EQ(d("!b"), "(. b unary_!)");
    EXPECT_EQ(d("-5"), "(int -5)");
}

TEST(Desugar, AssignmentOperators) {
    EXPECT_EQ(d("x += 1"), "(= x (apply (. x +) (int 1)))");
    EXPECT_EQ(d("a.b += 1"), "(apply (. a b_=) (apply (. (. a b) +) (int 1)))");
    // A receiver that is not a simple path keeps the Phase 1 `op=` send (Q19 of Phase 1).
    EXPECT_EQ(d("f(a).b += 1"), "(apply (. (. (apply f a) b) +=) (int 1))");
}

TEST(Desugar, ParensTypedAndIfWithoutElse) {
    EXPECT_EQ(d("(a)"), "a");
    EXPECT_EQ(d("x: Int"), "x");
    EXPECT_EQ(d("if a then b"), "(if a (block b ()) ())");
    EXPECT_EQ(d("if a then b = 1"), "(if a (= b (int 1)) ())");  // already ()
}

TEST(Desugar, ValueDiscardingForUnit) {
    EXPECT_EQ(d("(x: Unit)"), "(block x ())");
    EXPECT_EQ(d("(x: Int)"), "x");
}

TEST(Desugar, RecursesEverywhere) {
    EXPECT_EQ(d("x => -x + 1"), "(lambda (x) (apply (. (. x unary_-) +) (int 1)))");
    EXPECT_EQ(d("while (i < n) i += 1"),
              "(while (apply (. i <) n) (= i (apply (. i +) (int 1))))");
}

TEST(Desugar, MultipleParameterListsBecomeNestedLambdas) {
    EXPECT_EQ(du("def add(a: Int)(b: Int): Int = a + b"),
              "(unit (def add ((a:Int)) : Int (lambda (b:Int) (apply (. a +) b))))");
    EXPECT_EQ(du("def f(a: Int)(b: Int)(c: Int) = a"),
              "(unit (def f ((a:Int)) (lambda (b:Int) (lambda (c:Int) a))))");
}

TEST(Desugar, DefinitionsAndBlocks) {
    EXPECT_EQ(du("val x = -1 * y"), "(unit (val x (apply (. (int -1) *) y)))");
    EXPECT_EQ(du("def f = { val a = (1); a + 1 }"),
              "(unit (def f (block (val a (int 1)) (apply (. a +) (int 1)))))");
}

TEST(Desugar, SettersAndUpdate) {
    EXPECT_EQ(d("p.x = 3"), "(apply (. p x_=) (int 3))");
    EXPECT_EQ(d("this.x = 3"), "(apply (. this x_=) (int 3))");
    EXPECT_EQ(d("a(1) = 2"), "(apply (. a update) (int 1) (int 2))");
    EXPECT_EQ(d("m(1, 2) = 3"), "(apply (. m update) (int 1) (int 2) (int 3))");
}

TEST(Desugar, ForComprehensions) {
    EXPECT_EQ(d("for (x <- xs) yield x * 2"),
              "(apply (. xs map) (lambda (x) (apply (. x *) (int 2))))");
    EXPECT_EQ(d("for (x <- xs) println(x)"), "(apply (. xs foreach) (lambda (x) (apply println x)))");
    EXPECT_EQ(d("for (x <- xs if x > 1) yield x"),
              "(apply (. (apply (. xs withFilter) (lambda (x) (apply (. x >) (int 1)))) map) "
              "(lambda (x) x))");
    EXPECT_EQ(d("for (x <- xs; y <- ys) yield (x, y)"),
              "(apply (. xs flatMap) (lambda (x) (apply (. ys map) (lambda (y) (tuple x y)))))");
    EXPECT_EQ(d("for (x <- xs; y <- ys) println(y)"),
              "(apply (. xs foreach) (lambda (x) (apply (. ys foreach) (lambda (y) "
              "(apply println y)))))");
    EXPECT_EQ(d("for ((a, b) <- ps) yield a"),
              "(apply (. ps map) (lambda (<p0>) (match <p0> (case (tuple-pat a b) a))))");
    EXPECT_EQ(d("for (Some(v) <- os) yield v"),
              "(apply (. (apply (. os withFilter) (lambda (<p0>) (match <p0> "
              "(case (unapply Some v) true) (case _ false)))) map) "
              "(lambda (<p1>) (match <p1> (case (unapply Some v) v))))");
    EXPECT_EQ(d("for (x <- xs; y = x * 2) yield y"),
              "(apply (. (apply (. xs map) (lambda (x) (block (val y (apply (. x *) (int 2))) "
              "(tuple x y)))) map) (lambda (<p0>) (match <p0> (case (tuple-pat x y) y))))");
}

TEST(Desugar, CaseClassCompanions) {
    EXPECT_EQ(du("case class P(x: Int, y: Int)"),
              "(unit (case-class P (x:Int y:Int) (body)) (object P (body "
              "(def apply ((x:Int y:Int)) (new P x y)) (def unapply ((<u>)) <u>))))");
    EXPECT_EQ(du("case class V(xs: Int*)"),
              "(unit (case-class V (xs:Int*) (body)) (object V (body "
              "(def apply ((xs:Int*)) (new V (splice xs))) (def unapply ((<u>)) <u>))))");
    // A user companion keeps its members and gains only what it lacks.
    EXPECT_EQ(du("case class P(x: Int)\nobject P:\n  def apply(s: String) = new P(s.length)\n"),
              "(unit (case-class P (x:Int) (body)) (object P (body (def apply ((s:String)) "
              "(new P (. s length))) (def unapply ((<u>)) <u>))))");
}

TEST(Desugar, PatternValues) {
    EXPECT_EQ(du("val (a, b) = pair"),
              "(unit (val <t0> (match pair (case (tuple-pat a b) (tuple a b)))) "
              "(val a (. <t0> _1)) (val b (. <t0> _2)))");
    EXPECT_EQ(du("val Some(v) = o"), "(unit (val v (match o (case (unapply Some v) v))))");
    EXPECT_EQ(du("val List() = xs"), "(unit (match xs (case (unapply List) ())))");
    EXPECT_EQ(du("def f = {\n  val (a, b) = p\n  a\n}"),
              "(unit (def f (block (val <t0> (match p (case (tuple-pat a b) (tuple a b)))) "
              "(val a (. <t0> _1)) (val b (. <t0> _2)) a)))");
}

// --- Nested templates (Phase 4) --------------------------------------------

TEST(Desugar, ANestedTemplateIsLiftedWithAQualifiedName) {
    const std::string d = du("object O:\n  class C(val n: Int)\n");
    // The lifted template comes FIRST, so the object's own body can name it.
    EXPECT_NE(d.find("O.C"), std::string::npos) << d;
    EXPECT_LT(d.find("O.C"), d.find("(object O ")) << d;
}

TEST(Desugar, NestingIsArbitrarilyDeep) {
    const std::string d = du("object O:\n  object P:\n    class C(val n: Int)\n");
    EXPECT_NE(d.find("O.P.C"), std::string::npos) << d;
}

TEST(Desugar, ANestedTemplateInAClassIsStillRejected) {
    EXPECT_THROW(du("class Outer:\n  class Inner(val n: Int)\n"), ParseError);
}

TEST(Desugar, ASiblingNamedInAnExtendsClauseIsQualified) {
    const std::string d =
        du("object Ast:\n  sealed trait T\n  case class Leaf(n: Int) extends T\n");
    // `extends T` must have become `extends Ast.T`, because a parent is resolved
    // before any template scope exists.
    EXPECT_NE(d.find("Ast.T"), std::string::npos) << d;
}

// --- enum (Phase 4, DESIGN §4.5) -------------------------------------------

TEST(Desugar, AnEnumExpandsToASealedClassCasesAndACompanion) {
    const std::string u = du("enum Color:\n  case Red, Green\n");
    // The cases are written inside the companion and lifted out by the nested-
    // template pass, so they carry the qualified names Scala requires.
    EXPECT_NE(u.find("class abstract sealed Color"), std::string::npos) << u;
    EXPECT_NE(u.find("Color.Red"), std::string::npos) << u;
    EXPECT_NE(u.find("Color.Green"), std::string::npos) << u;
    // The marker parent is named by the `Enum` type KEY, not by the source name
    // `Enum`, so that an enum may itself be called `Enum` without extending
    // itself.
    EXPECT_NE(u.find("@Enum"), std::string::npos) << u;
    EXPECT_NE(u.find("values"), std::string::npos) << u;
    EXPECT_NE(u.find("valueOf"), std::string::npos) << u;
    EXPECT_NE(u.find("fromOrdinal"), std::string::npos) << u;
}

TEST(Desugar, AnEnumWithAParameterisedCaseHasNoValuesOrValueOf) {
    // scalac defines `values` and `valueOf` only when every case is a singleton;
    // `fromOrdinal` always exists and covers the singletons alone.
    const std::string u = du("enum Tree:\n  case Leaf(n: Int)\n");
    EXPECT_EQ(u.find("values"), std::string::npos) << u;
    EXPECT_EQ(u.find("valueOf"), std::string::npos) << u;
    EXPECT_NE(u.find("fromOrdinal"), std::string::npos) << u;
}

TEST(Desugar, EnumCasesKeepTheirDeclarationOrdinals) {
    EvalHarness h;
    h.eval("enum Colour2 { case Red, Green, Blue }");
    EXPECT_EQ(h.eval("Colour2.Red.ordinal"), "0");
    EXPECT_EQ(h.eval("Colour2.Green.ordinal"), "1");
    EXPECT_EQ(h.eval("Colour2.Blue.ordinal"), "2");
    EXPECT_EQ(h.eval("Colour2.values.length"), "3");
}
