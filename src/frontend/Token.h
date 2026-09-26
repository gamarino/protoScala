/*
 * Token — one lexical element of Scala 3 source.
 *
 * The raw Lexer produces every kind except the layout kinds (Newline, Indent,
 * Outdent, EndMarker, ColonEol), which Layout inserts (DESIGN §3.2). Soft
 * keywords (as derives end extension infix inline opaque open transparent
 * using) are Identifier tokens; the parser and Layout test their text.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace protoScala {

struct SourcePos {
    int line = 1;    // 1-based
    int column = 1;  // 1-based, counted in code points
};

enum class TokenKind : uint8_t {
    // Literals
    IntLit, FloatLit, CharLit, StringLit, InterpolatedString,
    // Names (alphanumeric, operator, mixed, backquoted; soft keywords)
    Identifier,
    // Hard keywords (docs/LANGUAGE.md §1, plus `this`)
    KwAbstract, KwCase, KwCatch, KwClass, KwDef, KwDo, KwElse, KwEnum, KwExport,
    KwExtends, KwFalse, KwFinal, KwFinally, KwFor, KwGiven, KwIf, KwImplicit,
    KwImport, KwLazy, KwMatch, KwNew, KwNull, KwObject, KwOverride, KwPackage,
    KwPrivate, KwProtected, KwReturn, KwSealed, KwSuper, KwThen, KwThis, KwThrow,
    KwTrait, KwTrue, KwTry, KwType, KwVal, KwVar, KwWhile, KwWith, KwYield,
    // Punctuation and reserved operators
    LParen, RParen, LBracket, RBracket, LBrace, RBrace,
    Comma, Semicolon, Dot, Colon, Equals, Arrow, CtxArrow, TypeLambdaArrow,
    LeftArrow, Subtype, Supertype, Hash, At, Underscore,
    // Layout (inserted by Layout, never by the Lexer)
    ColonEol, Newline, Indent, Outdent, EndMarker,
    EndOfFile, Error,
};

// One piece of an interpolated string: literal text (escapes already
// processed, except for the `raw` interpolator) or the source text of a
// `$name` / `${expr}` hole.
struct InterpolationPart {
    bool isHole = false;
    std::string text;
    SourcePos pos;
};

struct Token {
    TokenKind kind = TokenKind::Error;
    // Identifier: the name (without backquotes). Keywords and punctuation:
    // their spelling. Literals: the source spelling. InterpolatedString: the
    // interpolator (`s`, `f`, `raw`, ...). EndMarker: the designator (`if`,
    // `while`, a definition name). Error: the message.
    std::string text;
    SourcePos pos;             // first character
    SourcePos end;             // one past the last character
    int  lineIndent = 0;       // width of the leading blanks of the line `pos` is on
    bool firstOnLine = false;  // only blanks and comments precede it on its line
    // A line holding nothing but whitespace lies between this token and the
    // previous one. Scala 3's leading-infix rule needs it: a blank line ends
    // the expression, so the operator that starts the next line starts a new
    // statement (dotty `Scanners.pastBlankLine`). A comment-only line is *not*
    // blank, exactly as in dotty.
    bool pastBlankLine = false;
    bool backquoted = false;
    bool isOperator = false;   // Identifier made only of operator characters
    bool errorAtEof = false;   // Error: the input ended inside a construct

    // IntLit: the value when it fits a long long; otherwise `digits` (in
    // `base`, without `_` or suffix) is the exact literal.
    long long intValue = 0;
    bool fitsLong = true;
    std::string digits;
    int base = 10;
    double floatValue = 0.0;              // FloatLit
    char32_t charValue = 0;               // CharLit
    std::string stringValue;              // StringLit: decoded UTF-8
    std::vector<InterpolationPart> parts; // InterpolatedString
};

const char* tokenKindName(TokenKind k);

} // namespace protoScala
