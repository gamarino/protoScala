#include "frontend/Layout.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

using protoScala::LexError;
using protoScala::tokenize;
using protoScala::test::kinds;

namespace {
std::string lay(const std::string& src) { return kinds(tokenize(src)); }
}

TEST(Layout, StatementsOnSeparateLinesGetNewline) {
    EXPECT_EQ(lay("val a = 1\nval b = 2"),
              "KwVal Identifier Equals IntLit Newline KwVal Identifier Equals IntLit EOF");
}

TEST(Layout, NoNewlineWhenLineCannotEndOrNextCannotBegin) {
    EXPECT_EQ(lay("val a = 1 +\n  2"),
              "KwVal Identifier Equals IntLit Identifier IntLit EOF");
    EXPECT_EQ(lay("f(a,\nb)"), "Identifier LParen Identifier Comma Identifier RParen EOF");
    EXPECT_EQ(lay("x\n.foo"), "Identifier Dot Identifier EOF");
}

TEST(Layout, IndentAfterEqualsAndOutdentAtDedent) {
    EXPECT_EQ(lay("def f =\n  1\ndef g = 2"),
              "KwDef Identifier Equals Indent IntLit Outdent Newline "
              "KwDef Identifier Equals IntLit EOF");
}

TEST(Layout, BraceVariantOfTheSameDefinition) {
    EXPECT_EQ(lay("def f = {\n  1\n}\ndef g = 2"),
              "KwDef Identifier Equals LBrace IntLit RBrace Newline "
              "KwDef Identifier Equals IntLit EOF");
}

TEST(Layout, IndentedBlockStatementsAreSeparatedByNewline) {
    EXPECT_EQ(lay("def f =\n  val a = 1\n  a"),
              "KwDef Identifier Equals Indent KwVal Identifier Equals IntLit Newline "
              "Identifier Outdent EOF");
}

TEST(Layout, IfThenElseIndented) {
    EXPECT_EQ(lay("if c then\n  a\nelse\n  b"),
              "KwIf Identifier KwThen Indent Identifier Outdent KwElse Indent "
              "Identifier Outdent EOF");
}

TEST(Layout, IfThenElseBraces) {
    EXPECT_EQ(lay("if (c) {\n  a\n} else {\n  b\n}"),
              "KwIf LParen Identifier RParen LBrace Identifier RBrace KwElse LBrace "
              "Identifier RBrace EOF");
}

TEST(Layout, OldStyleConditionOpensRegion) {
    EXPECT_EQ(lay("if (c)\n  a\nelse\n  b"),
              "KwIf LParen Identifier RParen Indent Identifier Outdent KwElse Indent "
              "Identifier Outdent EOF");
    EXPECT_EQ(lay("while (c)\n  a"),
              "KwWhile LParen Identifier RParen Indent Identifier Outdent EOF");
}

TEST(Layout, WhileDoIndented) {
    EXPECT_EQ(lay("while c do\n  a\n  b"),
              "KwWhile Identifier KwDo Indent Identifier Newline Identifier Outdent EOF");
}

TEST(Layout, ArrowOpensRegionInsideBraces) {
    EXPECT_EQ(lay("xs.foreach { x =>\n  a\n  b\n}"),
              "Identifier Dot Identifier LBrace Identifier Arrow Indent Identifier Newline "
              "Identifier Outdent RBrace EOF");
}

TEST(Layout, NothingIsInsertedInsideParensOrBrackets) {
    EXPECT_EQ(lay("f(x =>\n    x + 1,\n  2)"),
              "Identifier LParen Identifier Arrow Identifier Identifier IntLit Comma "
              "IntLit RParen EOF");
    EXPECT_EQ(lay("val m: Map[\n  Int,\n  Int] = e"),
              "KwVal Identifier Colon Identifier LBracket Identifier Comma Identifier "
              "RBracket Equals Identifier EOF");
}

TEST(Layout, RegionOpenersFromDesignList) {
    // Every opener of DESIGN §3.2 produces Indent when the next line is deeper.
    const char* openers[] = {"=", "=>", "then", "else", "do", "try", "catch", "finally",
                             "match", "with", "yield", "return", "throw", "<-"};
    for (const char* op : openers) {
        const std::string src = std::string("a ") + op + "\n  b";
        const std::string laid = lay(src);
        EXPECT_NE(laid.find("Indent Identifier Outdent"), std::string::npos)
            << "opener " << op << " gave: " << laid;
    }
}

TEST(Layout, ColonAtEndOfLineOpensTemplateBody) {
    EXPECT_EQ(lay("object A:\n  val x = 1"),
              "KwObject Identifier ColonEol Indent KwVal Identifier Equals IntLit Outdent EOF");
    EXPECT_EQ(lay("object A {\n  val x = 1\n}"),
              "KwObject Identifier LBrace KwVal Identifier Equals IntLit RBrace EOF");
    // A colon followed by a type on the same line stays a Colon.
    EXPECT_EQ(lay("val x: Int = 1"), "KwVal Identifier Colon Identifier Equals IntLit EOF");
}

TEST(Layout, MultipleOutdentsInARow) {
    EXPECT_EQ(lay("def f =\n  if c then\n    a\n  else\n    b\nf"),
              "KwDef Identifier Equals Indent KwIf Identifier KwThen Indent Identifier "
              "Outdent KwElse Indent Identifier Outdent Outdent Newline Identifier EOF");
}

TEST(Layout, OutdentBeforeClosingBrace) {
    EXPECT_EQ(lay("{\n  val a =\n    1\n}"),
              "LBrace KwVal Identifier Equals Indent IntLit Outdent RBrace EOF");
}

TEST(Layout, ElseOnSameLineClosesThenRegion) {
    EXPECT_EQ(lay("if c then\n  a else b"),
              "KwIf Identifier KwThen Indent Identifier Outdent KwElse Identifier EOF");
}

TEST(Layout, ElseOnSameLineDoesNotCloseAnEnclosingBody) {
    EXPECT_EQ(lay("def f =\n  if a then b else c"),
              "KwDef Identifier Equals Indent KwIf Identifier KwThen Identifier KwElse "
              "Identifier Outdent EOF");
}

TEST(Layout, LeadingInfixOperatorContinuesTheLine) {
    EXPECT_EQ(lay("val x = a\n+ b"),
              "KwVal Identifier Equals Identifier Identifier Identifier EOF");
    // Not an infix operator: `-b` is a prefix expression on its own line.
    EXPECT_EQ(lay("a\n-b"), "Identifier Newline Identifier Identifier EOF");
}

TEST(Layout, EndMarkers) {
    EXPECT_EQ(lay("if c then\n  a\nend if\nb"),
              "KwIf Identifier KwThen Indent Identifier Outdent Newline EndMarker Newline "
              "Identifier EOF");
    auto toks = tokenize("def f =\n  1\nend f");
    EXPECT_EQ(toks[toks.size() - 2].kind, protoScala::TokenKind::EndMarker);
    EXPECT_EQ(toks[toks.size() - 2].text, "f");
    // `end` used as an ordinary identifier.
    EXPECT_EQ(lay("val end = 1"), "KwVal Identifier Equals IntLit EOF");
}

TEST(Layout, BracesRegionIgnoresDeeperContinuation) {
    EXPECT_EQ(lay("{\n  a\n    .b\n  c\n}"),
              "LBrace Identifier Dot Identifier Newline Identifier RBrace EOF");
}

TEST(Layout, BadUnindentIsAnError) {
    EXPECT_THROW(tokenize("def f =\n    a\n  b"), LexError);
}

TEST(Layout, UnbalancedBracketsAreErrors) {
    EXPECT_THROW(tokenize("f(a]"), LexError);
    try {
        tokenize("f(a,");
        FAIL() << "expected LexError";
    } catch (const LexError& e) {
        EXPECT_TRUE(e.atEof);  // the REPL keeps reading
    }
}

TEST(Layout, LexicalErrorsBecomeLexError) {
    try {
        tokenize("\"\"\"open");
        FAIL();
    } catch (const LexError& e) {
        EXPECT_TRUE(e.atEof);
    }
}

TEST(Layout, IndentedFirstLineSetsTopLevelWidth) {
    EXPECT_EQ(lay("  a\n  b"), "Identifier Newline Identifier EOF");
}

// Brace and indentation variants of the remaining region openers.

TEST(Layout, WhileBraces) {
    EXPECT_EQ(lay("while (c) {\n  a\n  b\n}"),
              "KwWhile LParen Identifier RParen LBrace Identifier Newline Identifier RBrace EOF");
}

TEST(Layout, MatchIndented) {
    EXPECT_EQ(lay("x match\n  case 1 => a\n  case 2 =>\n    b\nc"),
              "Identifier KwMatch Indent KwCase IntLit Arrow Identifier Newline KwCase IntLit "
              "Arrow Indent Identifier Outdent Outdent Newline Identifier EOF");
}

TEST(Layout, MatchBraces) {
    EXPECT_EQ(lay("x match {\n  case 1 => a\n  case 2 =>\n    b\n}\nc"),
              "Identifier KwMatch LBrace KwCase IntLit Arrow Identifier Newline KwCase IntLit "
              "Arrow Indent Identifier Outdent RBrace Newline Identifier EOF");
}

TEST(Layout, TryCatchFinallyIndented) {
    EXPECT_EQ(lay("try\n  a\ncatch\n  case e => b\nfinally\n  c"),
              "KwTry Indent Identifier Outdent KwCatch Indent KwCase Identifier Arrow "
              "Identifier Outdent KwFinally Indent Identifier Outdent EOF");
}

TEST(Layout, TryCatchFinallyBraces) {
    EXPECT_EQ(lay("try {\n  a\n} catch {\n  case e => b\n} finally {\n  c\n}"),
              "KwTry LBrace Identifier RBrace KwCatch LBrace KwCase Identifier Arrow "
              "Identifier RBrace KwFinally LBrace Identifier RBrace EOF");
}

TEST(Layout, ForYieldIndented) {
    EXPECT_EQ(lay("for\n  x <- xs\n  y <- ys\nyield\n  x"),
              "KwFor Indent Identifier LeftArrow Identifier Newline Identifier LeftArrow "
              "Identifier Outdent KwYield Indent Identifier Outdent EOF");
}

TEST(Layout, ForYieldBraces) {
    EXPECT_EQ(lay("for {\n  x <- xs\n  y <- ys\n} yield x"),
              "KwFor LBrace Identifier LeftArrow Identifier Newline Identifier LeftArrow "
              "Identifier RBrace KwYield Identifier EOF");
}

TEST(Layout, EndMarkerAfterWhileDo) {
    EXPECT_EQ(lay("while c do\n  a\nend while"),
              "KwWhile Identifier KwDo Indent Identifier Outdent Newline EndMarker EOF");
}

TEST(Layout, BlankLinesBetweenTemplateMembers) {
    EXPECT_EQ(lay("class C:\n  def f =\n    1\n\n  def g = 2\n\nval z = 3"),
              "KwClass Identifier ColonEol Indent KwDef Identifier Equals Indent IntLit Outdent "
              "Newline KwDef Identifier Equals IntLit Outdent Newline KwVal Identifier Equals "
              "IntLit EOF");
}

TEST(Layout, DedentBelowTopLevelIsAnError) {
    EXPECT_THROW(tokenize("  a\nb"), LexError);
}
