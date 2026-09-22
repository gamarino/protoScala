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
    // For each `(` token, the index of its matching `)` (or npos when it is
    // unclosed): lambdaAhead looks past a parenthesised list in O(1) instead
    // of rescanning it at every nesting level.
    std::vector<std::size_t> closingParen_;

    const Token& peek(std::size_t k = 0) const;  // clamps at the final EOF token
    const Token& advance();
    bool at(TokenKind k) const { return peek().kind == k; }
    bool atIdent(const char* text) const;
    const Token& expect(TokenKind k, const char* what);
    [[noreturn]] void fail(const std::string& msg, const Token& at) const;
    // "<feature> is not implemented yet", or "... are ..." when `plural`.
    [[noreturn]] void unsupported(const std::string& feature, const Token& at,
                                  bool plural = false) const;
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
    NodePtr parseInfixLoop(NodePtr& lhs, int minPrec, int assocPrec, bool assocRight);
    NodePtr parsePrefix();
    NodePtr parseSimple();
    NodePtr parseSimpleRest(NodePtr base);
    NodePtr parseSimpleLoop(NodePtr& base);
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

    // Templates (Phase 2)
    bool inTemplateBody_ = false;  // parsing the statements of a template body (abstract members allowed)
    Modifiers parseModifiers(bool* isLazy);
    NodePtr parseTemplateDef(Modifiers mods);           // at `case`, `class`, `trait` or `object`
    std::vector<Param> parseClassParamClause();         // after the name: `(` ... `)`
    std::vector<ParentRef> parseParents();              // after `extends`
    std::vector<NodePtr> parseTemplateBody(std::string* selfName);  // `{...}` or `:` + indented block
    NodePtr parseTemplateStat();
    std::vector<std::string> parseTypeParams();         // `[` ... `]`, variance and bounds erased
    NodePtr parseNew();                                 // at `new`

    // Placeholder syntax: one frame per Expr being parsed (SLS 6.23.2).
    std::vector<std::vector<std::string>> placeholderFrames_;
    int placeholderCounter_ = 0;
    int caseLambdaCounter_ = 0;
    NodePtr parseExprNoPlaceholders();   // an Expr without placeholder handling
    NodePtr placeholder(SourcePos pos);  // at `_`

    // Pattern matching and for-comprehensions (Phase 2)
    NodePtr parseMatch(NodePtr scrutinee);                // at `match`
    CaseDef parseCaseClause(TokenKind terminator);
    NodePtr parseCaseBody(TokenKind terminator);
    NodePtr parseCaseLambda(SourcePos pos, TokenKind terminator);  // at the first `case`
    PatternPtr parsePattern();                            // p1 | p2 | ...
    PatternPtr parsePattern1();                           // typed patterns
    PatternPtr parsePattern2();                           // x @ p
    PatternPtr parseInfixPattern(int minPrec, int assocPrec = -1);
    PatternPtr parseSimplePattern();
    std::vector<PatternPtr> parsePatternArgs();           // after `(`, up to and including `)`
    NodePtr parseFor();                                   // at `for`
    void parseEnumerators(For& f, TokenKind terminator);
    Enumerator parseGeneratorOrValue();
};

int precedence(const std::string& op);
bool isRightAssociative(const std::string& op);
bool isAssignmentOperator(const std::string& op);

NodePtr parseExpressionSource(const std::string& src);
std::unique_ptr<CompilationUnit> parseSource(const std::string& src);

} // namespace protoScala
