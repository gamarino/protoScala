#include "frontend/Layout.h"
#include "frontend/Lexer.h"

#include <algorithm>

namespace protoScala {

namespace {

enum class RegionKind : uint8_t { TopLevel, Braces, Parens, Brackets, Indented };

struct Region {
    RegionKind kind;
    int width = 0;
    bool widthKnown = true;
    TokenKind opener = TokenKind::EndOfFile;  // Indented: the token that opened it
    bool condition = false;                   // Parens/Braces right after if/while/for
    // Braces opened with a token after `{` on the same line: `width` is that
    // line's width until the first line break inside, which widens it when
    // the next line is indented deeper and no region opener precedes it
    // (`{ val a = 1\n  val b = 2\n  b }`, as scalac accepts).
    bool provisional = false;
    // Constructs begun in this region that still wait for a partner keyword,
    // innermost last: KwIf (awaits then), KwThen (else), KwTry (catch or
    // finally), KwCatch (finally), KwWhile (do), KwFor (do or yield), and
    // RParen for a closed old-style condition (then, else, do or yield).
    // A same-line closer satisfies one of these before it may close an
    // Indented region, so `if d then x else y` keeps its `else`. The list is
    // cleared at every statement boundary (Newline, `;`) of the region.
    std::vector<TokenKind> pending{};
};

bool canEndStatement(TokenKind k) {
    switch (k) {
        case TokenKind::IntLit: case TokenKind::FloatLit: case TokenKind::CharLit:
        case TokenKind::StringLit: case TokenKind::InterpolatedString:
        case TokenKind::Identifier: case TokenKind::KwThis: case TokenKind::KwNull:
        case TokenKind::KwTrue: case TokenKind::KwFalse: case TokenKind::KwReturn:
        case TokenKind::KwType: case TokenKind::Underscore: case TokenKind::RParen:
        case TokenKind::RBracket: case TokenKind::RBrace: case TokenKind::EndMarker:
        case TokenKind::Outdent:
        // `import M.given` is the only statement that can END with the keyword
        // `given`: a `given` DEFINITION is refused by the parser (D3), and a
        // `using`/`given` parameter list is inside brackets. Without this the
        // next line would be read as a continuation of the import.
        case TokenKind::KwGiven:
            return true;
        default:
            return false;
    }
}

bool canBeginStatement(TokenKind k) {
    switch (k) {
        case TokenKind::KwCatch: case TokenKind::KwElse: case TokenKind::KwExtends:
        case TokenKind::KwFinally: case TokenKind::KwMatch: case TokenKind::KwWith:
        case TokenKind::KwYield: case TokenKind::KwThen: case TokenKind::KwDo:
        case TokenKind::Comma: case TokenKind::Dot: case TokenKind::Semicolon:
        case TokenKind::Colon: case TokenKind::ColonEol: case TokenKind::Equals:
        case TokenKind::Arrow: case TokenKind::CtxArrow: case TokenKind::TypeLambdaArrow:
        case TokenKind::LeftArrow: case TokenKind::Subtype: case TokenKind::Supertype:
        case TokenKind::Hash: case TokenKind::LBracket: case TokenKind::RParen:
        case TokenKind::RBracket: case TokenKind::RBrace: case TokenKind::EndOfFile:
            return false;
        default:
            return true;
    }
}

bool opensRegion(TokenKind k) {
    switch (k) {
        case TokenKind::Equals: case TokenKind::Arrow: case TokenKind::CtxArrow:
        case TokenKind::LeftArrow: case TokenKind::KwCatch: case TokenKind::KwDo:
        case TokenKind::KwElse: case TokenKind::KwFinally: case TokenKind::KwFor:
        case TokenKind::KwIf: case TokenKind::KwMatch: case TokenKind::KwReturn:
        case TokenKind::KwThen: case TokenKind::KwThrow: case TokenKind::KwTry:
        case TokenKind::KwWhile: case TokenKind::KwYield: case TokenKind::KwWith:
        case TokenKind::ColonEol:
            return true;
        default:
            return false;
    }
}

// The previous line ends with a token that says the statement continues.
bool continuesStatement(TokenKind k) {
    switch (k) {
        case TokenKind::KwThen: case TokenKind::KwElse: case TokenKind::KwDo:
        case TokenKind::KwCatch: case TokenKind::KwFinally: case TokenKind::KwYield:
        case TokenKind::KwMatch:
            return true;
        default:
            return false;
    }
}

// Whether `closer` is the partner of `opener`: used both for the Indented
// region an opener started (legacy rule: a closing keyword on the same line
// closes it) and for the pending partners of a region. RParen/RBrace stand
// for an old-style condition.
bool closesRegionOpenedBy(TokenKind closer, TokenKind opener) {
    switch (closer) {
        case TokenKind::KwThen:    return opener == TokenKind::KwIf || opener == TokenKind::RParen;
        case TokenKind::KwElse:    return opener == TokenKind::KwThen || opener == TokenKind::RParen;
        case TokenKind::KwDo:      return opener == TokenKind::KwWhile || opener == TokenKind::KwFor ||
                                          opener == TokenKind::RParen;
        case TokenKind::KwYield:   return opener == TokenKind::KwFor || opener == TokenKind::RParen ||
                                          opener == TokenKind::RBrace;
        case TokenKind::KwCatch:   return opener == TokenKind::KwTry;
        case TokenKind::KwFinally: return opener == TokenKind::KwTry || opener == TokenKind::KwCatch;
        default:                   return false;
    }
}

bool isCloser(TokenKind k) {
    return k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace;
}

bool isEndDesignator(TokenKind k) {
    switch (k) {
        case TokenKind::Identifier: case TokenKind::KwIf: case TokenKind::KwWhile:
        case TokenKind::KwFor: case TokenKind::KwMatch: case TokenKind::KwTry:
        case TokenKind::KwNew: case TokenKind::KwThis: case TokenKind::KwVal:
        case TokenKind::KwGiven:
            return true;
        default:
            return false;
    }
}

class LayoutPass {
public:
    explicit LayoutPass(const std::vector<Token>& raw) : raw_(raw) {}

    std::vector<Token> run() {
        regions_.push_back(Region{RegionKind::TopLevel, raw_.front().lineIndent});
        for (std::size_t i = 0; i < raw_.size(); ++i) {
            const Token& t = raw_[i];
            if (t.kind == TokenKind::Error)
                throw LexError(t.text, t.pos, t.errorAtEof);
            if (t.kind == TokenKind::EndOfFile) {
                while (regions_.back().kind == RegionKind::Indented) {
                    regions_.pop_back();
                    synth(TokenKind::Outdent, t);
                }
                if (regions_.size() > 1)
                    throw LexError(std::string("unclosed '") + openerName(regions_.back().kind) +
                                   "'", t.pos, /*atEof=*/true);
                out_.push_back(t);
                break;
            }
            if (t.firstOnLine && !out_.empty()) lineBreak(i);
            if (t.kind == TokenKind::Identifier && t.text == "end" && t.firstOnLine &&
                tryEndMarker(i)) {
                continue;
            }
            closeByKeyword(t);
            bool closesCondition = false;
            switch (t.kind) {
                case TokenKind::LParen:
                    regions_.push_back(Region{RegionKind::Parens, 0, true, TokenKind::EndOfFile,
                                              isConditionKeyword(prevKind_)});
                    break;
                case TokenKind::LBracket:
                    regions_.push_back(Region{RegionKind::Brackets});
                    break;
                case TokenKind::LBrace: {
                    // `{` followed by a token on the same line (`{ x =>`): the
                    // region's width is the width of this line. `{` at the end of
                    // a line: the width of the first line inside (set by lineBreak).
                    const bool sameLine = !raw_[i + 1].firstOnLine;
                    regions_.push_back(Region{RegionKind::Braces, sameLine ? t.lineIndent : 0,
                                              sameLine, TokenKind::EndOfFile,
                                              prevKind_ == TokenKind::KwFor});
                    regions_.back().provisional = sameLine;
                    break;
                }
                case TokenKind::RParen: case TokenKind::RBracket: case TokenKind::RBrace:
                    closesCondition = closeBracket(t);
                    // Phase 4: the header of a collective extension opens an
                    // indented region, which is the idiomatic Scala 3 spelling and
                    // the only one it accepts (`extension (n: Int):` is rejected
                    // by scalac). `extension` is a soft keyword, so it is
                    // recognised by text on a line of its own, and the closing
                    // bracket of its header is treated exactly as the `)` of an
                    // old-style condition is: it opens a region when the next line
                    // is more indented.
                    if (!closesCondition && extensionHeaderLine_ >= 0 &&
                        t.pos.line == extensionHeaderLine_ && raw_[i + 1].firstOnLine) {
                        closesCondition = true;
                        extensionHeaderLine_ = -1;
                    }
                    if (closesCondition) conditionClosed();
                    break;
                case TokenKind::Semicolon:
                    regions_.back().pending.clear();
                    break;
                case TokenKind::KwIf: case TokenKind::KwTry: case TokenKind::KwWhile:
                case TokenKind::KwFor:
                    regions_.back().pending.push_back(t.kind);
                    break;
                default:
                    break;
            }
            if (t.kind == TokenKind::Identifier && t.firstOnLine && !t.backquoted &&
                t.text == "extension" &&
                (raw_[i + 1].kind == TokenKind::LParen || raw_[i + 1].kind == TokenKind::LBracket))
                extensionHeaderLine_ = t.pos.line;
            Token copy = t;
            if (t.kind == TokenKind::Colon && raw_[i + 1].firstOnLine)
                copy.kind = TokenKind::ColonEol;
            out_.push_back(copy);
            prevKind_ = copy.kind;
            prevClosesCondition_ = closesCondition;
        }
        return std::move(out_);
    }

private:
    const std::vector<Token>& raw_;
    std::vector<Token> out_;
    std::vector<Region> regions_;
    TokenKind prevKind_ = TokenKind::EndOfFile;
    bool prevClosesCondition_ = false;
    // The line a collective extension's header starts on, or -1.
    int extensionHeaderLine_ = -1;

    static bool isConditionKeyword(TokenKind k) {
        return k == TokenKind::KwIf || k == TokenKind::KwWhile || k == TokenKind::KwFor;
    }

    static const char* openerName(RegionKind k) {
        switch (k) {
            case RegionKind::Parens:   return "(";
            case RegionKind::Brackets: return "[";
            case RegionKind::Braces:   return "{";
            default:                   return "?";
        }
    }

    static const char* tokenKindSpelling(TokenKind k) {
        switch (k) {
            case TokenKind::KwThen:    return "then";
            case TokenKind::KwElse:    return "else";
            case TokenKind::KwDo:      return "do";
            case TokenKind::KwCatch:   return "catch";
            case TokenKind::KwFinally: return "finally";
            case TokenKind::KwYield:   return "yield";
            case TokenKind::KwMatch:   return "match";
            default:                   return "?";
        }
    }

    void synth(TokenKind k, const Token& at) {
        if (k == TokenKind::Newline) regions_.back().pending.clear();
        Token s;
        s.kind = k;
        s.pos = at.pos;
        s.end = at.pos;
        s.lineIndent = at.lineIndent;
        out_.push_back(s);
    }

    void lineBreak(std::size_t i) {
        const Token& t = raw_[i];
        if (regions_.back().kind == RegionKind::Parens ||
            regions_.back().kind == RegionKind::Brackets)
            return;  // DESIGN §3.2: no layout tokens inside (...) and [...]
        const int w = t.lineIndent;
        const bool conditionOpener =
            prevClosesCondition_ && t.kind != TokenKind::KwThen &&
            t.kind != TokenKind::KwDo && t.kind != TokenKind::KwYield;
        if (regions_.back().kind == RegionKind::Braces && !regions_.back().widthKnown) {
            regions_.back().width = w;
            regions_.back().widthKnown = true;
        } else if (regions_.back().provisional) {
            Region& r = regions_.back();
            r.provisional = false;
            if (w > r.width && !opensRegion(prevKind_) && !conditionOpener) r.width = w;
        }
        if ((opensRegion(prevKind_) || conditionOpener) && w > regions_.back().width) {
            // An old-style condition is recorded as RParen whether it closed
            // with `)` or (for `for {...}`) with `}`: closesRegionOpenedBy
            // treats both alike.
            const TokenKind opener = conditionOpener ? TokenKind::RParen : prevKind_;
            regions_.push_back(Region{RegionKind::Indented, w, true, opener});
            synth(TokenKind::Indent, t);
            return;
        }
        if (w < regions_.back().width) {
            // After `then`, `else`, `do`, `catch`, `finally`, `yield` or
            // `match` the construct continues on the next line, which
            // therefore cannot leave the region holding the construct.
            if (continuesStatement(prevKind_))
                throw LexError(std::string("the line after '") + tokenKindSpelling(prevKind_) +
                               "' is indented less than its enclosing block", t.pos, false);
            bool popped = false;
            while (regions_.back().kind == RegionKind::Indented && w < regions_.back().width) {
                regions_.pop_back();
                synth(TokenKind::Outdent, t);
                popped = true;
            }
            const Region& cur = regions_.back();
            // After the pops the line must sit exactly on an enclosing width:
            // deeper than an Indented region it returned to, or shallower than
            // the top level (whose width is the first line's), matches none.
            const bool between = popped && cur.kind == RegionKind::Indented && w > cur.width;
            const bool belowTop = cur.kind == RegionKind::TopLevel && w != cur.width;
            if ((between || belowTop) && !isCloser(t.kind))
                throw LexError("unindent does not match any outer indentation level",
                               t.pos, false);
            const bool widthOk = (w == cur.width) ||
                                 (cur.kind == RegionKind::Braces && w < cur.width);
            if (widthOk && (popped || canEndStatement(prevKind_)) &&
                canBeginStatement(t.kind) && !isLeadingInfix(i))
                synth(TokenKind::Newline, t);
            return;
        }
        if (w == regions_.back().width && canEndStatement(prevKind_) &&
            canBeginStatement(t.kind) && !isLeadingInfix(i))
            synth(TokenKind::Newline, t);
        // w > width after a non-opener: a continuation line, nothing inserted
        // -- except across a blank line. dotty decides the separator before it
        // looks at the indentation width at all, so a blank line ends the
        // statement however deeply the next line is indented
        // (`Scanners.handleNewLine`). Verified against scalac 3.9: `val x = 1`,
        // a blank line, then a more-indented `+ a * 6` prints 1, and the same
        // shape after a trailing `+` is an error, not a continuation. A token
        // that cannot begin a statement -- `.map(...)` on its own line -- is
        // unaffected, as it is in dotty.
        else if (w > regions_.back().width && t.pastBlankLine &&
                 canEndStatement(prevKind_) && canBeginStatement(t.kind))
            synth(TokenKind::Newline, t);
    }

    // Scala 3 leading infix operator: an operator identifier followed by a
    // blank and an operand on the same line, and *not* preceded by a blank
    // line. dotty requires all three (`Scanners.isLeadingInfixOperator`, which
    // is gated on `!pastBlankLine`): a blank line ends the expression, so the
    // operator opens a new statement instead of continuing the previous one.
    // Verified against scalac 3.9: with the blank line, `val x = 1` / blank /
    // `+ a * 6` / `println(x)` prints 1; without it, 31. A dedented
    // continuation line still continues (scalac only warns), and a
    // comment-only line is not a blank line, so both keep continuing.
    bool isLeadingInfix(std::size_t i) const {
        const Token& t = raw_[i];
        if (t.kind != TokenKind::Identifier || !t.isOperator || t.backquoted) return false;
        if (t.pastBlankLine) return false;
        const Token& n = raw_[i + 1];
        if (n.kind == TokenKind::EndOfFile || n.firstOnLine) return false;
        if (n.pos.column <= t.end.column) return false;  // no blank after the operator
        return canBeginStatement(n.kind) && !isCloser(n.kind);
    }

    // A closing keyword first satisfies the innermost pending partner of the
    // current region (a construct still open on this line or, for a closer
    // first on its line, the construct the preceding Outdents returned to).
    // Only when none is pending does a same-line closer close an Indented
    // region opened by its partner; it then satisfies the partner that opened
    // it, which is pending in the enclosing region.
    void closeByKeyword(const Token& t) {
        if (satisfyPending(t.kind)) return;
        if (t.firstOnLine) return;
        if (regions_.back().kind == RegionKind::Indented &&
            closesRegionOpenedBy(t.kind, regions_.back().opener)) {
            regions_.pop_back();
            synth(TokenKind::Outdent, t);
            satisfyPending(t.kind);
        }
    }

    // Consumes the innermost pending partner of `closer` in the current
    // region (and the constructs begun after it, which ended with it).
    // `then` and `catch` leave their own optional partner pending.
    bool satisfyPending(TokenKind closer) {
        auto& pending = regions_.back().pending;
        const auto it = std::find_if(pending.rbegin(), pending.rend(),
                                     [closer](TokenKind p) { return closesRegionOpenedBy(closer, p); });
        if (it == pending.rend()) return false;
        pending.erase(std::next(it).base(), pending.end());
        if (closer == TokenKind::KwThen || closer == TokenKind::KwCatch)
            pending.push_back(closer);
        return true;
    }

    // An old-style condition `if (...)`, `while (...)`, `for (...)` or
    // `for {...}` just closed: its keyword now awaits the partners of a
    // closed condition.
    void conditionClosed() {
        auto& pending = regions_.back().pending;
        if (!pending.empty() && isConditionKeyword(pending.back()))
            pending.back() = TokenKind::RParen;
    }

    // Returns whether the closed bracket was an old-style condition.
    bool closeBracket(const Token& t) {
        while (regions_.back().kind == RegionKind::Indented) {
            regions_.pop_back();
            synth(TokenKind::Outdent, t);
        }
        const RegionKind want = t.kind == TokenKind::RParen   ? RegionKind::Parens
                              : t.kind == TokenKind::RBracket ? RegionKind::Brackets
                                                              : RegionKind::Braces;
        if (regions_.back().kind != want)
            throw LexError("unbalanced '" + t.text + "'", t.pos, false);
        const bool condition = regions_.back().condition;
        regions_.pop_back();
        return condition;
    }

    bool tryEndMarker(std::size_t& i) {
        const Token& designator = raw_[i + 1];
        if (designator.firstOnLine || !isEndDesignator(designator.kind)) return false;
        const Token& after = raw_[i + 2 < raw_.size() ? i + 2 : raw_.size() - 1];
        if (!(after.firstOnLine || after.kind == TokenKind::EndOfFile)) return false;
        Token m = raw_[i];
        m.kind = TokenKind::EndMarker;
        m.text = designator.text;
        m.end = designator.end;
        out_.push_back(m);
        prevKind_ = TokenKind::EndMarker;
        prevClosesCondition_ = false;
        i += 1;  // the loop's ++i skips the designator
        return true;
    }
};

} // namespace

std::vector<Token> applyLayout(const std::vector<Token>& raw) {
    // Precondition: `raw` is a Lexer output, which ends with EndOfFile or
    // Error. An empty vector is the layout of empty input.
    if (raw.empty()) {
        Token eof;
        eof.kind = TokenKind::EndOfFile;
        return {eof};
    }
    const TokenKind last = raw.back().kind;
    if (last != TokenKind::EndOfFile && last != TokenKind::Error)
        throw std::invalid_argument("applyLayout: token vector must end with EndOfFile or Error");
    return LayoutPass(raw).run();
}

std::vector<Token> tokenize(const std::string& source) {
    return applyLayout(Lexer(source).tokenizeAll());
}

} // namespace protoScala
