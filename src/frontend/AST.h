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
    TemplateDef, New, Match, For,   // Phase 2
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
// Modifiers of a definition or class parameter. Only `private` changes the
// meaning of a program (D5: class-qualified member keys); the others are
// recorded for the checks the compiler does make (abstract, final, sealed).
struct Modifiers {
    bool isPrivate = false;    // also private[this] and private[pkg]
    bool isProtected = false;
    bool isOverride = false;
    bool isAbstract = false;
    bool isFinal = false;
    bool isSealed = false;
};

struct Param {
    std::string name;
    TypePtr type;          // may be null (lambda parameters)
    NodePtr defaultValue;  // may be null
    bool byName = false;   // x: => T
    bool repeated = false; // x: T*
    SourcePos pos;
    // Class parameters only: `val x: T`, `var x: T`, and their modifiers.
    bool isVal = false;
    bool isVar = false;
    Modifiers mods;
};
struct Lambda : Node {
    Lambda(SourcePos p) : Node(NodeKind::Lambda, p) {}
    std::vector<Param> params;
    NodePtr body;
    // Set by Desugar on the lambdas that carry the extra parameter lists of a
    // curried def (`def f(a)(b) = e` becomes `def f(a) = (b) => e`): the body
    // is the def's own body, so a `return` in it returns from the lambda.
    bool ownsReturn = false;
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
struct Pattern;
using PatternPtr = std::unique_ptr<Pattern>;

struct ValDef : Node {
    // Both defined in AST.cpp: Pattern is incomplete here, and a constructor
    // needs the destructors of the members it initialises.
    explicit ValDef(SourcePos p);
    ~ValDef() override;
    std::string name;               // empty when `pattern` is set
    bool isVar = false;
    bool isLazy = false;
    Modifiers mods;
    TypePtr type;                   // may be null
    NodePtr rhs;                    // null: an abstract member (templates only)
    PatternPtr pattern;             // `val (a, b) = e` (Task 2); null for a simple name
};
struct DefDef : Node {
    DefDef(SourcePos p) : Node(NodeKind::DefDef, p) {}
    std::string name;
    std::vector<std::string> annotations;  // e.g. "main", "tailrec"
    std::vector<std::string> typeParams;
    std::vector<std::vector<Param>> paramLists;  // empty: parameterless def
    TypePtr resultType;  // may be null
    NodePtr body;
    bool curried = false;  // set by Desugar when it folded extra parameter lists into lambdas
    Modifiers mods;
    bool synthetic = false;  // written by Desugar (case-class companions), not by the user
    // `body` is null for an abstract member (templates only). A DefDef named
    // "this" is an auxiliary constructor (templates only).
    // `@main` under any qualification (`@scala.main`): the last dot-segment
    // of the annotation name decides (annotations are not resolved in Phase 1).
    bool isMain() const {
        for (const auto& a : annotations) {
            const auto dot = a.rfind('.');
            if ((dot == std::string::npos ? a : a.substr(dot + 1)) == "main") return true;
        }
        return false;
    }
};
struct Import : Node { Import(SourcePos p) : Node(NodeKind::Import, p) {} std::string text; };

enum class TemplateKind : uint8_t { Class, Trait, Object };

// One entry of an `extends` clause: `B(args)`, `T`, `Option[A]`.
struct ParentRef {
    TypePtr type;                 // a Name or Applied type tree
    std::vector<NodePtr> args;    // constructor / trait arguments
    bool hasArgs = false;         // an argument list was written (possibly empty)
    SourcePos pos;
};

// class, trait, object, case class, case object.
struct TemplateDef : Node {
    TemplateDef(SourcePos p) : Node(NodeKind::TemplateDef, p) {}
    TemplateKind kind = TemplateKind::Class;
    bool isCase = false;
    Modifiers mods;
    std::string name;
    std::vector<std::string> typeParams;   // erased
    bool hasParamClause = false;           // `class C()` vs `class C`
    std::vector<Param> ctorParams;         // primary constructor (or trait) parameters
    std::vector<ParentRef> parents;        // the extends clause, superclass first
    std::string selfName;                  // `self =>` alias of `this`, or empty
    std::vector<NodePtr> body;             // template statements
    bool synthetic = false;                // a companion object created by Desugar
};

// new T(args)
struct New : Node {
    New(SourcePos p) : Node(NodeKind::New, p) {}
    TypePtr type;
    std::vector<NodePtr> args;
    bool hasArgs = false;
};

// A pattern (SLS 8). `expr` holds the literal of a Literal pattern and the
// path (Ident / Select) of a Stable or Extractor pattern; `args` the
// sub-patterns (Typed and Bind: exactly one).
struct Pattern {
    enum class Kind : uint8_t {
        Wildcard,     // _
        Var,          // x
        Literal,      // 1, "s", 'c', true, null, ()
        Stable,       // Nil, `x`, Color.Red: matched with ==
        Typed,        // x: T, _: T
        Bind,         // x @ p
        Alt,          // p1 | p2
        Extractor,    // C(p1, ...), h :: t
        Tuple,        // (p1, p2, ...)
        SeqWildcard,  // _*, rest*, rest @ _* (last argument of a sequence extractor)
    };
    Kind kind = Kind::Wildcard;
    SourcePos pos;
    std::string name;              // Var, Bind, SeqWildcard binder ("" for `_*`); Extractor: the path as written
    NodePtr expr;
    TypePtr type;                  // Typed
    std::vector<PatternPtr> args;
};

struct CaseDef {
    PatternPtr pattern;
    NodePtr guard;   // may be null
    NodePtr body;
    SourcePos pos;
};

struct Match : Node {
    Match(SourcePos p) : Node(NodeKind::Match, p) {}
    NodePtr scrutinee;
    std::vector<CaseDef> cases;
};

struct Enumerator {
    enum class Kind : uint8_t { Generator, Guard, Value };  // p <- e | if c | p = e
    Kind kind = Kind::Generator;
    PatternPtr pattern;   // null for a guard
    NodePtr expr;
    SourcePos pos;
};

struct For : Node {
    For(SourcePos p) : Node(NodeKind::For, p) {}
    std::vector<Enumerator> enums;
    NodePtr body;
    bool isYield = false;
};

std::string dump(const Pattern& p);

// Deep copies used by Desugar (a for-comprehension pattern appears in the
// withFilter lambda and in the map lambda). cloneSimpleExpr copies literal,
// Ident and Select trees only (what a pattern contains) and throws
// std::logic_error for anything else.
NodePtr cloneSimpleExpr(const Node& n);
PatternPtr clonePattern(const Pattern& p);
TypePtr cloneType(const TypeTree& t);

// Destroys a tree without recursion: an expression nested deeper than the
// native stack (a 200 000-term `a + b + ...` chain, which the parser builds
// iteratively) must not overflow it when it is freed. Owners of trees that
// may be arbitrarily deep use it (CompilationUnit, the parser's error paths).
void destroyTree(NodePtr root);

struct CompilationUnit {
    CompilationUnit() = default;
    CompilationUnit(const CompilationUnit&) = delete;
    CompilationUnit& operator=(const CompilationUnit&) = delete;
    ~CompilationUnit() {
        for (auto& s : stats) destroyTree(std::move(s));
    }
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
