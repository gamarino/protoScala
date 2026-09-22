/*
 * Lexer — Scala 3 source text to raw tokens (no layout tokens).
 *
 * Hand-written, one-character lookahead, same shape as protoClojure's
 * src/reader/Lexer.cpp (advance() counts columns in code points; UTF-8 is
 * decoded with the same decodeUtf8 helper). The Lexer never throws: a
 * lexical error is an Error token and ends the stream.
 */
#pragma once
#include "frontend/Token.h"

#include <string>
#include <vector>

namespace protoScala {

class Lexer {
public:
    explicit Lexer(std::string source);

    // The next raw token. After EndOfFile or Error, keeps returning EndOfFile.
    Token next();

    // Every token up to and including the first EndOfFile or Error.
    std::vector<Token> tokenizeAll();

private:
    std::string source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    int lastTokenLine_ = 0;          // line of the previous token (0: none yet)
    bool done_ = false;
    std::vector<int> lineIndent_;    // leading-blank width per line (index line-1)
    std::vector<bool> lineHasTab_;   // leading blanks of that line contain a tab
    std::vector<bool> lineHasSpace_; // ... contain a space
    bool sawTabIndent_ = false;
    bool sawSpaceIndent_ = false;

    bool eof() const { return pos_ >= source_.size(); }
    char cur() const { return eof() ? '\0' : source_[pos_]; }
    char peekChar(std::size_t k = 1) const {
        return pos_ + k < source_.size() ? source_[pos_ + k] : '\0';
    }
    void advance();
    void computeLineIndents();
    // Skips blanks and comments. Returns an Error token for an unterminated
    // block comment, else a token with kind EndOfFile meaning "nothing to report".
    Token skipTrivia();

    Token make(TokenKind k, SourcePos start, std::string text);
    Token error(const std::string& msg, SourcePos at, bool atEof = false);

    Token lexIdentifierOrKeyword();
    Token lexOperator();
    Token lexBackquoted();
    Token lexNumber();
    Token lexChar();
    Token lexString();          // "..." or """..."""
    Token lexInterpolated(std::string interpolator, SourcePos start);
    // Reads one escape after the backslash (current char is the one after
    // `\`); appends the UTF-8 encoding to `out`. Returns an error message or "".
    std::string readEscape(std::string& out);
    std::size_t identCharLength(std::size_t at) const;  // 0 when not an ident char
    static bool isOpChar(char c);
};

} // namespace protoScala
