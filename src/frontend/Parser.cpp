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
    const bool eof = at.kind == TokenKind::EndOfFile;
    throw ParseError(eof ? "unexpected end of input" + (msg.empty() ? "" : ": " + msg) : msg,
                     at.pos, eof);
}

void Parser::unsupported(const std::string& feature, const Token& at) const {
    throw ParseError(feature + " is not implemented yet", at.pos, false);
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
    // Task 3: top-level expressions only; Task 4 adds definitions in parseBlockStat.
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

NodePtr Parser::parseExpr() {
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwIf:     return parseIf();
        case TokenKind::KwWhile:  return parseWhile();
        case TokenKind::KwReturn: return parseReturn();
        case TokenKind::KwDo:
            fail("do-while loops are not part of Scala 3; use while ... do", t);
        case TokenKind::KwThrow:  unsupported("throw", t);
        case TokenKind::KwTry:    unsupported("try", t);
        case TokenKind::KwFor:    unsupported("for comprehension", t);
        default: break;
    }
    if (lambdaAhead()) return parseLambda();
    NodePtr e = parseInfix(0);
    if (at(TokenKind::KwMatch)) unsupported("match", peek());
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
    if (k0 != TokenKind::LParen) return false;
    int depth = 0;
    for (std::size_t k = 0;; ++k) {
        const TokenKind kk = peek(k).kind;
        if (kk == TokenKind::EndOfFile) return false;
        if (kk == TokenKind::LParen) ++depth;
        if (kk == TokenKind::RParen && --depth == 0) return peek(k + 1).kind == TokenKind::Arrow;
    }
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
    return lhs;
}

NodePtr Parser::parsePrefix() {
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
        case TokenKind::KwNew:      unsupported("new", t);
        case TokenKind::KwThis:     unsupported("this", t);
        case TokenKind::KwSuper:    unsupported("super", t);
        case TokenKind::Underscore: unsupported("placeholder syntax '_'", t);
        case TokenKind::KwMatch:    unsupported("match", t);
        default:
            fail("expression expected but '" + spelling(t) + "' found", t);
    }
    return parseSimpleRest(std::move(base));
}

NodePtr Parser::parseSimpleRest(NodePtr base) {
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
            return base;
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
    auto block = std::make_unique<Block>(pos);
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
    return block;
}

NodePtr Parser::parseBlockStat(TokenKind terminator) {
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
        case NodeKind::DefDef: ok = d == as<DefDef>(previous).name; break;
        case NodeKind::ValDef: ok = d == "val" || d == as<ValDef>(previous).name; break;
        default: break;
    }
    if (!ok)
        fail("misaligned end marker: 'end " + d + "' does not close the preceding construct",
             marker);
}

// ---------------------------------------------------------------------------
// Types

TypePtr Parser::parseType() {
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
    const Token& t = peek();
    TypePtr ty;
    if (atIdent("_*")) {  // `xs: _*`, which the lexer reads as one mixed identifier
        advance();
        auto rep = makeType(TypeTree::Kind::Repeated, t.pos);
        rep->args.push_back(makeType(TypeTree::Kind::Wildcard, t.pos));
        return rep;
    }
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
