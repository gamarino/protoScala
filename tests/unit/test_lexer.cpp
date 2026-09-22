#include "TestSupport.h"

#include <gtest/gtest.h>

using protoScala::Token;
using protoScala::TokenKind;
using protoScala::test::kinds;
using protoScala::test::rawTokens;

TEST(Lexer, EmptyInputIsEof) {
    EXPECT_EQ(kinds(rawTokens("")), "EOF");
}

TEST(Lexer, HardKeywordsAndIdentifiers) {
    EXPECT_EQ(kinds(rawTokens("val x = if then else")),
              "KwVal Identifier Equals KwIf KwThen KwElse EOF");
    EXPECT_EQ(kinds(rawTokens("this def while do return lazy var")),
              "KwThis KwDef KwWhile KwDo KwReturn KwLazy KwVar EOF");
}

TEST(Lexer, SoftKeywordsAreIdentifiers) {
    auto t = rawTokens("end as using inline open");
    for (int i = 0; i < 5; ++i) EXPECT_EQ(t[i].kind, TokenKind::Identifier);
}

TEST(Lexer, OperatorAndMixedIdentifiers) {
    auto t = rawTokens("a+b :: <= unary_- x_+ ! ~");
    EXPECT_EQ(t[0].text, "a");
    EXPECT_EQ(t[1].text, "+");  EXPECT_TRUE(t[1].isOperator);
    EXPECT_EQ(t[2].text, "b");
    EXPECT_EQ(t[3].text, "::");
    EXPECT_EQ(t[4].text, "<=");
    EXPECT_EQ(t[5].text, "unary_-"); EXPECT_FALSE(t[5].isOperator);
    EXPECT_EQ(t[6].text, "x_+");
    EXPECT_EQ(t[7].text, "!");
    EXPECT_EQ(t[8].text, "~");
}

TEST(Lexer, ReservedOperators) {
    EXPECT_EQ(kinds(rawTokens("= => <- <: >: # @ : _ ?=> =>>")),
              "Equals Arrow LeftArrow Subtype Supertype Hash At Colon Underscore "
              "CtxArrow TypeLambdaArrow EOF");
    EXPECT_EQ(kinds(rawTokens("== =>> :+")), "Identifier TypeLambdaArrow Identifier EOF");
}

TEST(Lexer, BackquotedIdentifier) {
    auto t = rawTokens("`type` `hello world`");
    EXPECT_EQ(t[0].kind, TokenKind::Identifier);
    EXPECT_EQ(t[0].text, "type");
    EXPECT_TRUE(t[0].backquoted);
    EXPECT_EQ(t[1].text, "hello world");
}

TEST(Lexer, UnicodeIdentifier) {
    auto t = rawTokens("año = 1");
    EXPECT_EQ(t[0].kind, TokenKind::Identifier);
    EXPECT_EQ(t[0].text, "año");
    EXPECT_EQ(t[1].pos.column, 5);  // columns count code points
}

TEST(Lexer, IntegerLiterals) {
    auto t = rawTokens("42 0x1F 0b101 1_000_000 7L");
    EXPECT_EQ(t[0].intValue, 42);
    EXPECT_EQ(t[1].intValue, 31);
    EXPECT_EQ(t[2].intValue, 5);
    EXPECT_EQ(t[3].intValue, 1000000);
    EXPECT_EQ(t[4].intValue, 7);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(t[i].kind, TokenKind::IntLit);
}

TEST(Lexer, IntegerBeyondLongKeepsDigits) {
    auto t = rawTokens("123456789012345678901234567890");
    EXPECT_EQ(t[0].kind, TokenKind::IntLit);
    EXPECT_FALSE(t[0].fitsLong);
    EXPECT_EQ(t[0].digits, "123456789012345678901234567890");
    EXPECT_EQ(t[0].base, 10);
}

TEST(Lexer, LongSuffixAcceptedForEveryBase) {
    // docs/LANGUAGE.md §1: "L suffix accepted and ignored" applies to
    // decimal, hex and binary integers alike.
    auto t = rawTokens("0xFFL 0b101L 0x1F");
    EXPECT_EQ(t[0].kind, TokenKind::IntLit);
    EXPECT_TRUE(t[0].fitsLong);
    EXPECT_EQ(t[0].intValue, 255);
    EXPECT_EQ(t[1].kind, TokenKind::IntLit);
    EXPECT_EQ(t[1].intValue, 5);
    EXPECT_EQ(t[2].kind, TokenKind::IntLit);
    EXPECT_EQ(t[2].intValue, 31);
}

TEST(Lexer, HexDigitFIsConsumedNotMistakenForFloatSuffix) {
    // `f` is a valid hex digit: 0xFFf is the hex integer 0xFFF (4095), not a
    // float — the digit-consuming loop absorbs it before any suffix check
    // ever sees it.
    auto t = rawTokens("0xFFf");
    EXPECT_EQ(t[0].kind, TokenKind::IntLit);
    EXPECT_EQ(t[0].intValue, 0xFFF);
}

TEST(Lexer, LeadingZeroIsAnError) {
    auto t = rawTokens("012");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
}

TEST(Lexer, FloatLiterals) {
    auto t = rawTokens("1.5 1e3 2.5e-3 .5 3f 4d 1.toString");
    EXPECT_DOUBLE_EQ(t[0].floatValue, 1.5);
    EXPECT_DOUBLE_EQ(t[1].floatValue, 1000.0);
    EXPECT_DOUBLE_EQ(t[2].floatValue, 0.0025);
    EXPECT_DOUBLE_EQ(t[3].floatValue, 0.5);
    EXPECT_EQ(t[4].kind, TokenKind::FloatLit);
    EXPECT_EQ(t[5].kind, TokenKind::FloatLit);
    // `1.toString` is a method call on the integer 1.
    EXPECT_EQ(t[6].kind, TokenKind::IntLit);
    EXPECT_EQ(t[7].kind, TokenKind::Dot);
    EXPECT_EQ(t[8].text, "toString");
}

TEST(Lexer, CharLiterals) {
    auto t = rawTokens(R"('a' '\n' '\'' 'A' 'ñ')");
    EXPECT_EQ(t[0].charValue, U'a');
    EXPECT_EQ(t[1].charValue, U'\n');
    EXPECT_EQ(t[2].charValue, U'\'');
    EXPECT_EQ(t[3].charValue, U'A');
    EXPECT_EQ(t[4].charValue, U'ñ');
}

TEST(Lexer, SymbolLiteralIsRejected) {
    EXPECT_EQ(rawTokens("'sym")[0].kind, TokenKind::Error);
}

TEST(Lexer, CharLiteralUnicodeEscapeIsProcessedByReadEscape) {
    // Built by runtime concatenation, not written as a literal `A` in
    // this source file: the earlier version of this test embedded the
    // escape directly and it was silently pre-converted to the character
    // 'A' before ever reaching the compiler, so the test passed without
    // ever exercising Lexer::readEscape's 'u' branch. Concatenating two
    // plain string literals means the six-character sequence `A` only
    // exists in memory at run time, when the Lexer actually sees it.
    const std::string src = std::string("'\\") + "u0041'";
    auto t = rawTokens(src);
    ASSERT_EQ(t[0].kind, TokenKind::CharLit);
    EXPECT_EQ(t[0].charValue, U'A');
}

TEST(Lexer, StringLiteralUnicodeEscapeIsProcessedByReadEscape) {
    // Same concatenation technique as above, for the string-literal path
    // through the same Lexer::readEscape function.
    const std::string src = std::string("\"") + "\\" + "u00e9\"";
    auto t = rawTokens(src);
    ASSERT_EQ(t[0].kind, TokenKind::StringLit);
    EXPECT_EQ(t[0].stringValue, "\xC3\xA9");
}

TEST(Lexer, StringEscapes) {
    auto t = rawTokens(R"("a\tb\n\"q\" \\ é")");
    EXPECT_EQ(t[0].kind, TokenKind::StringLit);
    EXPECT_EQ(t[0].stringValue, "a\tb\n\"q\" \\ \xC3\xA9");
}

TEST(Lexer, UnclosedStringAtEndOfLineIsAnError) {
    auto t = rawTokens("\"abc\nx");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_FALSE(t[0].errorAtEof);
}

TEST(Lexer, TripleQuotedStringIsRawAndMultiLine) {
    auto t = rawTokens("\"\"\"a\\n\n  \"b\"\"\"\"\" x");
    EXPECT_EQ(t[0].kind, TokenKind::StringLit);
    EXPECT_EQ(t[0].stringValue, "a\\n\n  \"b\"\"");  // extra quotes belong to the content
    EXPECT_EQ(t[1].text, "x");
}

TEST(Lexer, UnterminatedTripleQuoteIsIncompleteInput) {
    auto t = rawTokens("\"\"\"abc");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_TRUE(t[0].errorAtEof);
}

TEST(Lexer, InterpolatedStringIsStructured) {
    auto t = rawTokens(R"(s"a $x b ${y + 1} $$")");
    ASSERT_EQ(t[0].kind, TokenKind::InterpolatedString);
    EXPECT_EQ(t[0].text, "s");
    ASSERT_EQ(t[0].parts.size(), 5u);
    EXPECT_FALSE(t[0].parts[0].isHole); EXPECT_EQ(t[0].parts[0].text, "a ");
    EXPECT_TRUE(t[0].parts[1].isHole);  EXPECT_EQ(t[0].parts[1].text, "x");
    EXPECT_EQ(t[0].parts[2].text, " b ");
    EXPECT_TRUE(t[0].parts[3].isHole);  EXPECT_EQ(t[0].parts[3].text, "y + 1");
    EXPECT_EQ(t[0].parts[4].text, " $");
}

TEST(Lexer, InterpolationHoleWithNestedString) {
    auto t = rawTokens(R"(s"a${"nested"}b")");
    ASSERT_EQ(t[0].kind, TokenKind::InterpolatedString);
    ASSERT_EQ(t[0].parts.size(), 3u);
    EXPECT_FALSE(t[0].parts[0].isHole); EXPECT_EQ(t[0].parts[0].text, "a");
    EXPECT_TRUE(t[0].parts[1].isHole);  EXPECT_EQ(t[0].parts[1].text, "\"nested\"");
    EXPECT_FALSE(t[0].parts[2].isHole); EXPECT_EQ(t[0].parts[2].text, "b");
}

TEST(Lexer, InterpolationHoleBracesInsideNestedStringDoNotAffectDepth) {
    // The `}` inside `"}"` and the `{` inside `"{"` are part of nested
    // string literals and must not be mistaken for the hole's own braces.
    auto t = rawTokens(R"(s"${ if x then "}" else "{" }")");
    ASSERT_EQ(t[0].kind, TokenKind::InterpolatedString);
    ASSERT_EQ(t[0].parts.size(), 1u);
    EXPECT_TRUE(t[0].parts[0].isHole);
    EXPECT_EQ(t[0].parts[0].text, " if x then \"}\" else \"{\" ");
}

TEST(Lexer, NestedBlockComments) {
    EXPECT_EQ(kinds(rawTokens("a /* x /* y */ z */ b // tail\nc")),
              "Identifier Identifier Identifier EOF");
    auto t = rawTokens("/* open /* inner */");
    EXPECT_EQ(t[0].kind, TokenKind::Error);
    EXPECT_TRUE(t[0].errorAtEof);
}

TEST(Lexer, PositionsIndentationAndFirstOnLine) {
    auto t = rawTokens("val x =\n    1 +\n  /* c */ 2");
    EXPECT_EQ(t[0].pos.line, 1); EXPECT_TRUE(t[0].firstOnLine);  EXPECT_EQ(t[0].lineIndent, 0);
    EXPECT_FALSE(t[1].firstOnLine);
    EXPECT_EQ(t[3].pos.line, 2); EXPECT_TRUE(t[3].firstOnLine);  EXPECT_EQ(t[3].lineIndent, 4);
    EXPECT_FALSE(t[4].firstOnLine);
    // A comment before the first token does not change the line's indentation.
    EXPECT_EQ(t[5].pos.line, 3); EXPECT_TRUE(t[5].firstOnLine);  EXPECT_EQ(t[5].lineIndent, 2);
    EXPECT_EQ(t[5].pos.column, 11);
}

TEST(Lexer, MixedTabsAndSpacesInIndentationIsAnError) {
    auto t = rawTokens("a\n\tb\n  c");
    EXPECT_EQ(t.back().kind, TokenKind::Error);
}

TEST(Lexer, Punctuation) {
    EXPECT_EQ(kinds(rawTokens("( ) [ ] { } , ; .")),
              "LParen RParen LBracket RBracket LBrace RBrace Comma Semicolon Dot EOF");
}
