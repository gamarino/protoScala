#include "frontend/AST.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using namespace protoScala;

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
    EXPECT_EQ(d("a.b += 1"), "(apply (. (. a b) +=) (int 1))");
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
