/*
 * AST — the parser's output and the desugarer's input and output.
 *
 * A plain C++ tree owned by the frontend (DESIGN §3.3): it never holds a
 * ProtoObject*; the compiler materialises constants. Nodes carry the source
 * position of their first token. Type syntax is kept as TypeTree so later
 * phases can use it in patterns and isInstanceOf; the compiler ignores it.
 */
#pragma once
#include "frontend/Token.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala {

struct TypeTree {
    enum class Kind : uint8_t { Name, Applied, Function, Tuple, ByName, Repeated, Wildcard, Infix };
    Kind kind;
    std::string name;  // Name: dotted path; Infix: the operator; Applied: the type constructor
    std::vector<std::unique_ptr<TypeTree>> args;  // Function: params..., result (last)
    SourcePos pos;
};
using TypePtr = std::unique_ptr<TypeTree>;

enum class NodeKind : uint8_t {
    IntLit, FloatLit, StringLit, CharLit, BoolLit, NullLit, UnitLit, InterpString,
    Ident, Select, Apply, TypeApply, Infix, Prefix, Assign, If, While, Return,
    Block, Lambda, Typed, Parens, Tuple, Splice, NamedArg,
    ValDef, DefDef, Import,
};

struct Node {
    Node(NodeKind k, SourcePos p) : kind(k), pos(p) {}
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    NodeKind kind;
    SourcePos pos;
};
using NodePtr = std::unique_ptr<Node>;

struct IntLit : Node {
    IntLit(SourcePos p) : Node(NodeKind::IntLit, p) {}
    long long value = 0;
    bool fitsLong = true;
    std::string digits;  // exact literal when !fitsLong (with a leading '-' when negative)
    int base = 10;
};
struct FloatLit : Node { FloatLit(SourcePos p) : Node(NodeKind::FloatLit, p) {} double value = 0; std::string text; };
struct StringLit : Node { StringLit(SourcePos p) : Node(NodeKind::StringLit, p) {} std::string value; };
struct CharLit : Node { CharLit(SourcePos p) : Node(NodeKind::CharLit, p) {} char32_t value = 0; };
struct BoolLit : Node { BoolLit(SourcePos p, bool v) : Node(NodeKind::BoolLit, p), value(v) {} bool value; };
struct NullLit : Node { NullLit(SourcePos p) : Node(NodeKind::NullLit, p) {} };
struct UnitLit : Node { UnitLit(SourcePos p) : Node(NodeKind::UnitLit, p) {} };
struct InterpString : Node {
    InterpString(SourcePos p) : Node(NodeKind::InterpString, p) {}
    std::string interpolator;
    std::vector<InterpolationPart> parts;
};
struct Ident : Node {
    Ident(SourcePos p, std::string n) : Node(NodeKind::Ident, p), name(std::move(n)) {}
    std::string name;
};
struct Select : Node {
    Select(SourcePos p, NodePtr q, std::string n)
        : Node(NodeKind::Select, p), qualifier(std::move(q)), name(std::move(n)) {}
    NodePtr qualifier;
    std::string name;
};
struct Apply : Node {
    Apply(SourcePos p, NodePtr f) : Node(NodeKind::Apply, p), fn(std::move(f)) {}
    NodePtr fn;
    std::vector<NodePtr> args;
    bool blockArg = false;  // f { ... }
};
struct TypeApply : Node {
    TypeApply(SourcePos p, NodePtr f) : Node(NodeKind::TypeApply, p), fn(std::move(f)) {}
    NodePtr fn;
    std::vector<TypePtr> types;
};
struct Infix : Node {
    Infix(SourcePos p, NodePtr l, std::string o, NodePtr r)
        : Node(NodeKind::Infix, p), lhs(std::move(l)), op(std::move(o)), rhs(std::move(r)) {}
    NodePtr lhs;
    std::string op;
    NodePtr rhs;
};
struct Prefix : Node {
    Prefix(SourcePos p, std::string o, NodePtr e)
        : Node(NodeKind::Prefix, p), op(std::move(o)), operand(std::move(e)) {}
    std::string op;
    NodePtr operand;
};
struct Assign : Node {
    Assign(SourcePos p, NodePtr t, NodePtr v)
        : Node(NodeKind::Assign, p), target(std::move(t)), value(std::move(v)) {}
    NodePtr target, value;
};
struct If : Node {
    If(SourcePos p) : Node(NodeKind::If, p) {}
    NodePtr cond, thenp, elsep;  // elsep may be null until desugaring
};
struct While : Node { While(SourcePos p) : Node(NodeKind::While, p) {} NodePtr cond, body; };
struct Return : Node { Return(SourcePos p) : Node(NodeKind::Return, p) {} NodePtr value; /* may be null */ };
struct Block : Node {
    Block(SourcePos p) : Node(NodeKind::Block, p) {}
    std::vector<NodePtr> stats;  // statements, in order; the block's value is the last
                                 // one when it is an expression, else ()
};
struct Param {
    std::string name;
    TypePtr type;          // may be null (lambda parameters)
    NodePtr defaultValue;  // may be null
    bool byName = false;   // x: => T
    bool repeated = false; // x: T*
    SourcePos pos;
};
struct Lambda : Node {
    Lambda(SourcePos p) : Node(NodeKind::Lambda, p) {}
    std::vector<Param> params;
    NodePtr body;
};
struct Typed : Node {
    Typed(SourcePos p, NodePtr e, TypePtr t)
        : Node(NodeKind::Typed, p), expr(std::move(e)), type(std::move(t)) {}
    NodePtr expr;
    TypePtr type;
};
struct Parens : Node { Parens(SourcePos p, NodePtr e) : Node(NodeKind::Parens, p), expr(std::move(e)) {} NodePtr expr; };
struct Tuple : Node { Tuple(SourcePos p) : Node(NodeKind::Tuple, p) {} std::vector<NodePtr> elems; };
struct Splice : Node { Splice(SourcePos p, NodePtr e) : Node(NodeKind::Splice, p), expr(std::move(e)) {} NodePtr expr; };
struct NamedArg : Node {
    NamedArg(SourcePos p, std::string n, NodePtr v)
        : Node(NodeKind::NamedArg, p), name(std::move(n)), value(std::move(v)) {}
    std::string name;
    NodePtr value;
};
struct ValDef : Node {
    ValDef(SourcePos p) : Node(NodeKind::ValDef, p) {}
    std::string name;
    bool isVar = false;
    bool isLazy = false;
    TypePtr type;  // may be null
    NodePtr rhs;
};
struct DefDef : Node {
    DefDef(SourcePos p) : Node(NodeKind::DefDef, p) {}
    std::string name;
    std::vector<std::string> annotations;  // e.g. "main", "tailrec"
    std::vector<std::string> typeParams;
    std::vector<std::vector<Param>> paramLists;  // empty: parameterless def
    TypePtr resultType;  // may be null
    NodePtr body;
    bool isMain() const {
        for (const auto& a : annotations) if (a == "main") return true;
        return false;
    }
};
struct Import : Node { Import(SourcePos p) : Node(NodeKind::Import, p) {} std::string text; };

struct CompilationUnit {
    std::vector<NodePtr> stats;
};

// S-expression rendering used by the unit tests (format documented in AST.cpp).
std::string dump(const Node& n);
std::string dump(const TypeTree& t);
std::string dump(const CompilationUnit& u);

// Downcast helper: `as<Apply>(node)`; the caller has checked `kind`.
template <typename T> T& as(Node& n) { return static_cast<T&>(n); }
template <typename T> const T& as(const Node& n) { return static_cast<const T&>(n); }

} // namespace protoScala
