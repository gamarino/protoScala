/*
 * Parser — hand-written recursive descent with precedence climbing for infix
 * operators (DESIGN §3.3), over the complete laid-out token vector.
 */
#pragma once
#include "frontend/AST.h"
#include "frontend/Token.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

struct ParseError : std::runtime_error {
    ParseError(const std::string& msg, SourcePos p, bool eof)
        : std::runtime_error(msg), pos(p), atEof(eof) {}
    SourcePos pos;
    bool atEof;  // the error is "unexpected end of input": the REPL asks for more
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // The whole input must be one expression (unit tests, REPL probes).
    NodePtr parseSingleExpression();

    // Top-level statements of a script or REPL input (Task 4).
    std::unique_ptr<CompilationUnit> parseCompilationUnit();

private:
    std::vector<Token> toks_;
    std::size_t i_ = 0;

    const Token& peek(std::size_t k = 0) const;  // clamps at the final EOF token
    const Token& advance();
    bool at(TokenKind k) const { return peek().kind == k; }
    bool atIdent(const char* text) const;
    const Token& expect(TokenKind k, const char* what);
    [[noreturn]] void fail(const std::string& msg, const Token& at) const;
    [[noreturn]] void unsupported(const std::string& feature, const Token& at) const;
    bool skipOneNewline();  // consumes one Newline or Semicolon if present

    // Expressions
    NodePtr parseExpr();
    NodePtr parseExprOrIndented();
    NodePtr parseIf();
    NodePtr parseWhile();
    NodePtr parseReturn();
    bool lambdaAhead() const;
    std::vector<Param> parseLambdaParams();  // up to and including `=>`
    NodePtr parseLambda();
    // `assocPrec`/`assocRight`: the precedence and associativity of the
    // operator whose right operand is being parsed (-1: none), so mixed
    // associativity at one precedence level is detected across the recursion.
    NodePtr parseInfix(int minPrec, int assocPrec = -1, bool assocRight = false);
    NodePtr parseInfixRest(NodePtr lhs, int minPrec, int assocPrec = -1,
                           bool assocRight = false);
    NodePtr parsePrefix();
    NodePtr parseSimple();
    NodePtr parseSimpleRest(NodePtr base);
    NodePtr parseParensExpr();  // `(` ... `)`: unit, parens or tuple
    std::vector<NodePtr> parseArgs();  // after `(`, up to and including `)`
    NodePtr parseBlockExpr();          // `{` ... `}`
    NodePtr parseIndentedBlock();      // Indent ... Outdent
    std::unique_ptr<Block> parseBlockBody(TokenKind terminator, SourcePos pos);
    NodePtr parseBlockStat(TokenKind terminator);
    bool sameLineAhead(TokenKind k) const;

    // Types
    TypePtr parseType();
    TypePtr parseInfixType();
    TypePtr parseSimpleType();

    // Definitions (Task 4)
    NodePtr parseDefinition(std::vector<std::string> annotations);
    NodePtr parseValDef(SourcePos pos, bool isVar, bool isLazy);
    NodePtr parseDefDef(SourcePos pos, std::vector<std::string> annotations);
    std::vector<Param> parseParamClause();
    NodePtr parseImport();
    bool atDefinitionStart() const;
    void checkEndMarker(const Node& previous, const Token& marker) const;
};

int precedence(const std::string& op);
bool isRightAssociative(const std::string& op);
bool isAssignmentOperator(const std::string& op);

NodePtr parseExpressionSource(const std::string& src);
std::unique_ptr<CompilationUnit> parseSource(const std::string& src);

} // namespace protoScala
