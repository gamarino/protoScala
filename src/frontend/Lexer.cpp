#include "frontend/Lexer.h"
#include "frontend/UnicodeLetters.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace protoScala {

namespace {

// Decode the UTF-8 sequence starting at s[pos]. Returns its length in bytes
// (1 to 4) and stores the code point in *cp, or returns 0 when the bytes are
// not well-formed UTF-8: an invalid lead or stray continuation byte, a
// truncated sequence, an overlong encoding, a surrogate or a value beyond
// U+10FFFF.
std::size_t decodeUtf8(const std::string& s, std::size_t pos, char32_t* cp) {
    const unsigned b0 = static_cast<unsigned char>(s[pos]);
    std::size_t len = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        len = 2; value = b0 & 0x1F; minimum = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3; value = b0 & 0x0F; minimum = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4; value = b0 & 0x07; minimum = 0x10000;
    } else {
        return 0;
    }
    if (len > s.size() - pos) return 0;
    for (std::size_t i = 1; i < len; ++i) {
        const unsigned b = static_cast<unsigned char>(s[pos + i]);
        if ((b & 0xC0) != 0x80) return 0;
        value = (value << 6) | (b & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        return 0;
    }
    *cp = value;
    return len;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) { out += static_cast<char>(cp); return; }
    if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
        return;
    }
    out += static_cast<char>(0x80 | (cp & 0x3F));
}

const std::unordered_map<std::string, TokenKind>& keywords() {
    static const std::unordered_map<std::string, TokenKind> table = {
        {"abstract", TokenKind::KwAbstract}, {"case", TokenKind::KwCase},
        {"catch", TokenKind::KwCatch},       {"class", TokenKind::KwClass},
        {"def", TokenKind::KwDef},           {"do", TokenKind::KwDo},
        {"else", TokenKind::KwElse},         {"enum", TokenKind::KwEnum},
        {"export", TokenKind::KwExport},     {"extends", TokenKind::KwExtends},
        {"false", TokenKind::KwFalse},       {"final", TokenKind::KwFinal},
        {"finally", TokenKind::KwFinally},   {"for", TokenKind::KwFor},
        {"given", TokenKind::KwGiven},       {"if", TokenKind::KwIf},
        {"implicit", TokenKind::KwImplicit}, {"import", TokenKind::KwImport},
        {"lazy", TokenKind::KwLazy},         {"match", TokenKind::KwMatch},
        {"new", TokenKind::KwNew},           {"null", TokenKind::KwNull},
        {"object", TokenKind::KwObject},     {"override", TokenKind::KwOverride},
        {"package", TokenKind::KwPackage},   {"private", TokenKind::KwPrivate},
        {"protected", TokenKind::KwProtected}, {"return", TokenKind::KwReturn},
        {"sealed", TokenKind::KwSealed},     {"super", TokenKind::KwSuper},
        {"then", TokenKind::KwThen},         {"this", TokenKind::KwThis},
        {"throw", TokenKind::KwThrow},       {"trait", TokenKind::KwTrait},
        {"true", TokenKind::KwTrue},         {"try", TokenKind::KwTry},
        {"type", TokenKind::KwType},         {"val", TokenKind::KwVal},
        {"var", TokenKind::KwVar},           {"while", TokenKind::KwWhile},
        {"with", TokenKind::KwWith},         {"yield", TokenKind::KwYield},
    };
    return table;  // immutable data, not a ProtoObject: a static is fine
}

TokenKind reservedOperator(const std::string& op) {
    if (op == "=")   return TokenKind::Equals;
    if (op == "=>")  return TokenKind::Arrow;
    if (op == "?=>") return TokenKind::CtxArrow;
    if (op == "=>>") return TokenKind::TypeLambdaArrow;
    if (op == "<-")  return TokenKind::LeftArrow;
    if (op == "<:")  return TokenKind::Subtype;
    if (op == ">:")  return TokenKind::Supertype;
    if (op == "#")   return TokenKind::Hash;
    if (op == "@")   return TokenKind::At;
    if (op == ":")   return TokenKind::Colon;
    return TokenKind::Identifier;
}

// Whether the ASCII or Unicode code point `cp` may appear in an alphanumeric
// identifier.
bool isIdentCodePoint(char32_t cp) {
    if (cp < 0x80) {
        return std::isalnum(static_cast<int>(cp)) || cp == '_' || cp == '$';
    }
    return isSymbolLetter(cp);
}

} // namespace

const char* tokenKindName(TokenKind k) {
    switch (k) {
        case TokenKind::IntLit:             return "IntLit";
        case TokenKind::FloatLit:           return "FloatLit";
        case TokenKind::CharLit:            return "CharLit";
        case TokenKind::StringLit:          return "StringLit";
        case TokenKind::InterpolatedString: return "InterpolatedString";
        case TokenKind::Identifier:         return "Identifier";
        case TokenKind::KwAbstract:         return "KwAbstract";
        case TokenKind::KwCase:             return "KwCase";
        case TokenKind::KwCatch:            return "KwCatch";
        case TokenKind::KwClass:            return "KwClass";
        case TokenKind::KwDef:              return "KwDef";
        case TokenKind::KwDo:               return "KwDo";
        case TokenKind::KwElse:             return "KwElse";
        case TokenKind::KwEnum:             return "KwEnum";
        case TokenKind::KwExport:           return "KwExport";
        case TokenKind::KwExtends:          return "KwExtends";
        case TokenKind::KwFalse:            return "KwFalse";
        case TokenKind::KwFinal:            return "KwFinal";
        case TokenKind::KwFinally:          return "KwFinally";
        case TokenKind::KwFor:              return "KwFor";
        case TokenKind::KwGiven:            return "KwGiven";
        case TokenKind::KwIf:               return "KwIf";
        case TokenKind::KwImplicit:         return "KwImplicit";
        case TokenKind::KwImport:           return "KwImport";
        case TokenKind::KwLazy:             return "KwLazy";
        case TokenKind::KwMatch:            return "KwMatch";
        case TokenKind::KwNew:              return "KwNew";
        case TokenKind::KwNull:             return "KwNull";
        case TokenKind::KwObject:           return "KwObject";
        case TokenKind::KwOverride:         return "KwOverride";
        case TokenKind::KwPackage:          return "KwPackage";
        case TokenKind::KwPrivate:          return "KwPrivate";
        case TokenKind::KwProtected:        return "KwProtected";
        case TokenKind::KwReturn:           return "KwReturn";
        case TokenKind::KwSealed:           return "KwSealed";
        case TokenKind::KwSuper:            return "KwSuper";
        case TokenKind::KwThen:             return "KwThen";
        case TokenKind::KwThis:             return "KwThis";
        case TokenKind::KwThrow:            return "KwThrow";
        case TokenKind::KwTrait:            return "KwTrait";
        case TokenKind::KwTrue:             return "KwTrue";
        case TokenKind::KwTry:              return "KwTry";
        case TokenKind::KwType:             return "KwType";
        case TokenKind::KwVal:              return "KwVal";
        case TokenKind::KwVar:              return "KwVar";
        case TokenKind::KwWhile:            return "KwWhile";
        case TokenKind::KwWith:             return "KwWith";
        case TokenKind::KwYield:            return "KwYield";
        case TokenKind::LParen:             return "LParen";
        case TokenKind::RParen:             return "RParen";
        case TokenKind::LBracket:           return "LBracket";
        case TokenKind::RBracket:           return "RBracket";
        case TokenKind::LBrace:             return "LBrace";
        case TokenKind::RBrace:             return "RBrace";
        case TokenKind::Comma:              return "Comma";
        case TokenKind::Semicolon:          return "Semicolon";
        case TokenKind::Dot:                return "Dot";
        case TokenKind::Colon:              return "Colon";
        case TokenKind::Equals:             return "Equals";
        case TokenKind::Arrow:              return "Arrow";
        case TokenKind::CtxArrow:           return "CtxArrow";
        case TokenKind::TypeLambdaArrow:    return "TypeLambdaArrow";
        case TokenKind::LeftArrow:          return "LeftArrow";
        case TokenKind::Subtype:            return "Subtype";
        case TokenKind::Supertype:          return "Supertype";
        case TokenKind::Hash:               return "Hash";
        case TokenKind::At:                 return "At";
        case TokenKind::Underscore:         return "Underscore";
        case TokenKind::ColonEol:           return "ColonEol";
        case TokenKind::Newline:            return "Newline";
        case TokenKind::Indent:             return "Indent";
        case TokenKind::Outdent:            return "Outdent";
        case TokenKind::EndMarker:          return "EndMarker";
        case TokenKind::EndOfFile:          return "EOF";
        case TokenKind::Error:              return "Error";
    }
    return "?";
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {
    // A UTF-8 byte-order mark at the start of the source is not part of it.
    if (source_.compare(0, 3, "\xEF\xBB\xBF") == 0) source_.erase(0, 3);
    computeLineIndents();
}

bool Lexer::isOpChar(char c) {
    return c != '\0' && std::strchr("!#%&*+-/:<=>?@\\^|~", c) != nullptr;
}

// Columns count code points: a UTF-8 continuation byte does not advance the
// column.
void Lexer::advance() {
    if (eof()) return;
    const unsigned char c = static_cast<unsigned char>(source_[pos_]);
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else if ((c & 0xC0) != 0x80) {
        ++column_;
    }
    ++pos_;
}

// Leading-blank width of every line, and whether tabs or spaces were used.
void Lexer::computeLineIndents() {
    std::size_t i = 0;
    while (true) {
        int width = 0; bool tab = false, space = false;
        while (i < source_.size() && (source_[i] == ' ' || source_[i] == '\t')) {
            (source_[i] == '\t' ? tab : space) = true;
            ++width; ++i;
        }
        lineIndent_.push_back(width);
        lineHasTab_.push_back(tab);
        lineHasSpace_.push_back(space);
        while (i < source_.size() && source_[i] != '\n') ++i;
        if (i >= source_.size()) break;
        ++i;  // past '\n'
    }
}

Token Lexer::make(TokenKind k, SourcePos start, std::string text) {
    Token t;
    t.kind = k;
    t.text = std::move(text);
    t.pos = start;
    t.end = SourcePos{line_, column_};
    t.lineIndent = lineIndent_[start.line - 1];
    t.firstOnLine = (start.line != lastTokenLine_);
    return t;
}

Token Lexer::error(const std::string& msg, SourcePos at, bool atEof) {
    Token t;
    t.kind = TokenKind::Error;
    t.text = msg;
    t.pos = at;
    t.end = SourcePos{line_, column_};
    t.errorAtEof = atEof;
    if (at.line >= 1 && at.line <= static_cast<int>(lineIndent_.size())) {
        t.lineIndent = lineIndent_[at.line - 1];
    }
    t.firstOnLine = (at.line != lastTokenLine_);
    return t;
}

Token Lexer::next() {
    if (done_) { Token t; t.kind = TokenKind::EndOfFile; t.pos = {line_, column_}; return t; }
    Token trivia = skipTrivia();
    if (trivia.kind == TokenKind::Error) { done_ = true; return trivia; }
    SourcePos start{line_, column_};
    if (eof()) {
        done_ = true;
        Token t = make(TokenKind::EndOfFile, start, "");
        t.firstOnLine = true;
        return t;
    }
    // Indentation consistency is checked only on lines that hold tokens.
    if (start.line != lastTokenLine_) {
        if (lineHasTab_[start.line - 1]) sawTabIndent_ = true;
        if (lineHasSpace_[start.line - 1]) sawSpaceIndent_ = true;
        if (sawTabIndent_ && sawSpaceIndent_) {
            done_ = true;
            return error("indentation mixes tabs and spaces", start);
        }
    }
    Token t;
    const char c = cur();
    switch (c) {
        case '(': advance(); t = make(TokenKind::LParen, start, "("); break;
        case ')': advance(); t = make(TokenKind::RParen, start, ")"); break;
        case '[': advance(); t = make(TokenKind::LBracket, start, "["); break;
        case ']': advance(); t = make(TokenKind::RBracket, start, "]"); break;
        case '{': advance(); t = make(TokenKind::LBrace, start, "{"); break;
        case '}': advance(); t = make(TokenKind::RBrace, start, "}"); break;
        case ',': advance(); t = make(TokenKind::Comma, start, ","); break;
        case ';': advance(); t = make(TokenKind::Semicolon, start, ";"); break;
        case '`': t = lexBackquoted(); break;
        case '"': t = lexString(); break;
        case '\'': t = lexChar(); break;
        case '.':
            if (std::isdigit(static_cast<unsigned char>(peekChar()))) { t = lexNumber(); break; }
            advance(); t = make(TokenKind::Dot, start, "."); break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) t = lexNumber();
            else if (isOpChar(c)) t = lexOperator();
            else if (identCharLength(pos_) > 0) t = lexIdentifierOrKeyword();
            else t = error(std::string("illegal character '") + c + "'", start);
    }
    if (t.kind == TokenKind::Error) done_ = true;
    lastTokenLine_ = start.line;
    return t;
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> out;
    while (true) {
        out.push_back(next());
        const TokenKind k = out.back().kind;
        if (k == TokenKind::EndOfFile || k == TokenKind::Error) return out;
    }
}

// Blanks, newlines, `//` comments and nested `/* */` comments.
Token Lexer::skipTrivia() {
    while (!eof()) {
        const char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && peekChar() == '/') {
            while (!eof() && cur() != '\n') advance();
            continue;
        }
        if (c == '/' && peekChar() == '*') {
            const SourcePos start{line_, column_};
            int depth = 0;
            do {
                if (cur() == '/' && peekChar() == '*') { depth++; advance(); advance(); }
                else if (cur() == '*' && peekChar() == '/') { depth--; advance(); advance(); }
                else advance();
            } while (depth > 0 && !eof());
            if (depth > 0) return error("unclosed comment", start, /*atEof=*/true);
            continue;
        }
        break;
    }
    Token none; none.kind = TokenKind::EndOfFile;
    return none;
}

// 0 when the byte at `at` does not start an identifier character; otherwise
// its length in bytes (1 for ASCII, 1-4 for a well-formed UTF-8 sequence
// whose code point is a letter, combining mark or decimal digit).
std::size_t Lexer::identCharLength(std::size_t at) const {
    if (at >= source_.size()) return 0;
    const unsigned char c = static_cast<unsigned char>(source_[at]);
    if (c < 0x80) {
        return isIdentCodePoint(c) ? 1 : 0;
    }
    char32_t cp = 0;
    const std::size_t len = decodeUtf8(source_, at, &cp);
    if (len == 0) return 0;
    return isIdentCodePoint(cp) ? len : 0;
}

// Alphanumeric identifiers: a letter, `_` or `$`, then letters, digits, `_`,
// `$`; after an `_`, the rest may be operator characters (`unary_-`, `x_+`).
Token Lexer::lexIdentifierOrKeyword() {
    const SourcePos start{line_, column_};
    std::string text;
    bool lastWasUnderscore = false;
    while (!eof()) {
        if (lastWasUnderscore && isOpChar(cur())) {
            while (!eof() && isOpChar(cur())) { text += cur(); advance(); }
            break;
        }
        const std::size_t len = identCharLength(pos_);
        if (len == 0) break;
        lastWasUnderscore = (cur() == '_');
        text.append(source_, pos_, len);
        for (std::size_t i = 0; i < len; ++i) advance();
    }
    // An identifier immediately followed by `"` is a string interpolator.
    if (cur() == '"') return lexInterpolated(text, start);
    if (text == "_") return make(TokenKind::Underscore, start, text);
    const auto& kw = keywords();
    if (auto it = kw.find(text); it != kw.end()) return make(it->second, start, text);
    return make(TokenKind::Identifier, start, text);
}

// Operator identifiers: a maximal run of operator characters, stopping before
// a `//` or `/*` that would start a comment.
Token Lexer::lexOperator() {
    const SourcePos start{line_, column_};
    std::string text;
    while (!eof() && isOpChar(cur())) {
        if (!text.empty() && cur() == '/' && (peekChar() == '/' || peekChar() == '*')) break;
        text += cur();
        advance();
    }
    const TokenKind reserved = reservedOperator(text);
    Token t = make(reserved, start, text);
    t.isOperator = (reserved == TokenKind::Identifier);
    return t;
}

// Consume ``` ` ``` and read until the next ``` ` ``` on the same line.
Token Lexer::lexBackquoted() {
    const SourcePos start{line_, column_};
    advance();  // consume opening `
    std::string text;
    while (!eof() && cur() != '`' && cur() != '\n') {
        text += cur();
        advance();
    }
    if (text.empty() || eof() || cur() != '`') {
        return error("unclosed quoted identifier", start);
    }
    advance();  // consume closing `
    Token t = make(TokenKind::Identifier, start, text);
    t.backquoted = true;
    return t;
}

// One escape sequence after the backslash. `cur()` is the character right
// after `\`.
std::string Lexer::readEscape(std::string& out) {
    const char c = cur();
    switch (c) {
        case 'b': out += '\b'; advance(); return "";
        case 't': out += '\t'; advance(); return "";
        case 'n': out += '\n'; advance(); return "";
        case 'f': out += '\f'; advance(); return "";
        case 'r': out += '\r'; advance(); return "";
        case '"': out += '"';  advance(); return "";
        case '\'': out += '\''; advance(); return "";
        case '\\': out += '\\'; advance(); return "";
        case 'u': {
            while (!eof() && cur() == 'u') advance();  // one or more u's
            char32_t cp = 0;
            for (int i = 0; i < 4; ++i) {
                const char h = cur();
                int digit;
                if (h >= '0' && h <= '9') digit = h - '0';
                else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                else return "invalid unicode escape";
                cp = static_cast<char32_t>((cp << 4) | static_cast<unsigned>(digit));
                advance();
            }
            appendUtf8(out, cp);
            return "";
        }
        default:
            if (c >= '0' && c <= '9') {
                return "octal escape literals are unsupported: use \\u escapes";
            }
            return "invalid escape character";
    }
}

// `0x`/`0X` -> base 16, `0b`/`0B` -> base 2, else base 10 (with a leading-zero
// check). Digits may contain `_` between digits. Base 10 only: a fractional
// part, an exponent and the `f`/`F`/`d`/`D` (float) suffixes are recognised.
// The `l`/`L` (long, accepted and ignored) suffix is recognised for every
// base (docs/LANGUAGE.md §1); a hex/binary digit run absorbs any of
// `f`/`F`/`d`/`D` that are valid digits in that base before this suffix
// check ever sees them (e.g. `0xFFf` is the hex value 0xFFF, not a float).
Token Lexer::lexNumber() {
    const SourcePos start{line_, column_};
    int base = 10;
    std::string digits;
    bool isFloat = false;

    if (cur() == '0' && (peekChar() == 'x' || peekChar() == 'X')) {
        base = 16;
        advance(); advance();
    } else if (cur() == '0' && (peekChar() == 'b' || peekChar() == 'B')) {
        base = 2;
        advance(); advance();
    } else if (cur() == '0' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
        return error("decimal integer literals may not have a leading zero", start);
    }

    auto isBaseDigit = [&](char c) {
        if (base == 16) return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        if (base == 2)  return c == '0' || c == '1';
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    };

    bool lastWasUnderscore = false;
    while (!eof() && (isBaseDigit(cur()) || cur() == '_')) {
        if (cur() == '_') {
            lastWasUnderscore = true;
        } else {
            digits += cur();
            lastWasUnderscore = false;
        }
        advance();
    }
    if (lastWasUnderscore) return error("trailing '_' in number literal", start);

    if (base == 10) {
        if (cur() == '.' && std::isdigit(static_cast<unsigned char>(peekChar()))) {
            isFloat = true;
            digits += '.';
            advance();
            bool lastUnd = false;
            while (!eof() && (std::isdigit(static_cast<unsigned char>(cur())) || cur() == '_')) {
                if (cur() == '_') lastUnd = true;
                else { digits += cur(); lastUnd = false; }
                advance();
            }
            if (lastUnd) return error("trailing '_' in number literal", start);
        }
        if (cur() == 'e' || cur() == 'E') {
            const char c1 = peekChar(1);
            const bool hasSign = (c1 == '+' || c1 == '-');
            const char digitCandidate = hasSign ? peekChar(2) : c1;
            if (std::isdigit(static_cast<unsigned char>(digitCandidate))) {
                isFloat = true;
                digits += 'e';
                advance();  // past 'e'/'E'
                if (hasSign) { digits += cur(); advance(); }
                bool lastUnd = false;
                while (!eof() && (std::isdigit(static_cast<unsigned char>(cur())) || cur() == '_')) {
                    if (cur() == '_') lastUnd = true;
                    else { digits += cur(); lastUnd = false; }
                    advance();
                }
                if (lastUnd) return error("trailing '_' in number literal", start);
            }
        }
        if (cur() == 'f' || cur() == 'F' || cur() == 'd' || cur() == 'D') {
            isFloat = true;
            advance();
        }
    }
    if (!isFloat && (cur() == 'l' || cur() == 'L')) {
        advance();  // accepted and ignored, for any base (docs/LANGUAGE.md §1)
    }

    if (identCharLength(pos_) > 0) {
        return error("malformed number literal", start);
    }

    Token t = make(isFloat ? TokenKind::FloatLit : TokenKind::IntLit, start, digits);
    if (isFloat) {
        t.floatValue = std::strtod(digits.c_str(), nullptr);
    } else {
        errno = 0;
        const long long value = std::strtoll(digits.c_str(), nullptr, base);
        t.digits = digits;
        t.base = base;
        if (errno == ERANGE) {
            t.fitsLong = false;
        } else {
            t.intValue = value;
        }
    }
    return t;
}

// After `'`: `\` -> readEscape; otherwise one UTF-8 code point; then a
// closing `'` is required.
Token Lexer::lexChar() {
    const SourcePos start{line_, column_};
    advance();  // consume opening '
    if (cur() == '\'') {
        advance();
        return error("empty character literal", start);
    }
    char32_t cp = 0;
    if (cur() == '\\') {
        advance();
        std::string encoded;
        const std::string msg = readEscape(encoded);
        if (!msg.empty()) return error(msg, start);
        if (encoded.empty() || decodeUtf8(encoded, 0, &cp) == 0) {
            return error("invalid escape character", start);
        }
    } else {
        if (eof() || cur() == '\n') return error("unclosed character literal", start);
        const std::size_t len = decodeUtf8(source_, pos_, &cp);
        if (len == 0) return error("invalid UTF-8 in character literal", start);
        for (std::size_t i = 0; i < len; ++i) advance();
    }
    if (cur() != '\'') {
        if (isIdentCodePoint(cp)) {
            return error("symbol literals are not supported in Scala 3", start);
        }
        return error("unclosed character literal", start);
    }
    advance();  // consume closing '
    Token t = make(TokenKind::CharLit, start, "");
    t.charValue = cp;
    return t;
}

// `"..."` (escapes via readEscape) or `"""..."""` (raw, may span lines). A
// run of k >= 3 quotes closes a triple-quoted string with its last three;
// the first k-3 belong to the content.
Token Lexer::lexString() {
    const SourcePos start{line_, column_};
    advance();  // consume opening "
    if (cur() == '"' && peekChar() == '"') {
        advance(); advance();  // consume the other two "
        std::string content;
        while (true) {
            if (eof()) return error("unclosed multi-line string literal", start, true);
            if (cur() == '"') {
                int run = 0;
                while (!eof() && cur() == '"') { ++run; advance(); }
                if (run >= 3) {
                    content.append(static_cast<std::size_t>(run - 3), '"');
                    break;
                }
                content.append(static_cast<std::size_t>(run), '"');
                if (eof()) return error("unclosed multi-line string literal", start, true);
                continue;
            }
            content += cur();
            advance();
        }
        Token t = make(TokenKind::StringLit, start, content);
        t.stringValue = content;
        return t;
    }
    std::string value;
    while (true) {
        if (eof() || cur() == '\n') return error("unclosed string literal", start, false);
        if (cur() == '"') { advance(); break; }
        if (cur() == '\\') {
            const SourcePos escPos{line_, column_};
            advance();
            const std::string msg = readEscape(value);
            if (!msg.empty()) return error(msg, escPos);
            continue;
        }
        value += cur();
        advance();
    }
    Token t = make(TokenKind::StringLit, start, value);
    t.stringValue = value;
    return t;
}

// `id"..."` or `id"""..."""`. `$$` -> literal `$`; `$name` -> hole with the
// identifier text; `${` -> hole with the source text up to the matching `}`
// (nested braces are counted, nested string literals are skipped verbatim).
// Escapes are processed in literal parts unless `id == "raw"`.
Token Lexer::lexInterpolated(std::string interpolator, SourcePos start) {
    advance();  // consume opening "
    bool triple = false;
    if (cur() == '"' && peekChar() == '"') {
        triple = true;
        advance(); advance();
    }
    const bool raw = (interpolator == "raw");
    std::vector<InterpolationPart> parts;
    std::string literal;
    SourcePos literalStart{line_, column_};

    auto flushLiteral = [&]() {
        if (!literal.empty()) {
            InterpolationPart p;
            p.isHole = false;
            p.text = literal;
            p.pos = literalStart;
            parts.push_back(std::move(p));
        }
        literal.clear();
    };

    const std::string unterminatedMsg =
        triple ? "unclosed multi-line string literal" : "unclosed string literal";

    while (true) {
        if (eof()) return error(unterminatedMsg, start, triple);
        if (!triple && cur() == '\n') return error("unclosed string literal", start, false);

        if (triple && cur() == '"') {
            int run = 0;
            while (!eof() && cur() == '"') { ++run; advance(); }
            if (run >= 3) {
                literal.append(static_cast<std::size_t>(run - 3), '"');
                break;
            }
            literal.append(static_cast<std::size_t>(run), '"');
            if (eof()) return error(unterminatedMsg, start, true);
            continue;
        }
        if (!triple && cur() == '"') { advance(); break; }

        if (cur() == '$') {
            const SourcePos dollarPos{line_, column_};
            advance();
            if (cur() == '$') { literal += '$'; advance(); continue; }
            if (cur() == '{') {
                flushLiteral();
                advance();  // consume {
                const SourcePos exprStart{line_, column_};
                std::string exprText;
                int depth = 1;
                while (true) {
                    if (eof()) return error(unterminatedMsg, start, triple);
                    const char ec = cur();
                    if (ec == '{') { depth++; exprText += ec; advance(); continue; }
                    if (ec == '}') {
                        depth--;
                        advance();
                        if (depth == 0) break;
                        exprText += ec;
                        continue;
                    }
                    if (ec == '"') {
                        // Skip a nested string literal verbatim (its
                        // characters belong to the hole's source text).
                        const bool nestedTriple = (peekChar(1) == '"' && peekChar(2) == '"');
                        exprText += ec;
                        advance();
                        if (nestedTriple) {
                            exprText += cur(); advance();
                            exprText += cur(); advance();
                            while (true) {
                                if (eof()) return error("unclosed multi-line string literal", start, true);
                                if (cur() == '"') {
                                    int r = 0;
                                    while (!eof() && cur() == '"') { exprText += '"'; ++r; advance(); }
                                    if (r >= 3) break;
                                    continue;
                                }
                                exprText += cur();
                                advance();
                            }
                        } else {
                            while (true) {
                                if (eof() || cur() == '\n') return error("unclosed string literal", start, false);
                                if (cur() == '"') { exprText += cur(); advance(); break; }
                                if (cur() == '\\') {
                                    exprText += cur(); advance();
                                    if (!eof()) { exprText += cur(); advance(); }
                                    continue;
                                }
                                exprText += cur();
                                advance();
                            }
                        }
                        continue;
                    }
                    exprText += ec;
                    advance();
                }
                InterpolationPart p;
                p.isHole = true;
                p.text = exprText;
                p.pos = exprStart;
                parts.push_back(std::move(p));
                literalStart = SourcePos{line_, column_};
                continue;
            }
            const std::size_t idLen = identCharLength(pos_);
            if (idLen == 0) {
                return error("invalid string interpolation: $$, $ident or ${expr} expected", dollarPos);
            }
            flushLiteral();
            const SourcePos idStart{line_, column_};
            std::string name;
            while (true) {
                const std::size_t len = identCharLength(pos_);
                if (len == 0) break;
                name.append(source_, pos_, len);
                for (std::size_t i = 0; i < len; ++i) advance();
            }
            InterpolationPart p;
            p.isHole = true;
            p.text = name;
            p.pos = idStart;
            parts.push_back(std::move(p));
            literalStart = SourcePos{line_, column_};
            continue;
        }

        if (!triple && !raw && cur() == '\\') {
            const SourcePos escPos{line_, column_};
            advance();
            const std::string msg = readEscape(literal);
            if (!msg.empty()) return error(msg, escPos);
            continue;
        }

        if (literal.empty()) literalStart = SourcePos{line_, column_};
        literal += cur();
        advance();
    }
    flushLiteral();
    Token t = make(TokenKind::InterpolatedString, start, interpolator);
    t.parts = std::move(parts);
    return t;
}

} // namespace protoScala
