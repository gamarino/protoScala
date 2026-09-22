/*
 * Parser — recursive descent over the laid-out token vector (DESIGN §3.3).
 *
 * Infix expressions use precedence climbing with Scala's precedence by the
 * operator's first character. Both brace and indentation syntax are accepted:
 * Layout has already turned significant indentation into Indent/Outdent/
 * Newline tokens, so an indented block is parsed exactly like a braced one.
 */
#include "frontend/Parser.h"
#include "frontend/Layout.h"
#include "runtime/StackGuard.h"

#include <cctype>

namespace protoScala {

namespace {

bool isLetterStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool isLayoutToken(TokenKind k) {
    return k == TokenKind::Newline || k == TokenKind::Indent || k == TokenKind::Outdent ||
           k == TokenKind::EndMarker || k == TokenKind::ColonEol;
}

// How a token is named in an error message: its spelling, or its kind for
// synthetic layout tokens, which have none.
std::string spelling(const Token& t) {
    return t.text.empty() ? tokenKindName(t.kind) : t.text;
}

TypePtr makeType(TypeTree::Kind kind, SourcePos pos, std::string name = {}) {
    auto t = std::make_unique<TypeTree>();
    t->kind = kind;
    t->name = std::move(name);
    t->pos = pos;
    return t;
}

} // namespace

bool isAssignmentOperator(const std::string& op) {
    if (op.size() < 2 || op.back() != '=' || op.front() == '=') return false;
    if (op == "<=" || op == ">=" || op == "!=") return false;
    for (char c : op)
        if (isLetterStart(c) || std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool isRightAssociative(const std::string& op) { return !op.empty() && op.back() == ':'; }

// Scala precedence by first character, higher binds tighter.
int precedence(const std::string& op) {
    if (isAssignmentOperator(op)) return 0;
    const char c = op.front();
    if (isLetterStart(c)) return 1;
    switch (c) {
        case '|': return 2;
        case '^': return 3;
        case '&': return 4;
        case '=': case '!': return 5;
        case '<': case '>': return 6;
        case ':': return 7;
        case '+': case '-': return 8;
        case '*': case '/': case '%': return 9;
        default: return 10;
    }
}

Parser::Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {
    // The parser relies on a final EndOfFile token (peek() clamps at it).
    if (toks_.empty() || toks_.back().kind != TokenKind::EndOfFile) {
        Token eof;
        eof.kind = TokenKind::EndOfFile;
        if (!toks_.empty()) eof.pos = toks_.back().end;
        toks_.push_back(eof);
    }
    closingParen_.assign(toks_.size(), std::string::npos);
    std::vector<std::size_t> open;
    for (std::size_t j = 0; j < toks_.size(); ++j) {
        if (toks_[j].kind == TokenKind::LParen) {
            open.push_back(j);
        } else if (toks_[j].kind == TokenKind::RParen && !open.empty()) {
            closingParen_[open.back()] = j;
            open.pop_back();
        }
    }
}

// ---------------------------------------------------------------------------
// Token helpers

const Token& Parser::peek(std::size_t k) const {
    const std::size_t j = i_ + k;
    return j < toks_.size() ? toks_[j] : toks_.back();
}

const Token& Parser::advance() {
    const Token& t = peek();
    if (i_ < toks_.size() - 1) ++i_;
    return t;
}

bool Parser::atIdent(const char* text) const {
    return at(TokenKind::Identifier) && !peek().backquoted && peek().text == text;
}

void Parser::fail(const std::string& msg, const Token& at) const {
    // Layout closes open blocks with synthetic Newline/Outdent tokens before
    // EndOfFile; an error on one of those is still "the input ended too early",
    // which the REPL answers by asking for more input.
    bool eof = at.kind == TokenKind::EndOfFile;
    if (!eof && &at >= toks_.data() && &at < toks_.data() + toks_.size()) {
        std::size_t j = static_cast<std::size_t>(&at - toks_.data());
        while (toks_[j].kind == TokenKind::Newline || toks_[j].kind == TokenKind::Outdent) ++j;
        eof = toks_[j].kind == TokenKind::EndOfFile;
    }
    throw ParseError(eof ? "unexpected end of input" + (msg.empty() ? "" : ": " + msg) : msg,
                     at.pos, eof);
}

void Parser::unsupported(const std::string& feature, const Token& at, bool plural) const {
    throw ParseError(feature + (plural ? " are" : " is") + " not implemented yet", at.pos, false);
}

const Token& Parser::expect(TokenKind k, const char* what) {
    if (!at(k)) fail(std::string(what) + " expected but '" + spelling(peek()) + "' found", peek());
    return advance();
}

bool Parser::skipOneNewline() {
    if (at(TokenKind::Newline) || at(TokenKind::Semicolon)) {
        advance();
        return true;
    }
    return false;
}

// True when `k` occurs at bracket depth 0 before the end of the current line.
bool Parser::sameLineAhead(TokenKind k) const {
    int depth = 0;
    for (std::size_t j = 0;; ++j) {
        const Token& t = peek(j);
        if (t.kind == TokenKind::EndOfFile || isLayoutToken(t.kind)) return false;
        if (j > 0 && t.firstOnLine) return false;
        if (depth == 0) {
            if (t.kind == k) return true;
            // A nested construct starts (or the condition ends): a then/do
            // further right belongs to it, not to the construct being parsed.
            switch (t.kind) {
                case TokenKind::KwElse: case TokenKind::KwIf: case TokenKind::KwWhile:
                case TokenKind::KwThen: case TokenKind::KwDo: case TokenKind::Arrow:
                    return false;
                default:
                    break;
            }
        }
        switch (t.kind) {
            case TokenKind::LParen: case TokenKind::LBracket: case TokenKind::LBrace:
                ++depth;
                break;
            case TokenKind::RParen: case TokenKind::RBracket: case TokenKind::RBrace:
                if (--depth < 0) return false;
                break;
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Entry points

NodePtr Parser::parseSingleExpression() {
    NodePtr e = parseExpr();
    while (skipOneNewline()) {}
    if (!at(TokenKind::EndOfFile))
        fail("end of expression expected but '" + spelling(peek()) + "' found", peek());
    return e;
}

std::unique_ptr<CompilationUnit> Parser::parseCompilationUnit() {
    // Script mode (Q2): definitions and expressions, in order.
    auto body = parseBlockBody(TokenKind::EndOfFile, SourcePos{1, 1});
    auto unit = std::make_unique<CompilationUnit>();
    unit->stats = std::move(body->stats);
    return unit;
}

// ---------------------------------------------------------------------------
// Expressions

NodePtr Parser::parseExprOrIndented() {
    if (at(TokenKind::Indent)) return parseIndentedBlock();
    return parseExpr();
}

// Expr, with placeholder sections (SLS 6.23.2): a `_` that this Expr
// properly contains (and no inner Expr does) becomes a parameter of a lambda
// wrapping it; a bare `_` (or `_: T`) belongs to the enclosing Expr, so
// `f(_)` is `x => f(x)`.
NodePtr Parser::parseExpr() {
    placeholderFrames_.emplace_back();
    struct Pop {
        std::vector<std::vector<std::string>>& frames;
        ~Pop() { frames.pop_back(); }
    } pop{placeholderFrames_};
    NodePtr e = parseExprNoPlaceholders();
    std::vector<std::string> names = std::move(placeholderFrames_.back());
    if (names.empty()) return e;
    const Node* bare = e.get();
    if (bare->kind == NodeKind::Typed) bare = as<Typed>(*bare).expr.get();
    if (bare->kind == NodeKind::Ident && names.size() == 1 && as<Ident>(*bare).name == names[0]) {
        if (placeholderFrames_.size() < 2)
            fail("unbound placeholder '_': write an explicit function literal", peek());
        placeholderFrames_[placeholderFrames_.size() - 2].push_back(names[0]);
        return e;
    }
    auto lambda = std::make_unique<Lambda>(e->pos);
    for (std::string& n : names) {
        Param p;
        p.name = std::move(n);
        p.pos = e->pos;
        lambda->params.push_back(std::move(p));
    }
    lambda->body = std::move(e);
    return lambda;
}

NodePtr Parser::placeholder(SourcePos pos) {
    if (placeholderFrames_.empty()) fail("unbound placeholder '_'", peek());
    std::string name = "_$" + std::to_string(++placeholderCounter_);
    placeholderFrames_.back().push_back(name);
    return std::make_unique<Ident>(pos, std::move(name));
}

NodePtr Parser::parseExprNoPlaceholders() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwIf:     return parseIf();
        case TokenKind::KwWhile:  return parseWhile();
        case TokenKind::KwReturn: return parseReturn();
        case TokenKind::KwDo:
            fail("do-while loops are not part of Scala 3; use while ... do", t);
        case TokenKind::KwThrow:  unsupported("throw", t);
        case TokenKind::KwTry:    unsupported("try", t);
        case TokenKind::KwFor:    return parseFor();
        default: break;
    }
    if (lambdaAhead()) return parseLambda();
    NodePtr e = parseInfix(0);
    while (at(TokenKind::KwMatch)) e = parseMatch(std::move(e));
    if (at(TokenKind::Equals)) {
        const NodeKind k = e->kind;
        if (k != NodeKind::Ident && k != NodeKind::Select && k != NodeKind::Apply)
            fail("left-hand side of an assignment must be a name", peek());
        const SourcePos p = e->pos;
        advance();
        return std::make_unique<Assign>(p, std::move(e), parseExprOrIndented());
    }
    if (at(TokenKind::Colon)) {
        const SourcePos p = e->pos;
        advance();
        return std::make_unique<Typed>(p, std::move(e), parseType());
    }
    return e;
}

// if (c) t [else e] | if c then t [else e]
NodePtr Parser::parseIf() {
    auto node = std::make_unique<If>(advance().pos);
    if (at(TokenKind::LParen)) {
        NodePtr cond = parseParensExpr();
        if (at(TokenKind::KwThen)) {
            advance();
            node->cond = std::move(cond);
        } else if (sameLineAhead(TokenKind::KwThen)) {
            // `if (a) || b then ...`: the parenthesised part starts a longer condition.
            node->cond = parseInfixRest(parseSimpleRest(std::move(cond)), 0);
            expect(TokenKind::KwThen, "'then'");
        } else {
            // Old style: the condition is the parenthesised expression.
            if (cond->kind == NodeKind::Parens) cond = std::move(as<Parens>(*cond).expr);
            node->cond = std::move(cond);
            if (at(TokenKind::Newline) && peek(1).kind != TokenKind::KwElse) advance();
        }
    } else {
        node->cond = parseExprOrIndented();
        expect(TokenKind::KwThen, "'then'");
    }
    node->thenp = parseExprOrIndented();
    const std::size_t save = i_;
    skipOneNewline();
    if (at(TokenKind::KwElse)) {
        advance();
        node->elsep = parseExprOrIndented();
    } else {
        i_ = save;
    }
    return node;
}

// while (c) body | while c do body
NodePtr Parser::parseWhile() {
    auto node = std::make_unique<While>(advance().pos);
    if (at(TokenKind::LParen)) {
        NodePtr cond = parseParensExpr();
        if (at(TokenKind::KwDo)) {
            advance();
            node->cond = std::move(cond);
        } else if (sameLineAhead(TokenKind::KwDo)) {
            node->cond = parseInfixRest(parseSimpleRest(std::move(cond)), 0);
            expect(TokenKind::KwDo, "'do'");
        } else {
            if (cond->kind == NodeKind::Parens) cond = std::move(as<Parens>(*cond).expr);
            node->cond = std::move(cond);
            if (at(TokenKind::Newline)) advance();
        }
    } else {
        node->cond = parseExprOrIndented();
        expect(TokenKind::KwDo, "'do'");
    }
    node->body = parseExprOrIndented();
    return node;
}

NodePtr Parser::parseReturn() {
    auto node = std::make_unique<Return>(advance().pos);
    const TokenKind k = peek().kind;
    const bool hasValue = k != TokenKind::Newline && k != TokenKind::Semicolon &&
                          k != TokenKind::Outdent && k != TokenKind::RBrace &&
                          k != TokenKind::RParen && k != TokenKind::KwElse &&
                          k != TokenKind::EndOfFile;
    if (hasValue) node->value = parseExprOrIndented();
    return node;
}

// `x =>`, `_ =>`, or `( ... ) =>` with the matching parenthesis followed by `=>`.
bool Parser::lambdaAhead() const {
    const TokenKind k0 = peek().kind;
    if ((k0 == TokenKind::Identifier && !peek().isOperator) || k0 == TokenKind::Underscore)
        return peek(1).kind == TokenKind::Arrow;
    if (k0 != TokenKind::LParen || i_ >= closingParen_.size()) return false;
    const std::size_t close = closingParen_[i_];
    return close != std::string::npos && close + 1 < toks_.size() &&
           toks_[close + 1].kind == TokenKind::Arrow;
}

std::vector<Param> Parser::parseLambdaParams() {
    std::vector<Param> params;
    auto one = [&]() {
        Param p;
        p.pos = peek().pos;
        if (at(TokenKind::Underscore)) {
            advance();
            p.name = "_";
        } else {
            p.name = expect(TokenKind::Identifier, "parameter name").text;
        }
        if (at(TokenKind::Colon)) {
            advance();
            p.type = parseType();
        }
        params.push_back(std::move(p));
    };
    if (at(TokenKind::LParen)) {
        advance();
        if (!at(TokenKind::RParen)) {
            one();
            while (at(TokenKind::Comma)) {
                advance();
                one();
            }
        }
        expect(TokenKind::RParen, "')'");
    } else {
        one();
    }
    expect(TokenKind::Arrow, "'=>'");
    return params;
}

NodePtr Parser::parseLambda() {
    auto node = std::make_unique<Lambda>(peek().pos);
    node->params = parseLambdaParams();
    node->body = parseExprOrIndented();
    return node;
}

NodePtr Parser::parseInfix(int minPrec, int assocPrec, bool assocRight) {
    return parseInfixRest(parsePrefix(), minPrec, assocPrec, assocRight);
}

// `assocPrec`/`assocRight` describe the operator whose right operand is being
// parsed (right-associative operators parse their operand at their own
// precedence), so mixing associativity at one level is caught across the
// recursion as well as within this loop.
NodePtr Parser::parseInfixRest(NodePtr lhs, int minPrec, int assocPrec, bool assocRight) {
    // Associativity seen per precedence level in this operator sequence
    // (-1: none, 0: left, 1: right). Scala forbids consecutive operators of
    // one level with different associativity even when tighter operators sit
    // in between (`a +: b * c +- d`), so a level is forgotten only when a
    // looser operator closes it.
    // The chain built here grows without native recursion; free it without
    // recursion too if parsing fails part-way (AST.h, destroyTree).
    try {
        return parseInfixLoop(lhs, minPrec, assocPrec, assocRight);
    } catch (...) {
        destroyTree(std::move(lhs));
        throw;
    }
}

NodePtr Parser::parseInfixLoop(NodePtr& lhs, int minPrec, int assocPrec, bool assocRight) {
    constexpr int kLevels = 11;  // precedence() returns 0..10
    int seen[kLevels];
    for (int& s : seen) s = -1;
    if (assocPrec >= 0) seen[assocPrec] = assocRight ? 1 : 0;
    while (at(TokenKind::Identifier)) {
        const Token& opTok = peek();
        const std::string op = opTok.text;
        // `xs*` at the end of an argument is a splice, not multiplication.
        if (op == "*" && peek(1).kind == TokenKind::RParen) break;
        const int p = precedence(op);
        if (p < minPrec) break;
        const bool right = isRightAssociative(op);
        if (seen[p] != -1 && seen[p] != (right ? 1 : 0))
            fail("left- and right-associative operators with the same precedence may not "
                 "be mixed", opTok);
        advance();
        if (at(TokenKind::Newline)) advance();  // InfixExpr ::= InfixExpr id [nl] InfixExpr
        const TokenKind next = peek().kind;
        if (next == TokenKind::RParen || next == TokenKind::RBrace ||
            next == TokenKind::Outdent || next == TokenKind::Comma)
            fail("postfix operators are not supported; '" + op + "' needs a right operand",
                 peek());
        NodePtr rhs = right ? parseInfix(p, p, true) : parseInfix(p + 1);
        const SourcePos pos = lhs->pos;
        lhs = std::make_unique<Infix>(pos, std::move(lhs), op, std::move(rhs));
        seen[p] = right ? 1 : 0;
        for (int q = p + 1; q < kLevels; ++q) seen[q] = -1;
    }
    return std::move(lhs);
}

NodePtr Parser::parsePrefix() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    if (t.kind == TokenKind::Identifier && !t.backquoted &&
        (t.text == "-" || t.text == "+" || t.text == "!" || t.text == "~")) {
        const Token& operand = peek(1);
        // Only `-5` (no blank between sign and digits) is a negative literal;
        // `- 5` is a prefix application.
        const bool adjacent =
            operand.pos.line == t.end.line && operand.pos.column == t.end.column;
        if (t.text == "-" && adjacent &&
            (operand.kind == TokenKind::IntLit || operand.kind == TokenKind::FloatLit)) {
            advance();
            NodePtr lit = parseSimple();  // the literal plus any `.method` suffix
            // Fold the sign into the literal itself (so Long.MinValue-style
            // literals work); a suffix such as `-1.abs` applies to the literal.
            Node* target = lit.get();
            for (;;) {
                if (target->kind == NodeKind::Select) target = as<Select>(*target).qualifier.get();
                else if (target->kind == NodeKind::Apply) target = as<Apply>(*target).fn.get();
                else if (target->kind == NodeKind::TypeApply) target = as<TypeApply>(*target).fn.get();
                else break;
            }
            if (target->kind == NodeKind::IntLit) {
                auto& il = as<IntLit>(*target);
                il.value = -il.value;
                if (!il.fitsLong) il.digits = "-" + il.digits;
                il.pos = t.pos;
            } else if (target->kind == NodeKind::FloatLit) {
                auto& fl = as<FloatLit>(*target);
                fl.value = -fl.value;
                fl.text = "-" + fl.text;
                fl.pos = t.pos;
            }
            lit->pos = t.pos;
            return lit;
        }
        advance();
        return std::make_unique<Prefix>(t.pos, t.text, parseSimple());
    }
    return parseSimple();
}

NodePtr Parser::parseSimple() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    NodePtr base;
    switch (t.kind) {
        case TokenKind::IntLit: {
            auto n = std::make_unique<IntLit>(t.pos);
            n->value = t.intValue;
            n->fitsLong = t.fitsLong;
            n->digits = t.digits;
            n->base = t.base;
            base = std::move(n);
            advance();
            break;
        }
        case TokenKind::FloatLit: {
            auto n = std::make_unique<FloatLit>(t.pos);
            n->value = t.floatValue;
            n->text = t.text;
            base = std::move(n);
            advance();
            break;
        }
        case TokenKind::StringLit: {
            auto n = std::make_unique<StringLit>(t.pos);
            n->value = t.stringValue;
            base = std::move(n);
            advance();
            break;
        }
        case TokenKind::CharLit: {
            auto n = std::make_unique<CharLit>(t.pos);
            n->value = t.charValue;
            base = std::move(n);
            advance();
            break;
        }
        case TokenKind::InterpolatedString: {
            auto n = std::make_unique<InterpString>(t.pos);
            n->interpolator = t.text;
            n->parts = t.parts;
            base = std::move(n);
            advance();
            break;
        }
        case TokenKind::Identifier:
            base = std::make_unique<Ident>(t.pos, t.text);
            advance();
            break;
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            base = std::make_unique<BoolLit>(t.pos, t.kind == TokenKind::KwTrue);
            advance();
            break;
        case TokenKind::KwNull:
            base = std::make_unique<NullLit>(t.pos);
            advance();
            break;
        case TokenKind::LParen:
            base = parseParensExpr();
            break;
        case TokenKind::LBrace:
            base = parseBlockExpr();
            break;
        case TokenKind::KwNew:
            base = parseNew();
            break;
        case TokenKind::KwThis:
            base = std::make_unique<Ident>(t.pos, "this");
            advance();
            break;
        case TokenKind::KwSuper:
            advance();
            if (at(TokenKind::LBracket)) unsupported("super[T] (a qualified super call)", peek());
            if (!at(TokenKind::Dot)) fail("'.' expected after 'super'", peek());
            base = std::make_unique<Ident>(t.pos, "super");  // the compiler checks the context
            break;
        case TokenKind::Underscore:
            base = placeholder(t.pos);
            advance();
            break;
        default:
            fail("expression expected but '" + spelling(t) + "' found", t);
    }
    return parseSimpleRest(std::move(base));
}

NodePtr Parser::parseSimpleRest(NodePtr base) {
    // `a.b.c...` and `f(x)(y)...` chains grow here without native recursion;
    // free them without recursion too if parsing fails part-way.
    try {
        return parseSimpleLoop(base);
    } catch (...) {
        destroyTree(std::move(base));
        throw;
    }
}

NodePtr Parser::parseSimpleLoop(NodePtr& base) {
    for (;;) {
        const Token& t = peek();
        if (t.kind == TokenKind::Dot) {
            advance();
            const Token& name = expect(TokenKind::Identifier, "member name");
            const SourcePos p = base->pos;
            base = std::make_unique<Select>(p, std::move(base), name.text);
        } else if (t.kind == TokenKind::LParen && !t.firstOnLine) {
            advance();
            const SourcePos p = base->pos;
            auto app = std::make_unique<Apply>(p, std::move(base));
            app->args = parseArgs();
            base = std::move(app);
        } else if (t.kind == TokenKind::LBrace && !t.firstOnLine) {
            const SourcePos p = base->pos;
            auto app = std::make_unique<Apply>(p, std::move(base));
            app->args.push_back(parseBlockExpr());
            app->blockArg = true;
            base = std::move(app);
        } else if (t.kind == TokenKind::LBracket) {
            advance();
            const SourcePos p = base->pos;
            auto tapp = std::make_unique<TypeApply>(p, std::move(base));
            tapp->types.push_back(parseType());
            while (at(TokenKind::Comma)) {
                advance();
                tapp->types.push_back(parseType());
            }
            expect(TokenKind::RBracket, "']'");
            base = std::move(tapp);
        } else {
            return std::move(base);
        }
    }
}

// `(` ... `)`: unit, a parenthesised expression or a tuple.
NodePtr Parser::parseParensExpr() {
    const SourcePos pos = expect(TokenKind::LParen, "'('").pos;
    if (at(TokenKind::RParen)) {
        advance();
        return std::make_unique<UnitLit>(pos);
    }
    NodePtr first = parseExpr();
    if (!at(TokenKind::Comma)) {
        expect(TokenKind::RParen, "')'");
        return std::make_unique<Parens>(pos, std::move(first));
    }
    auto tuple = std::make_unique<Tuple>(pos);
    tuple->elems.push_back(std::move(first));
    while (at(TokenKind::Comma)) {
        advance();
        tuple->elems.push_back(parseExpr());
    }
    expect(TokenKind::RParen, "')'");
    return tuple;
}

// After `(`, up to and including `)`.
std::vector<NodePtr> Parser::parseArgs() {
    std::vector<NodePtr> args;
    if (at(TokenKind::RParen)) {
        advance();
        return args;
    }
    for (;;) {
        NodePtr arg;
        if (at(TokenKind::Identifier) && peek(1).kind == TokenKind::Equals) {
            const Token& name = advance();
            advance();  // `=`
            arg = std::make_unique<NamedArg>(name.pos, name.text, parseExpr());
        } else {
            arg = parseExpr();
            if (atIdent("*") && peek(1).kind == TokenKind::RParen) {
                advance();
                const SourcePos p = arg->pos;
                arg = std::make_unique<Splice>(p, std::move(arg));
            } else if (arg->kind == NodeKind::Typed) {
                auto& typed = as<Typed>(*arg);
                const TypeTree& ty = *typed.type;
                if (ty.kind == TypeTree::Kind::Repeated &&
                    ty.args.front()->kind == TypeTree::Kind::Wildcard) {
                    const SourcePos p = arg->pos;
                    arg = std::make_unique<Splice>(p, std::move(typed.expr));
                }
            }
        }
        args.push_back(std::move(arg));
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    return args;
}

NodePtr Parser::parseBlockExpr() {
    const SourcePos pos = expect(TokenKind::LBrace, "'{'").pos;
    while (at(TokenKind::Newline)) advance();
    if (at(TokenKind::KwCase)) {
        NodePtr lambda = parseCaseLambda(pos, TokenKind::RBrace);
        expect(TokenKind::RBrace, "'}'");
        return lambda;
    }
    NodePtr block = parseBlockBody(TokenKind::RBrace, pos);
    expect(TokenKind::RBrace, "'}'");
    return block;
}

NodePtr Parser::parseIndentedBlock() {
    const SourcePos pos = expect(TokenKind::Indent, "indented block").pos;
    NodePtr block = parseBlockBody(TokenKind::Outdent, pos);
    expect(TokenKind::Outdent, "end of indented block");
    return block;
}

// Statements up to (not including) `terminator`.
std::unique_ptr<Block> Parser::parseBlockBody(TokenKind terminator, SourcePos pos) {
    checkNativeStack(StackUse::Source);
    auto block = std::make_unique<Block>(pos);
    // A block (a method body, a local block) is never a template body, even
    // inside one: abstract members are rejected there.
    const bool savedTemplate = inTemplateBody_;
    inTemplateBody_ = false;
    for (;;) {
        while (skipOneNewline()) {}
        if (at(terminator)) break;
        if (at(TokenKind::EndOfFile)) fail("", peek());
        if (at(TokenKind::EndMarker)) {
            if (block->stats.empty())
                fail("misaligned end marker: 'end " + peek().text +
                     "' does not close a preceding construct", peek());
            checkEndMarker(*block->stats.back(), peek());
            advance();
            continue;
        }
        block->stats.push_back(parseBlockStat(terminator));
        const TokenKind k = peek().kind;
        if (k != TokenKind::Newline && k != TokenKind::Semicolon && k != terminator)
            fail("';' or newline expected but '" + spelling(peek()) + "' found", peek());
    }
    inTemplateBody_ = savedTemplate;
    return block;
}

NodePtr Parser::parseBlockStat(TokenKind terminator) {
    if (atDefinitionStart()) return parseDefinition({});
    if (lambdaAhead()) {
        // Block lambda: `{ x => stats }`; the body is the rest of the block
        // (ResultExpr ::= Bindings '=>' Block).
        auto node = std::make_unique<Lambda>(peek().pos);
        node->params = parseLambdaParams();
        if (at(TokenKind::Indent)) node->body = parseIndentedBlock();
        else node->body = parseBlockBody(terminator, peek().pos);
        return node;
    }
    return parseExpr();
}

// `end if` / `end while` after an If / While; `end <name>` after a definition
// of that name; `end val` after a val.
void Parser::checkEndMarker(const Node& previous, const Token& marker) const {
    const std::string& d = marker.text;
    bool ok = false;
    switch (previous.kind) {
        case NodeKind::If:    ok = d == "if"; break;
        case NodeKind::While: ok = d == "while"; break;
        case NodeKind::Match: ok = d == "match"; break;
        case NodeKind::For:   ok = d == "for"; break;
        case NodeKind::DefDef: ok = d == as<DefDef>(previous).name; break;
        case NodeKind::ValDef: ok = d == "val" || d == as<ValDef>(previous).name; break;
        case NodeKind::TemplateDef: ok = d == as<TemplateDef>(previous).name; break;
        default: break;
    }
    if (!ok)
        fail("misaligned end marker: 'end " + d + "' does not close the preceding construct",
             marker);
}

// ---------------------------------------------------------------------------
// Definitions

namespace {

// Soft modifiers (Token.h): identifiers that act as modifiers only when a
// definition follows them. They are parsed and ignored (D5).
bool isSoftModifier(const Token& t) {
    if (t.kind != TokenKind::Identifier || t.backquoted) return false;
    const std::string& s = t.text;
    return s == "open" || s == "inline" || s == "transparent" || s == "infix" ||
           s == "opaque";
}

bool isHardModifier(TokenKind k) {
    switch (k) {
        case TokenKind::KwPrivate: case TokenKind::KwProtected: case TokenKind::KwFinal:
        case TokenKind::KwOverride: case TokenKind::KwAbstract: case TokenKind::KwSealed:
            return true;
        default:
            return false;
    }
}

// Keywords that begin a definition (after any modifiers).
bool isDefinitionKeyword(TokenKind k) {
    switch (k) {
        case TokenKind::KwVal: case TokenKind::KwVar: case TokenKind::KwDef:
        case TokenKind::KwLazy: case TokenKind::KwImport: case TokenKind::KwImplicit:
        case TokenKind::KwGiven: case TokenKind::KwClass: case TokenKind::KwObject:
        case TokenKind::KwTrait: case TokenKind::KwEnum: case TokenKind::KwCase:
        case TokenKind::KwType: case TokenKind::KwPackage: case TokenKind::KwExport:
            return true;
        default:
            return isHardModifier(k);
    }
}

bool isExtensionStart(const Token& t, const Token& next) {
    return t.kind == TokenKind::Identifier && !t.backquoted && t.text == "extension" &&
           (next.kind == TokenKind::LParen || next.kind == TokenKind::LBracket);
}

[[noreturn]] void notImplemented(const std::string& what, const Token& at) {
    throw ParseError(what + " are not implemented yet", at.pos, false);
}

const char* const kImplicitsUnsupported = "implicits and givens are not supported (D3)";

} // namespace

bool Parser::atDefinitionStart() const {
    if (at(TokenKind::At) || isDefinitionKeyword(peek().kind)) return true;
    if (isExtensionStart(peek(), peek(1))) return true;
    // Soft modifiers count only when a definition follows them.
    std::size_t j = 0;
    while (isSoftModifier(peek(j))) ++j;
    return j > 0 && isDefinitionKeyword(peek(j).kind);
}

// {Annotation} {Modifier} (val | var | lazy val | def | import | [case] class |
// trait | [case] object), or one of the definition forms reported as not
// implemented yet.
NodePtr Parser::parseDefinition(std::vector<std::string> annotations) {
    while (at(TokenKind::At)) {
        advance();
        std::string name = expect(TokenKind::Identifier, "annotation name").text;
        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
            advance();
            name += '.';
            name += advance().text;
        }
        annotations.push_back(std::move(name));
        if (at(TokenKind::LParen) && !peek().firstOnLine) {  // arguments are skipped
            int depth = 0;
            do {
                if (at(TokenKind::EndOfFile)) fail("')' expected", peek());
                if (at(TokenKind::LParen)) ++depth;
                if (at(TokenKind::RParen)) --depth;
                advance();
            } while (depth > 0);
        }
        while (at(TokenKind::Newline)) advance();
    }
    bool isLazy = false;
    const Modifiers mods = parseModifiers(&isLazy);
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwVal: {
            advance();
            NodePtr v = parseValDef(t.pos, false, isLazy);
            as<ValDef>(*v).mods = mods;
            return v;
        }
        case TokenKind::KwVar: {
            if (isLazy) fail("'lazy' is not allowed on a var", t);
            advance();
            NodePtr v = parseValDef(t.pos, true, false);
            as<ValDef>(*v).mods = mods;
            return v;
        }
        case TokenKind::KwDef: {
            if (isLazy) fail("'lazy' is not allowed on a def", t);
            advance();
            NodePtr d = parseDefDef(t.pos, std::move(annotations));
            as<DefDef>(*d).mods = mods;
            return d;
        }
        case TokenKind::KwImport:
            if (isLazy || !annotations.empty()) fail("an import takes no modifiers", t);
            return parseImport();
        case TokenKind::KwCase:
            if (peek(1).kind == TokenKind::KwClass || peek(1).kind == TokenKind::KwObject) {
                if (isLazy) fail("'lazy' is not allowed on a class or object", t);
                return parseTemplateDef(mods);
            }
            fail("'case' is only allowed in a match or in a pattern-matching function "
                 "literal `{ case ... }`", t);
        case TokenKind::KwClass: case TokenKind::KwObject: case TokenKind::KwTrait:
            if (isLazy) fail("'lazy' is not allowed on a class, trait or object", t);
            return parseTemplateDef(mods);
        case TokenKind::KwEnum: case TokenKind::KwType: case TokenKind::KwPackage:
        case TokenKind::KwExport:
            notImplemented("'" + t.text + "' definitions", t);
        default:
            if (isExtensionStart(t, peek(1))) notImplemented("'extension' definitions", t);
            fail("definition expected but '" + spelling(t) + "' found", t);
    }
}

// Hard and soft modifiers before a definition. `lazy` is reported apart;
// implicit/given are rejected (D3).
Modifiers Parser::parseModifiers(bool* isLazy) {
    Modifiers mods;
    for (;;) {
        const Token& t = peek();
        if (t.kind == TokenKind::KwImplicit || t.kind == TokenKind::KwGiven)
            fail(kImplicitsUnsupported, t);
        if (t.kind == TokenKind::KwLazy) {
            *isLazy = true;
            advance();
            continue;
        }
        if (!isHardModifier(t.kind) && !isSoftModifier(t)) break;
        switch (t.kind) {
            case TokenKind::KwPrivate:   mods.isPrivate = true; break;
            case TokenKind::KwProtected: mods.isProtected = true; break;
            case TokenKind::KwOverride:  mods.isOverride = true; break;
            case TokenKind::KwAbstract:  mods.isAbstract = true; break;
            case TokenKind::KwFinal:     mods.isFinal = true; break;
            case TokenKind::KwSealed:    mods.isSealed = true; break;
            default: break;              // soft modifiers: accepted and ignored
        }
        const bool qualifiable =
            t.kind == TokenKind::KwPrivate || t.kind == TokenKind::KwProtected;
        advance();
        if (qualifiable && at(TokenKind::LBracket)) {  // private[this], private[pkg]
            advance();
            if (at(TokenKind::KwThis)) advance();
            else expect(TokenKind::Identifier, "access qualifier");
            expect(TokenKind::RBracket, "']'");
        }
    }
    return mods;
}

// `[` TypeParam {`,` TypeParam} `]`. Names are kept; variance (`+A`, `-A`),
// higher-kinded parameters (`F[_]`), bounds (`<:`, `>:`) and context bounds
// (`: Ordering`) are parsed and erased (DESIGN §2).
std::vector<std::string> Parser::parseTypeParams() {
    expect(TokenKind::LBracket, "'['");
    std::vector<std::string> names;
    while (!at(TokenKind::RBracket)) {
        if (atIdent("+") || atIdent("-")) advance();
        if (at(TokenKind::Underscore)) {
            advance();
            names.push_back("_");
        } else {
            names.push_back(expect(TokenKind::Identifier, "type parameter").text);
        }
        if (at(TokenKind::LBracket)) {  // F[_]: the arity of a type constructor
            int depth = 0;
            do {
                if (at(TokenKind::EndOfFile)) fail("']' expected", peek());
                if (at(TokenKind::LBracket)) ++depth;
                if (at(TokenKind::RBracket)) --depth;
                advance();
            } while (depth > 0);
        }
        while (at(TokenKind::Subtype) || at(TokenKind::Supertype) || at(TokenKind::Colon)) {
            advance();
            parseType();
        }
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RBracket, "']'");
    return names;
}

// After `val` / `var`: name [: T] = rhs. Only a single name is supported.
NodePtr Parser::parseValDef(SourcePos pos, bool isVar, bool isLazy) {
    auto node = std::make_unique<ValDef>(pos);
    node->isVar = isVar;
    node->isLazy = isLazy;
    if (at(TokenKind::EndOfFile)) fail("value name expected", peek());
    const TokenKind next = peek(1).kind;
    // A lone name, upper-case or back-quoted too, is defined, not matched
    // (`val X = 1`); anything else is a pattern definition.
    const bool simpleName = at(TokenKind::Identifier) && !peek().isOperator &&
                            (next == TokenKind::Colon || next == TokenKind::Equals ||
                             next == TokenKind::Newline || next == TokenKind::EndOfFile ||
                             next == TokenKind::Semicolon);
    if (!simpleName) {
        if (at(TokenKind::Identifier) && next == TokenKind::Comma)
            notImplemented("several names in one value definition", peek());
        if (isLazy) notImplemented("lazy pattern definitions", peek());
        node->pattern = parsePattern2();
        if (at(TokenKind::Colon)) {  // val (a, b): (Int, Int) = ...: the type is erased
            advance();
            parseType();
        }
        expect(TokenKind::Equals, "'='");
        node->rhs = parseExprOrIndented();
        return node;
    }
    node->name = advance().text;
    if (at(TokenKind::Colon)) {
        advance();
        node->type = parseType();
    }
    if (!at(TokenKind::Equals)) {
        if (inTemplateBody_ && node->type) return node;  // abstract member
        fail("'=' expected: a value definition needs an initialiser", peek());
    }
    advance();
    if (isVar && at(TokenKind::Underscore))
        fail("default initialisation 'var x: T = _' is not supported", peek());
    node->rhs = parseExprOrIndented();
    return node;
}

// `(` [param {`,` param}] `)`; a `using` / `implicit` clause is rejected (D3).
std::vector<Param> Parser::parseParamClause() {
    expect(TokenKind::LParen, "'('");
    if (at(TokenKind::KwImplicit) ||
        (atIdent("using") && peek(1).kind != TokenKind::Colon))
        fail(kImplicitsUnsupported, peek());
    std::vector<Param> params;
    while (!at(TokenKind::RParen)) {
        Param p;
        p.pos = peek().pos;
        p.name = expect(TokenKind::Identifier, "parameter name").text;
        expect(TokenKind::Colon, "':' and a parameter type");
        p.type = parseType();  // `=> T` yields a ByName type
        p.byName = p.type->kind == TypeTree::Kind::ByName;
        if (atIdent("*")) {
            advance();
            p.repeated = true;
        }
        if (at(TokenKind::Equals)) {
            advance();
            p.defaultValue = parseExpr();
        }
        params.push_back(std::move(p));
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    return params;
}

// After `def`: name [TypeParams] {ParamClause} [: T] = body; the body is
// absent for an abstract member of a template.
NodePtr Parser::parseDefDef(SourcePos pos, std::vector<std::string> annotations) {
    auto node = std::make_unique<DefDef>(pos);
    node->annotations = std::move(annotations);
    if (at(TokenKind::KwThis) && inTemplateBody_) {  // auxiliary constructor
        advance();
        node->name = "this";
    } else {
        node->name = expect(TokenKind::Identifier, "method name").text;
    }
    if (at(TokenKind::LBracket)) node->typeParams = parseTypeParams();
    while (at(TokenKind::LParen) && !peek().firstOnLine)
        node->paramLists.push_back(parseParamClause());
    if (at(TokenKind::Colon)) {
        advance();
        node->resultType = parseType();
    }
    if (at(TokenKind::LBrace))
        fail("procedure syntax is not supported in Scala 3; write `def " + node->name +
                 "(): Unit = ...`",
             peek());
    if (!at(TokenKind::Equals)) {
        // An abstract member: only in a template body, never for a constructor.
        if (inTemplateBody_ && node->name != "this") return node;
        fail("'=' expected: abstract methods are only allowed in classes, traits and objects",
             peek());
    }
    advance();
    node->body = parseExprOrIndented();
    return node;
}

// `import` selectors: parsed as raw text and ignored by the compiler (Q8).
// Tokens are concatenated without blanks, except `, ` after a comma and
// blanks around `as` and `=>`, so `import a.{b, c as d}` round-trips.
NodePtr Parser::parseImport() {
    auto node = std::make_unique<Import>(expect(TokenKind::KwImport, "'import'").pos);
    int depth = 0;
    for (;;) {
        const Token& t = peek();
        const TokenKind k = t.kind;
        if (k == TokenKind::EndOfFile) {
            if (depth > 0 || node->text.empty()) fail("import selector expected", t);
            break;
        }
        if (depth == 0 && (k == TokenKind::Newline || k == TokenKind::Semicolon ||
                           k == TokenKind::Outdent || k == TokenKind::RBrace ||
                           k == TokenKind::EndMarker))
            break;
        if (k == TokenKind::LBrace) ++depth;
        if (k == TokenKind::RBrace) --depth;
        if (k == TokenKind::Comma) node->text += ", ";
        else if ((k == TokenKind::Identifier && !t.backquoted && t.text == "as") ||
                 k == TokenKind::Arrow)
            node->text += " " + t.text + " ";
        else node->text += t.backquoted ? "`" + t.text + "`" : spelling(t);
        advance();
    }
    if (node->text.empty()) fail("import selector expected", peek());
    return node;
}

// ---------------------------------------------------------------------------
// Templates

// [case] (class | trait | object) Name [TypeParams] [ConstrMods] [ParamClause]
// [extends Parents] [derives Types] [TemplateBody]
NodePtr Parser::parseTemplateDef(Modifiers mods) {
    const Token& start = peek();
    auto node = std::make_unique<TemplateDef>(start.pos);
    node->mods = mods;
    if (at(TokenKind::KwCase)) {
        advance();
        node->isCase = true;
    }
    switch (peek().kind) {
        case TokenKind::KwClass:  node->kind = TemplateKind::Class; break;
        case TokenKind::KwTrait:  node->kind = TemplateKind::Trait; break;
        case TokenKind::KwObject: node->kind = TemplateKind::Object; break;
        default: fail("'class', 'trait' or 'object' expected", peek());
    }
    advance();
    node->name = expect(TokenKind::Identifier, "class name").text;
    if (at(TokenKind::LBracket)) node->typeParams = parseTypeParams();
    if (node->kind != TemplateKind::Object) {
        // `class C private (x: Int)`: a constructor access modifier (advisory, D5).
        if ((at(TokenKind::KwPrivate) || at(TokenKind::KwProtected)) &&
            peek(1).kind == TokenKind::LParen) {
            advance();
        }
        if (at(TokenKind::LParen) && !peek().firstOnLine) {
            node->hasParamClause = true;
            node->ctorParams = parseClassParamClause();
            if (at(TokenKind::LParen) && !peek().firstOnLine)
                unsupported("multiple constructor parameter lists", peek(), true);
        }
    }
    if (node->isCase && node->kind == TemplateKind::Class && !node->hasParamClause)
        fail("A case class must have a parameter list; write `case class " + node->name +
                 "()` or a case object",
             start);
    if (at(TokenKind::KwExtends)) {
        advance();
        node->parents = parseParents();
    }
    if (atIdent("derives")) {  // type-class derivation: parsed and ignored (D3)
        advance();
        parseType();
        while (at(TokenKind::Comma)) {
            advance();
            parseType();
        }
    }
    // A template body on the same line, or `{` on the next line (Scala allows a
    // newline before a template body).
    if (at(TokenKind::Newline) && peek(1).kind == TokenKind::LBrace) advance();
    if (at(TokenKind::LBrace) || at(TokenKind::ColonEol) ||
        (at(TokenKind::Colon) && peek(1).kind == TokenKind::EndOfFile))
        node->body = parseTemplateBody(&node->selfName);
    return node;
}

// `(` [ClassParam {`,` ClassParam}] `)`;
// ClassParam ::= {Modifier} [`val` | `var`] id `:` Type [`=` Expr]
std::vector<Param> Parser::parseClassParamClause() {
    expect(TokenKind::LParen, "'('");
    if (at(TokenKind::KwImplicit) || (atIdent("using") && peek(1).kind != TokenKind::Colon))
        fail(kImplicitsUnsupported, peek());
    std::vector<Param> params;
    while (!at(TokenKind::RParen)) {
        Param p;
        p.pos = peek().pos;
        bool lazyIgnored = false;
        p.mods = parseModifiers(&lazyIgnored);
        if (lazyIgnored) fail("'lazy' is not allowed on a class parameter", peek());
        if (at(TokenKind::KwVal)) {
            advance();
            p.isVal = true;
        } else if (at(TokenKind::KwVar)) {
            advance();
            p.isVar = true;
        }
        p.name = expect(TokenKind::Identifier, "parameter name").text;
        expect(TokenKind::Colon, "':' and a parameter type");
        p.type = parseType();
        p.byName = p.type->kind == TypeTree::Kind::ByName;
        if (atIdent("*")) {
            advance();
            p.repeated = true;
        }
        if (at(TokenKind::Equals)) {
            advance();
            p.defaultValue = parseExpr();
        }
        params.push_back(std::move(p));
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    return params;
}

// Parents ::= ConstrApp {(`with` | `,`) ConstrApp}; ConstrApp ::= SimpleType [ArgumentExprs]
std::vector<ParentRef> Parser::parseParents() {
    std::vector<ParentRef> out;
    for (;;) {
        ParentRef p;
        p.pos = peek().pos;
        p.type = parseSimpleType();
        if (at(TokenKind::LParen) && !peek().firstOnLine) {
            advance();
            p.args = parseArgs();
            p.hasArgs = true;
            if (at(TokenKind::LParen) && !peek().firstOnLine)
                unsupported("multiple constructor argument lists", peek(), true);
        }
        out.push_back(std::move(p));
        if (at(TokenKind::KwWith) || at(TokenKind::Comma)) {
            advance();
            continue;
        }
        return out;
    }
}

// `{` [SelfAlias] stats `}`  |  `:` Indent [SelfAlias] stats Outdent
std::vector<NodePtr> Parser::parseTemplateBody(std::string* selfName) {
    TokenKind terminator;
    if (at(TokenKind::LBrace)) {
        advance();
        terminator = TokenKind::RBrace;
    } else {
        // ColonEol, or a plain `:` at the very end of the input (`class A:` typed
        // at the REPL): both need an indented body; at end of input the error
        // is "unexpected end of input", so the REPL asks for more lines.
        if (!at(TokenKind::ColonEol) && !at(TokenKind::Colon)) fail("template body expected", peek());
        advance();
        if (!at(TokenKind::Indent)) fail("an indented template body is expected after ':'", peek());
        advance();
        terminator = TokenKind::Outdent;
    }
    while (skipOneNewline()) {}
    // Self alias: `self =>`, `self: T =>`, `this: T =>` (the type is erased).
    bool alias = false;
    if ((at(TokenKind::Identifier) || at(TokenKind::KwThis)) && peek(1).kind == TokenKind::Arrow) {
        if (at(TokenKind::Identifier)) *selfName = peek().text;
        advance();
        advance();
        alias = true;
    } else if ((at(TokenKind::Identifier) || at(TokenKind::KwThis)) &&
               peek(1).kind == TokenKind::Colon) {
        const std::size_t save = i_;
        const std::string name = at(TokenKind::Identifier) ? peek().text : "";
        advance();
        advance();
        bool isAlias = false;
        try {
            parseInfixType();
            isAlias = at(TokenKind::Arrow);
        } catch (const ParseError&) {
            isAlias = false;
        }
        if (isAlias) {
            advance();
            *selfName = name;
            alias = true;
        } else {
            i_ = save;  // an ordinary statement `x: T` (a typed expression)
        }
    }
    // `{ self =>` followed by members on deeper lines: Layout opened an
    // indented region after `=>`; the members end with its Outdent.
    const bool aliasRegion = alias && at(TokenKind::Indent);
    if (aliasRegion) advance();
    const TokenKind statsEnd = aliasRegion ? TokenKind::Outdent : terminator;
    const bool saved = inTemplateBody_;
    inTemplateBody_ = true;
    std::vector<NodePtr> stats;
    for (;;) {
        while (skipOneNewline()) {}
        if (at(statsEnd)) break;
        if (at(TokenKind::EndOfFile)) fail("", peek());
        if (at(TokenKind::EndMarker)) {
            if (stats.empty())
                fail("misaligned end marker: 'end " + peek().text +
                     "' does not close a preceding construct", peek());
            checkEndMarker(*stats.back(), peek());
            advance();
            continue;
        }
        stats.push_back(parseTemplateStat());
        const TokenKind k = peek().kind;
        if (k != TokenKind::Newline && k != TokenKind::Semicolon && k != statsEnd)
            fail("';' or newline expected but '" + spelling(peek()) + "' found", peek());
    }
    inTemplateBody_ = saved;
    if (aliasRegion) {
        expect(TokenKind::Outdent, "end of template body");
        while (skipOneNewline()) {}
    }
    expect(terminator, terminator == TokenKind::RBrace ? "'}'" : "end of template body");
    return stats;
}

NodePtr Parser::parseTemplateStat() {
    if (atDefinitionStart()) return parseDefinition({});
    return parseExpr();
}

// new T | new T(args) | new T[A](args). Anonymous class bodies are not
// supported (Open question Q6).
NodePtr Parser::parseNew() {
    auto node = std::make_unique<New>(expect(TokenKind::KwNew, "'new'").pos);
    node->type = parseSimpleType();
    if (at(TokenKind::LParen) && !peek().firstOnLine) {
        advance();
        node->args = parseArgs();
        node->hasArgs = true;
    }
    if (at(TokenKind::LParen) && !peek().firstOnLine)
        unsupported("multiple constructor argument lists", peek(), true);
    // A body on the next line is still a class body, as for a template
    // definition (parseTemplateDef).
    if (at(TokenKind::Newline) && peek(1).kind == TokenKind::LBrace)
        unsupported("anonymous classes", peek(1), true);
    if ((at(TokenKind::LBrace) && !peek().firstOnLine) || at(TokenKind::ColonEol) ||
        at(TokenKind::KwWith))
        unsupported("anonymous classes", peek(), true);
    return node;
}

// ---------------------------------------------------------------------------
// Pattern matching

// e match { cases } | e match <Indent> cases <Outdent>. A case at the same
// indentation as `match` is not supported (D23).
NodePtr Parser::parseMatch(NodePtr scrutinee) {
    auto m = std::make_unique<Match>(scrutinee->pos);
    m->scrutinee = std::move(scrutinee);
    advance();  // match
    TokenKind terminator = TokenKind::RBrace;
    if (at(TokenKind::LBrace)) {
        advance();
    } else if (at(TokenKind::Indent)) {
        advance();
        terminator = TokenKind::Outdent;
    } else {
        fail("'{' or an indented block of cases expected after 'match'", peek());
    }
    while (skipOneNewline()) {}
    if (!at(TokenKind::KwCase)) fail("'case' expected", peek());
    while (at(TokenKind::KwCase)) {
        m->cases.push_back(parseCaseClause(terminator));
        while (skipOneNewline()) {}
    }
    expect(terminator, terminator == TokenKind::RBrace ? "'}'" : "end of the cases");
    return m;
}

// case Pattern [if Guard] => Block
CaseDef Parser::parseCaseClause(TokenKind terminator) {
    CaseDef c;
    c.pos = expect(TokenKind::KwCase, "'case'").pos;
    c.pattern = parsePattern();
    if (at(TokenKind::KwIf)) {
        advance();
        c.guard = parseInfix(0);
    }
    expect(TokenKind::Arrow, "'=>'");
    c.body = parseCaseBody(terminator);
    return c;
}

// The statements after `=>`, up to the next `case` or the end of the cases.
// A single expression is the body itself; several statements are a block.
NodePtr Parser::parseCaseBody(TokenKind terminator) {
    if (at(TokenKind::Indent)) return parseIndentedBlock();
    auto block = std::make_unique<Block>(peek().pos);
    const bool saved = inTemplateBody_;
    inTemplateBody_ = false;
    for (;;) {
        const TokenKind k = peek().kind;
        if (k == TokenKind::KwCase || k == terminator || k == TokenKind::EndOfFile ||
            k == TokenKind::EndMarker)
            break;
        if (k == TokenKind::Newline || k == TokenKind::Semicolon) {
            const TokenKind next = peek(1).kind;
            advance();
            if (next == TokenKind::KwCase || next == terminator) break;
            continue;
        }
        block->stats.push_back(parseBlockStat(terminator));
        const TokenKind after = peek().kind;
        if (after != TokenKind::Newline && after != TokenKind::Semicolon &&
            after != TokenKind::KwCase && after != terminator &&
            after != TokenKind::EndOfFile && after != TokenKind::EndMarker)
            fail("';' or newline expected but '" + spelling(peek()) + "' found", peek());
    }
    inTemplateBody_ = saved;
    if (block->stats.size() == 1 && block->stats[0]->kind != NodeKind::ValDef &&
        block->stats[0]->kind != NodeKind::DefDef)
        return std::move(block->stats[0]);
    return block;
}

// `{ case p => e ... }`: a one-parameter function whose body matches its
// argument (D34). Stops before the closing `terminator`.
NodePtr Parser::parseCaseLambda(SourcePos pos, TokenKind terminator) {
    const std::string param = "x$" + std::to_string(++caseLambdaCounter_);
    auto m = std::make_unique<Match>(pos);
    m->scrutinee = std::make_unique<Ident>(pos, param);
    while (at(TokenKind::KwCase)) {
        m->cases.push_back(parseCaseClause(terminator));
        while (skipOneNewline()) {}
    }
    auto lambda = std::make_unique<Lambda>(pos);
    Param p;
    p.name = param;
    p.pos = pos;
    lambda->params.push_back(std::move(p));
    lambda->body = std::move(m);
    return lambda;
}

namespace {

// A variable pattern is a simple identifier starting with a lower-case
// letter or `_` (SLS 8.1.1); a back-quoted or upper-case identifier is a
// stable identifier.
bool isVarId(const Token& t) {
    if (t.kind != TokenKind::Identifier || t.backquoted || t.isOperator || t.text.empty())
        return false;
    const unsigned char c = static_cast<unsigned char>(t.text[0]);
    return c == '_' || (c >= 'a' && c <= 'z') || c >= 0x80;
}

PatternPtr makePattern(Pattern::Kind k, SourcePos pos) {
    auto p = std::make_unique<Pattern>();
    p->kind = k;
    p->pos = pos;
    return p;
}

} // namespace

// Pattern ::= Pattern1 {`|` Pattern1}
PatternPtr Parser::parsePattern() {
    PatternPtr first = parsePattern1();
    if (!atIdent("|")) return first;
    auto alt = makePattern(Pattern::Kind::Alt, first->pos);
    alt->args.push_back(std::move(first));
    while (atIdent("|")) {
        advance();
        alt->args.push_back(parsePattern1());
    }
    return alt;
}

// x: T | _: T | Pattern2. The type is a simple or a parenthesised type, so
// that `|` still separates alternatives.
PatternPtr Parser::parsePattern1() {
    if ((isVarId(peek()) || at(TokenKind::Underscore)) && peek(1).kind == TokenKind::Colon) {
        const Token& t = advance();
        advance();  // :
        auto typed = makePattern(Pattern::Kind::Typed, t.pos);
        const bool wildcard = t.kind == TokenKind::Underscore;
        auto inner = makePattern(wildcard ? Pattern::Kind::Wildcard : Pattern::Kind::Var, t.pos);
        if (!wildcard) inner->name = t.text;
        typed->args.push_back(std::move(inner));
        typed->type = at(TokenKind::LParen) ? parseType() : parseSimpleType();
        return typed;
    }
    return parsePattern2();
}

// Pattern2 ::= id `@` InfixPattern | InfixPattern
PatternPtr Parser::parsePattern2() {
    if (isVarId(peek()) && peek(1).kind == TokenKind::At) {
        const Token& t = advance();
        advance();  // @
        auto bind = makePattern(Pattern::Kind::Bind, t.pos);
        bind->name = t.text;
        bind->args.push_back(parseInfixPattern(0));
        return bind;
    }
    return parseInfixPattern(0);
}

// SimplePattern {op SimplePattern}: `h :: t` is `::(h, t)`; operators ending
// in ':' are right-associative. `|` and a final `*` are not operators here.
PatternPtr Parser::parseInfixPattern(int minPrec, int assocPrec) {
    PatternPtr lhs = parseSimplePattern();
    while (at(TokenKind::Identifier) && peek().isOperator && !peek().backquoted &&
           peek().text != "|" && !(peek().text == "*" && peek(1).kind == TokenKind::RParen)) {
        const Token& opTok = peek();
        const std::string op = opTok.text;
        const int p = precedence(op);
        if (p < minPrec) break;
        const bool right = isRightAssociative(op);
        if (assocPrec == p && !right) break;
        advance();
        PatternPtr rhs = right ? parseInfixPattern(p, p) : parseInfixPattern(p + 1);
        auto ex = makePattern(Pattern::Kind::Extractor, lhs->pos);
        ex->name = op;
        ex->expr = std::make_unique<Ident>(opTok.pos, op);
        ex->args.push_back(std::move(lhs));
        ex->args.push_back(std::move(rhs));
        lhs = std::move(ex);
    }
    return lhs;
}

PatternPtr Parser::parseSimplePattern() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::Underscore:
            advance();
            // `_*` (two tokens): a sequence wildcard; parsePatternArgs checks
            // that it is the last argument.
            if (atIdent("*") &&
                (peek(1).kind == TokenKind::RParen || peek(1).kind == TokenKind::Comma)) {
                advance();
                return makePattern(Pattern::Kind::SeqWildcard, t.pos);
            }
            return makePattern(Pattern::Kind::Wildcard, t.pos);
        case TokenKind::IntLit: case TokenKind::FloatLit: case TokenKind::StringLit:
        case TokenKind::CharLit: case TokenKind::KwTrue: case TokenKind::KwFalse:
        case TokenKind::KwNull: {
            auto lit = makePattern(Pattern::Kind::Literal, t.pos);
            lit->expr = parseSimple();
            if (lit->expr->kind != NodeKind::IntLit && lit->expr->kind != NodeKind::FloatLit &&
                lit->expr->kind != NodeKind::StringLit && lit->expr->kind != NodeKind::CharLit &&
                lit->expr->kind != NodeKind::BoolLit && lit->expr->kind != NodeKind::NullLit)
                fail("a literal pattern cannot be selected or applied", t);
            return lit;
        }
        case TokenKind::LParen: {
            advance();
            if (at(TokenKind::RParen)) {
                advance();
                auto unit = makePattern(Pattern::Kind::Literal, t.pos);
                unit->expr = std::make_unique<UnitLit>(t.pos);
                return unit;
            }
            PatternPtr first = parsePattern();
            if (at(TokenKind::RParen)) {
                advance();
                return first;
            }
            auto tuple = makePattern(Pattern::Kind::Tuple, t.pos);
            tuple->args.push_back(std::move(first));
            while (at(TokenKind::Comma)) {
                advance();
                tuple->args.push_back(parsePattern());
            }
            expect(TokenKind::RParen, "')'");
            return tuple;
        }
        case TokenKind::Identifier: {
            if (t.text == "-" && !t.backquoted &&
                (peek(1).kind == TokenKind::IntLit || peek(1).kind == TokenKind::FloatLit)) {
                auto lit = makePattern(Pattern::Kind::Literal, t.pos);
                lit->expr = parsePrefix();  // folds the sign into the literal
                if (lit->expr->kind != NodeKind::IntLit && lit->expr->kind != NodeKind::FloatLit)
                    fail("a literal pattern cannot be selected or applied", t);
                return lit;
            }
            if (isVarId(t) && peek(1).kind != TokenKind::Dot &&
                !(peek(1).kind == TokenKind::LParen && !peek(1).firstOnLine)) {
                advance();
                auto var = makePattern(Pattern::Kind::Var, t.pos);
                var->name = t.text;
                return var;
            }
            if (t.isOperator && !t.backquoted)
                fail("pattern expected but '" + t.text + "' found", t);
            // A stable path, possibly an extractor: A, a.B, A.B(p...), A[T](p...)
            NodePtr path = std::make_unique<Ident>(t.pos, t.text);
            std::string text = t.text;
            advance();
            while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
                advance();
                const Token& name = advance();
                text += "." + name.text;
                const SourcePos pp = path->pos;
                path = std::make_unique<Select>(pp, std::move(path), name.text);
            }
            if (at(TokenKind::LBracket)) {  // type arguments of an extractor: erased
                advance();
                parseType();
                while (at(TokenKind::Comma)) {
                    advance();
                    parseType();
                }
                expect(TokenKind::RBracket, "']'");
            }
            if (at(TokenKind::LParen) && !peek().firstOnLine) {
                advance();
                auto ex = makePattern(Pattern::Kind::Extractor, t.pos);
                ex->name = text;
                ex->expr = std::move(path);
                ex->args = parsePatternArgs();
                return ex;
            }
            auto stable = makePattern(Pattern::Kind::Stable, t.pos);
            stable->expr = std::move(path);
            return stable;
        }
        default:
            fail("pattern expected but '" + spelling(t) + "' found", t);
    }
}

// After `(`: patterns, the last one possibly a sequence wildcard
// (`_*`, `rest*`, `rest @ _*`); up to and including `)`.
std::vector<PatternPtr> Parser::parsePatternArgs() {
    std::vector<PatternPtr> args;
    while (!at(TokenKind::RParen)) {
        if (isVarId(peek()) && peek(1).kind == TokenKind::Identifier &&
            !peek(1).backquoted && peek(1).text == "*" && peek(2).kind == TokenKind::RParen) {
            auto seq = makePattern(Pattern::Kind::SeqWildcard, peek().pos);
            seq->name = advance().text;
            advance();  // *
            args.push_back(std::move(seq));
            break;
        }
        if (isVarId(peek()) && peek(1).kind == TokenKind::At &&
            peek(2).kind == TokenKind::Underscore && peek(3).kind == TokenKind::Identifier &&
            !peek(3).backquoted && peek(3).text == "*" && peek(4).kind == TokenKind::RParen) {
            auto seq = makePattern(Pattern::Kind::SeqWildcard, peek().pos);
            seq->name = advance().text;
            advance();  // @
            advance();  // _
            advance();  // *
            args.push_back(std::move(seq));
            break;
        }
        args.push_back(parsePattern());
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    for (std::size_t k = 0; k + 1 < args.size(); ++k)
        if (args[k]->kind == Pattern::Kind::SeqWildcard)
            fail("a sequence wildcard must be the last pattern argument", peek());
    return args;
}

// ---------------------------------------------------------------------------
// For-comprehensions

// for (enums) [yield|do] body | for { enums } [yield|do] body |
// for <Indent> enums <Outdent> (yield|do) body | for enums (yield|do) body
NodePtr Parser::parseFor() {
    auto node = std::make_unique<For>(advance().pos);
    bool delimited = true;
    if (at(TokenKind::LParen)) {
        advance();
        parseEnumerators(*node, TokenKind::RParen);
        expect(TokenKind::RParen, "')'");
    } else if (at(TokenKind::LBrace)) {
        advance();
        parseEnumerators(*node, TokenKind::RBrace);
        expect(TokenKind::RBrace, "'}'");
    } else if (at(TokenKind::Indent)) {
        advance();
        parseEnumerators(*node, TokenKind::Outdent);
        expect(TokenKind::Outdent, "end of the enumerators");
    } else {
        delimited = false;
        parseEnumerators(*node, TokenKind::EndOfFile);
    }
    if (at(TokenKind::Newline) &&
        (peek(1).kind == TokenKind::KwYield || peek(1).kind == TokenKind::KwDo))
        advance();
    if (at(TokenKind::KwYield)) {
        advance();
        node->isYield = true;
    } else if (at(TokenKind::KwDo)) {
        advance();
    } else if (!delimited) {
        fail("'do' or 'yield' expected", peek());
    }
    node->body = parseExprOrIndented();
    return node;
}

// Generator {(`;` | nl) Enumerator | Guard}: guards may follow a generator
// or another guard without a separator. Without delimiters (`terminator` is
// EndOfFile) the enumerators end at the end of the line.
void Parser::parseEnumerators(For& f, TokenKind terminator) {
    while (at(TokenKind::Newline)) advance();
    f.enums.push_back(parseGeneratorOrValue());
    if (f.enums.back().kind != Enumerator::Kind::Generator)
        fail("a for-comprehension must start with a generator `p <- e`", peek());
    for (;;) {
        if (at(TokenKind::KwIf)) {
            Enumerator g;
            g.kind = Enumerator::Kind::Guard;
            g.pos = advance().pos;
            g.expr = parseInfix(0);
            f.enums.push_back(std::move(g));
            continue;
        }
        if (at(TokenKind::Semicolon) ||
            (at(TokenKind::Newline) && terminator != TokenKind::EndOfFile)) {
            advance();
            while (at(TokenKind::Newline) || at(TokenKind::Semicolon)) advance();
            if (at(terminator)) return;
            if (at(TokenKind::KwIf)) continue;
            f.enums.push_back(parseGeneratorOrValue());
            continue;
        }
        return;
    }
}

// [case] Pattern1 `<-` Expr | Pattern1 `=` Expr
Enumerator Parser::parseGeneratorOrValue() {
    Enumerator en;
    en.pos = peek().pos;
    if (at(TokenKind::KwCase)) advance();
    en.pattern = parsePattern1();
    if (at(TokenKind::LeftArrow)) {
        advance();
        en.kind = Enumerator::Kind::Generator;
    } else if (at(TokenKind::Equals)) {
        advance();
        en.kind = Enumerator::Kind::Value;
    } else {
        fail("'<-' or '=' expected in a for-comprehension", peek());
    }
    en.expr = parseExprOrIndented();
    return en;
}

// ---------------------------------------------------------------------------
// Types

TypePtr Parser::parseType() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    if (t.kind == TokenKind::Arrow) {  // by-name `=> T`
        advance();
        auto by = makeType(TypeTree::Kind::ByName, t.pos);
        by->args.push_back(parseType());
        return by;
    }
    if (t.kind == TokenKind::LParen) {
        advance();
        std::vector<TypePtr> elems;
        if (!at(TokenKind::RParen)) {
            elems.push_back(parseType());
            while (at(TokenKind::Comma)) {
                advance();
                elems.push_back(parseType());
            }
        }
        expect(TokenKind::RParen, "')'");
        if (at(TokenKind::Arrow)) {
            advance();
            auto fn = makeType(TypeTree::Kind::Function, t.pos);
            fn->args = std::move(elems);
            fn->args.push_back(parseType());
            return fn;
        }
        if (elems.size() == 1) return std::move(elems.front());
        if (elems.empty()) fail("'=>' expected after '()' in a type", peek());
        auto tuple = makeType(TypeTree::Kind::Tuple, t.pos);
        tuple->args = std::move(elems);
        return tuple;
    }
    TypePtr ty = parseInfixType();
    if (at(TokenKind::Arrow)) {
        advance();
        auto fn = makeType(TypeTree::Kind::Function, ty->pos);
        fn->args.push_back(std::move(ty));
        fn->args.push_back(parseType());
        return fn;
    }
    return ty;
}

TypePtr Parser::parseInfixType() {
    TypePtr lhs = parseSimpleType();
    while (atIdent("|") || atIdent("&")) {
        const std::string op = advance().text;
        const SourcePos p = lhs->pos;
        auto infix = makeType(TypeTree::Kind::Infix, p, op);
        infix->args.push_back(std::move(lhs));
        infix->args.push_back(parseSimpleType());
        lhs = std::move(infix);
    }
    return lhs;
}

TypePtr Parser::parseSimpleType() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    TypePtr ty;
    if (t.kind == TokenKind::Underscore || (t.kind == TokenKind::Identifier && atIdent("?"))) {
        advance();
        ty = makeType(TypeTree::Kind::Wildcard, t.pos);
        if (t.kind == TokenKind::Underscore && atIdent("*")) {  // `xs: _*`
            advance();
            auto rep = makeType(TypeTree::Kind::Repeated, t.pos);
            rep->args.push_back(std::move(ty));
            return rep;
        }
        return ty;
    }
    if (t.kind != TokenKind::Identifier)
        fail("type expected but '" + spelling(t) + "' found", t);
    std::string name = advance().text;
    while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
        advance();
        name += '.';
        name += advance().text;
    }
    ty = makeType(TypeTree::Kind::Name, t.pos, std::move(name));
    if (at(TokenKind::LBracket)) {
        advance();
        ty->kind = TypeTree::Kind::Applied;
        ty->args.push_back(parseType());
        while (at(TokenKind::Comma)) {
            advance();
            ty->args.push_back(parseType());
        }
        expect(TokenKind::RBracket, "']'");
    }
    return ty;
}

// ---------------------------------------------------------------------------
// Source entry points

NodePtr parseExpressionSource(const std::string& src) {
    try {
        Parser p(tokenize(src));
        return p.parseSingleExpression();
    } catch (const LexError& e) {
        throw ParseError(e.what(), e.pos, e.atEof);
    }
}

std::unique_ptr<CompilationUnit> parseSource(const std::string& src) {
    try {
        Parser p(tokenize(src));
        return p.parseCompilationUnit();
    } catch (const LexError& e) {
        throw ParseError(e.what(), e.pos, e.atEof);
    }
}

} // namespace protoScala
