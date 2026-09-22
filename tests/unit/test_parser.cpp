#include "frontend/AST.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using protoScala::ParseError;
using protoScala::dump;
using protoScala::parseExpressionSource;

namespace {
std::string e(const std::string& src) { return dump(*parseExpressionSource(src)); }

bool incomplete(const std::string& src) {
    try {
        parseExpressionSource(src);
    } catch (const ParseError& err) {
        return err.atEof;
    }
    return false;
}
} // namespace

TEST(Parser, Literals) {
    EXPECT_EQ(e("42"), "(int 42)");
    EXPECT_EQ(e("-5"), "(int -5)");
    EXPECT_EQ(e("2.5"), "(float 2.5)");
    EXPECT_EQ(e("-2.5"), "(float -2.5)");
    EXPECT_EQ(e("\"a\\n\""), "(str \"a\\n\")");
    EXPECT_EQ(e("'c'"), "(char 'c')");
    EXPECT_EQ(e("true"), "true");
    EXPECT_EQ(e("null"), "null");
    EXPECT_EQ(e("()"), "()");
    EXPECT_EQ(e("s\"x $y\""), "(interp s)");
}

TEST(Parser, PrecedenceByFirstCharacter) {
    EXPECT_EQ(e("1 + 2 * 3"), "(infix + (int 1) (infix * (int 2) (int 3)))");
    EXPECT_EQ(e("a - b - c"), "(infix - (infix - a b) c)");
    EXPECT_EQ(e("a max b + 1"), "(infix max a (infix + b (int 1)))");
    EXPECT_EQ(e("a == b && c < d || e"),
              "(infix || (infix && (infix == a b) (infix < c d)) e)");
    EXPECT_EQ(e("x += 1"), "(infix += x (int 1))");
}

TEST(Parser, RightAssociativeColonOperators) {
    EXPECT_EQ(e("a :: b :: c"), "(infix :: a (infix :: b c))");
}

TEST(Parser, MixedAssociativityIsAnError) {
    EXPECT_THROW(parseExpressionSource("a +: b +- c"), ParseError);
}

TEST(Parser, PrefixOperators) {
    EXPECT_EQ(e("-x + !y"), "(infix + (prefix - x) (prefix ! y))");
    EXPECT_EQ(e("~n"), "(prefix ~ n)");
}

TEST(Parser, InfixAcrossLines) {
    EXPECT_EQ(e("a +\n  b"), "(infix + a b)");
    EXPECT_EQ(e("a +\nb"), "(infix + a b)");
    EXPECT_EQ(e("a\n  + b"), "(infix + a b)");
}

TEST(Parser, SelectionApplicationAndTypeArguments) {
    EXPECT_EQ(e("f(1, 2)(3)"), "(apply (apply f (int 1) (int 2)) (int 3))");
    EXPECT_EQ(e("a.b.c(d)"), "(apply (. (. a b) c) d)");
    EXPECT_EQ(e("s.length"), "(. s length)");
    EXPECT_EQ(e("f[Int](x)"), "(apply (tapply f Int) x)");
    EXPECT_EQ(e("f()"), "(apply f)");
}

TEST(Parser, BlockArgumentAndBlockLambda) {
    EXPECT_EQ(e("xs.foreach { x => println(x) }"),
              "(apply (. xs foreach) (block (lambda (x) (block (apply println x)))))");
    EXPECT_EQ(e("xs.foreach { x =>\n  a\n  b\n}"),
              "(apply (. xs foreach) (block (lambda (x) (block a b))))");
}

TEST(Parser, IfBothStyles) {
    EXPECT_EQ(e("if (a) b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b"), "(if a b)");
    EXPECT_EQ(e("if a then\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) {\n  b\n} else {\n  c\n}"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a)\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) || b then c else d"), "(if (infix || (parens a) b) c d)");
    EXPECT_EQ(e("if a then b\nelse c"), "(if a b c)");
}

TEST(Parser, WhileBothStyles) {
    EXPECT_EQ(e("while (i < 10) i += 1"), "(while (infix < i (int 10)) (infix += i (int 1)))");
    EXPECT_EQ(e("while i < 10 do\n  i += 1"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
    EXPECT_EQ(e("while (i < 10) {\n  i += 1\n}"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
}

TEST(Parser, DoWhileIsRejected) {
    EXPECT_THROW(parseExpressionSource("do x while (c)"), ParseError);
}

TEST(Parser, Lambdas) {
    EXPECT_EQ(e("x => x * 2"), "(lambda (x) (infix * x (int 2)))");
    EXPECT_EQ(e("(x: Int, y: Int) => x + y"), "(lambda (x:Int y:Int) (infix + x y))");
    EXPECT_EQ(e("() => 42"), "(lambda () (int 42))");
    EXPECT_EQ(e("_ => 1"), "(lambda (_) (int 1))");
    EXPECT_EQ(e("(f: (Int, String) => Boolean) => f"),
              "(lambda (f:(Int, String) => Boolean) f)");
    EXPECT_EQ(e("x =>\n  f(x)\n  x"), "(lambda (x) (block (apply f x) x))");
}

TEST(Parser, ParensTuplesAscriptionSplicesNamedArgs) {
    EXPECT_EQ(e("(a)"), "(parens a)");
    EXPECT_EQ(e("(a, b)"), "(tuple a b)");
    EXPECT_EQ(e("x: Int"), "(typed x Int)");
    EXPECT_EQ(e("f(xs*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(xs: _*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(x = 1)"), "(apply f (named x (int 1)))");
}

TEST(Parser, AssignmentAndReturn) {
    EXPECT_EQ(e("x = y + 1"), "(= x (infix + y (int 1)))");
    EXPECT_EQ(e("return"), "(return)");
    EXPECT_EQ(e("return x"), "(return x)");
}

TEST(Parser, TypesAreParsedIntoTypeTrees) {
    EXPECT_EQ(e("x: List[Map[String, Int]]"), "(typed x List[Map[String, Int]])");
    EXPECT_EQ(e("x: (Int, String)"), "(typed x (Int, String))");
    EXPECT_EQ(e("x: Int => Int"), "(typed x (Int) => Int)");
    EXPECT_EQ(e("x: A | B"), "(typed x A | B)");
    EXPECT_EQ(e("x: scala.collection.Seq[?]"), "(typed x scala.collection.Seq[?])");
}

TEST(Parser, LaterPhaseConstructsAreReportedClearly) {
    for (const char* src : {"x match { case 1 => 2 }", "throw e", "try a finally b",
                            "for (x <- xs) yield x", "new A", "this", "_ + 1"}) {
        try {
            parseExpressionSource(src);
            FAIL() << "expected ParseError for " << src;
        } catch (const ParseError& err) {
            EXPECT_NE(std::string(err.what()).find("not implemented yet"), std::string::npos)
                << src << ": " << err.what();
        }
    }
}

TEST(Parser, IncompleteInputIsFlaggedForTheRepl) {
    EXPECT_TRUE(incomplete("1 +"));
    EXPECT_TRUE(incomplete("(1 + "));
    EXPECT_TRUE(incomplete("if a then"));
    EXPECT_TRUE(incomplete("{ a + 1"));
    EXPECT_TRUE(incomplete("\"\"\"abc"));
    EXPECT_FALSE(incomplete("1 + )"));
}

TEST(Parser, ErrorsCarryPositions) {
    try {
        parseExpressionSource("f(1,\n  ]");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_EQ(err.pos.line, 2);
        EXPECT_EQ(err.pos.column, 3);
    }
}

// Additional coverage beyond the plan: brace and indentation variants must
// produce the same trees, and associativity mixing is caught across nesting.

TEST(Parser, BraceAndIndentedBlocksAreEquivalent) {
    EXPECT_EQ(e("{\n  a\n  b\n}"), "(block a b)");
    EXPECT_EQ(e("{ a; b }"), "(block a b)");
    EXPECT_EQ(e("while i < 3 do\n  f(i)\n  i += 1"), e("while (i < 3) {\n  f(i)\n  i += 1\n}"));
    EXPECT_EQ(e("if a then\n  if b then\n    c\n  else\n    d\nelse\n  e"),
              "(if a (block (if b (block c) (block d))) (block e))");
    EXPECT_EQ(e("if (a) {\n  if (b) {\n    c\n  } else {\n    d\n  }\n} else {\n  e\n}"),
              "(if a (block (if b (block c) (block d))) (block e))");
}

TEST(Parser, StatementsAfterNestedIndentedBlocks) {
    EXPECT_EQ(e("{\n  if a then\n    b\n  c\n}"), "(block (if a (block b)) c)");
    EXPECT_EQ(e("{\n  while a do\n    b\n  c\n}"), "(block (while a (block b)) c)");
}

TEST(Parser, EndMarkersInBlocks) {
    EXPECT_EQ(e("{\n  if a then\n    b\n  end if\n  c\n}"), "(block (if a (block b)) c)");
    EXPECT_EQ(e("{\n  while a do\n    b\n  end while\n}"), "(block (while a (block b)))");
    try {
        parseExpressionSource("{\n  while a do\n    b\n  end if\n}");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_NE(std::string(err.what()).find("misaligned end marker"), std::string::npos);
    }
}

TEST(Parser, MixedAssociativityIsCaughtInNestedOperands) {
    EXPECT_THROW(parseExpressionSource("a +: b +: c +- d"), ParseError);
    EXPECT_THROW(parseExpressionSource("a +- b +: c"), ParseError);
    EXPECT_EQ(e("a +: b * c +: d"), "(infix +: a (infix +: (infix * b c) d))");
    // A tighter operator in between does not reset the level's associativity.
    EXPECT_THROW(parseExpressionSource("a +: b * c +- d"), ParseError);
    EXPECT_THROW(parseExpressionSource("a :: b + c :+ d"), ParseError);
    // A looser operator does.
    EXPECT_EQ(e("a +- b < c +: d"), "(infix < (infix +- a b) (infix +: c d))");
}

TEST(Parser, NegativeLiteralFoldAndSelections) {
    EXPECT_EQ(e("-1.abs"), "(. (int -1) abs)");
    EXPECT_EQ(e("- x.abs"), "(prefix - (. x abs))");
    EXPECT_EQ(e("a - 1"), "(infix - a (int 1))");
    EXPECT_EQ(e("- 5"), "(prefix - (int 5))");
    EXPECT_EQ(e("- 2.5"), "(prefix - (float 2.5))");
    EXPECT_EQ(e("-9223372036854775808L"), "(int -9223372036854775808)");
}

TEST(Parser, LambdasAsArgumentsAndApplicationOnNextLineIsNotAnArgument) {
    EXPECT_EQ(e("xs.map(x => x + 1)"), "(apply (. xs map) (lambda (x) (infix + x (int 1))))");
    EXPECT_EQ(e("f((a) + 1)"), "(apply f (infix + (parens a) (int 1)))");
    EXPECT_EQ(e("xs.map { (a, b) => a }"),
              "(apply (. xs map) (block (lambda (a b) (block a))))");
    EXPECT_EQ(e("{\n  f\n  (a)\n}"), "(block f (parens a))");
}

TEST(Parser, AssignmentTargets) {
    EXPECT_EQ(e("a.b = 1"), "(= (. a b) (int 1))");
    EXPECT_EQ(e("a(0) = 1"), "(= (apply a (int 0)) (int 1))");
    EXPECT_THROW(parseExpressionSource("1 = 2"), ParseError);
}

TEST(Parser, OperatorTableHelpers) {
    using protoScala::isAssignmentOperator;
    using protoScala::isRightAssociative;
    using protoScala::precedence;
    EXPECT_TRUE(isAssignmentOperator("+="));
    EXPECT_TRUE(isAssignmentOperator("::="));
    EXPECT_FALSE(isAssignmentOperator("=="));
    EXPECT_FALSE(isAssignmentOperator("<="));
    EXPECT_FALSE(isAssignmentOperator("!="));
    EXPECT_FALSE(isAssignmentOperator("="));
    EXPECT_TRUE(isRightAssociative("::"));
    EXPECT_FALSE(isRightAssociative("++"));
    EXPECT_LT(precedence("max"), precedence("|"));
    EXPECT_LT(precedence("+="), precedence("max"));
    EXPECT_LT(precedence("+"), precedence("*"));
    EXPECT_LT(precedence("*"), precedence("#"));
}

TEST(Parser, CompilationUnitOfExpressions) {
    EXPECT_EQ(dump(*protoScala::parseSource("f(1)\ng(2); h")),
              "(unit (apply f (int 1)) (apply g (int 2)) h)");
    EXPECT_EQ(dump(*protoScala::parseSource("")), "(unit)");
}

TEST(Parser, OldStyleConditionFollowedByNestedNewStyleConstruct) {
    EXPECT_EQ(e("if (x) 1 else if y then 2 else 3"), "(if x (int 1) (if y (int 2) (int 3)))");
    EXPECT_EQ(e("if (a) if b then c else d"), "(if a (if b c d))");
    EXPECT_EQ(e("while (a) while b do c"), "(while a (while b c))");
    EXPECT_EQ(e("if (a) xs.map(x => x) else b"),
              "(if a (apply (. xs map) (lambda (x) x)) b)");
    EXPECT_EQ(e("if (a) || b then c else d"), "(if (infix || (parens a) b) c d)");
}
