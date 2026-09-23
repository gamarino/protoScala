/*
 * Compiler — see Compiler.h. The essential invariants:
 *
 *  - Every emitted instruction goes through emit/emitJump, which keep the
 *    per-function operand-stack depth; control-flow joins restore the depth
 *    explicitly with adjust(). maxStack is the deepest point reached.
 *  - Captures are per activation: MAKE_FN copies the captured slot values
 *    each time it runs. Declarations the CaptureAnalysis pre-pass marks as
 *    boxed live in a Cell (MAKE_CELL at the start of their block), so every
 *    closure shares them (Scala var semantics; hoisted, mutually recursive
 *    local defs).
 */
#include "compiler/Compiler.h"
#include "runtime/StackGuard.h"

#include <stdexcept>

namespace protoScala {

namespace {

int line(SourcePos p) { return p.line; }

bool isFastBinary(const std::string& op, Op* out) {
    static const std::pair<const char*, Op> table[] = {
        {"+", Op::ADD}, {"-", Op::SUB}, {"*", Op::MUL}, {"<", Op::LT}, {"<=", Op::LE},
        {">", Op::GT}, {">=", Op::GE}, {"==", Op::EQ}, {"!=", Op::NE}};
    for (const auto& [name, op2] : table)
        if (op == name) { *out = op2; return true; }
    return false;
}

BindingKind kindOf(const ValDef& v) {
    return v.isLazy ? BindingKind::LazyVal : v.isVar ? BindingKind::Var : BindingKind::Val;
}

BindingKind kindOf(const DefDef& d) {
    return d.paramLists.empty() ? BindingKind::ParamlessDef : BindingKind::Def;
}

// Desugar collapses curried parameter lists, so a def has zero or one list.
const std::vector<Param>& paramsOf(const DefDef& d) {
    static const std::vector<Param> none;
    return d.paramLists.empty() ? none : d.paramLists[0];
}

const Node& bodyOf(const DefDef& d) {
    if (!d.body) throw CompileError("a method definition needs a body", d.pos);
    return *d.body;
}

const Node& rhsOf(const ValDef& v) {
    if (!v.rhs) throw CompileError("a value definition needs an initialiser", v.pos);
    return *v.rhs;
}

// A function literal has no name at its call sites and Scala's function types
// carry no by-name parameters either, so `(x: => T) => e` is refused.
void refuseByNameParams(const std::vector<Param>& params, const char* what) {
    for (const Param& p : params)
        if (p.byName)
            throw CompileError(std::string("a by-name parameter is not supported on ") + what +
                                   ": there is no name at the call site to resolve it against "
                                   "— take a function instead",
                               p.pos);
}

// ---------------------------------------------------------------------------
// CaptureAnalysis: decides which declarations of one function body live in a
// Cell. Scopes map names to their declaration and the function depth (0 =
// the analysed function, +1 per nested lambda / local def / lazy thunk)
// where they are bound. Local defs and lazy vals are hoisted: their function
// objects are created when the block starts, before any statement runs. A
// reference from a deeper function boxes:
//   - a var (every closure must see the same variable),
//   - a local def (hoisted, so it may be captured before it is stored),
//   - a val / lazy val when a hoisted function (local def or lazy thunk) lies
//     between the declaration and the reference (it is created before the
//     val's initialiser runs).
// Parameters (decl == nullptr) are never boxed: they are immutable and bound
// before any hoisted MAKE_FN runs.
//
// The pass also enforces Scala's forward-reference rule for blocks (SLS
// 6.11; Scala 3 ForwardDepChecks): a reference from statement i — directly or
// from any function nested in it — to a definition at index j >= i of the
// same block is illegal when a strict (non-lazy) val or var lies in [i, j].
// Legal forward references reach local defs and lazy vals only, which are
// hoisted, so no statement ever reads a slot before it is initialised.
// ---------------------------------------------------------------------------
class CaptureAnalysis {
public:
    CaptureAnalysis(std::unordered_set<const Node*>& boxed, const GlobalTable& globals)
        : boxed_(boxed), globals_(globals) {}

    void function(const std::vector<Param>& params, const Node& body, int depth, bool hoisted) {
        checkNativeStack(StackUse::Source);
        if (static_cast<int>(isHoistedLevel_.size()) <= depth) isHoistedLevel_.resize(depth + 1);
        isHoistedLevel_[depth] = hoisted;
        scopes_.emplace_back();
        for (const Param& p : params)
            scopes_.back().names[p.name] = Decl{nullptr, depth, DeclKind::Param, -1};
        walk(body, depth);
        scopes_.pop_back();
    }

private:
    enum class DeclKind : uint8_t { Param, Val, LazyVal, Var, Def };
    struct Decl {
        const Node* decl;
        int depth;
        DeclKind kind;
        int index;  // statement index in its block; -1 for parameters
    };
    struct Scope {
        std::unordered_map<std::string, Decl> names;
        const Block* block = nullptr;  // null for a parameter scope
        int current = -1;              // index of the block statement being walked
    };

    std::unordered_set<const Node*>& boxed_;
    const GlobalTable& globals_;
    std::vector<Scope> scopes_;
    std::vector<bool> isHoistedLevel_;

    const Decl* lookup(const std::string& name, const Scope** where) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto hit = it->names.find(name);
            if (hit != it->names.end()) { *where = &*it; return &hit->second; }
        }
        return nullptr;
    }

    bool hoistedBetween(int declDepth, int refDepth) const {
        for (int d = declDepth + 1; d <= refDepth; ++d)
            if (isHoistedLevel_[d]) return true;
        return false;
    }

    // "value x", "variable c", "lazy value z", "method f" (scalac wording).
    static std::string describe(DeclKind kind, const std::string& name) {
        switch (kind) {
            case DeclKind::Var: return "variable " + name;
            case DeclKind::LazyVal: return "lazy value " + name;
            case DeclKind::Def: return "method " + name;
            default: return "value " + name;
        }
    }

    void checkForwardReference(const Scope& scope, const Decl& d, const std::string& name,
                               SourcePos pos) const {
        if (!scope.block || d.index < scope.current) return;
        for (int k = scope.current; k <= d.index; ++k) {
            const Node& s = *scope.block->stats[static_cast<std::size_t>(k)];
            if (s.kind != NodeKind::ValDef || as<ValDef>(s).isLazy) continue;
            const auto& v = as<ValDef>(s);
            throw CompileError("forward reference to " + describe(d.kind, name) +
                                   " extends over the definition of " +
                                   describe(v.isVar ? DeclKind::Var : DeclKind::Val, v.name),
                               pos);
        }
    }

    void reference(const std::string& name, int depth, SourcePos pos) {
        const Scope* scope = nullptr;
        const Decl* d = lookup(name, &scope);
        if (!d || !d->decl) return;
        checkForwardReference(*scope, *d, name, pos);
        if (d->depth >= depth) return;
        const bool box = d->kind == DeclKind::Var || d->kind == DeclKind::Def ||
                         ((d->kind == DeclKind::Val || d->kind == DeclKind::LazyVal) &&
                          hoistedBetween(d->depth, depth));
        if (box) boxed_.insert(d->decl);
    }

    // The by-name arguments of a call whose callee expression is `fnNode`.
    // This pass runs before any name is resolved, so it deliberately
    // over-approximates: it asks the selector index for every mask ever
    // declared under that name (GlobalTable::anyByNameMaskOf), plus the
    // parameter lists of a `def` it can see in scope. An extra Cell costs a
    // word; a missing one would lose a write.
    std::uint32_t byNameMask(const Node& fnNode) const {
        const Node* fn = &fnNode;
        std::size_t list = 0;
        while (fn->kind == NodeKind::TypeApply || fn->kind == NodeKind::Apply) {
            if (fn->kind == NodeKind::Apply) { ++list; fn = as<Apply>(*fn).fn.get(); }
            else fn = as<TypeApply>(*fn).fn.get();
        }
        const std::string* selector = nullptr;
        if (fn->kind == NodeKind::Ident) selector = &as<Ident>(*fn).name;
        else if (fn->kind == NodeKind::Select) selector = &as<Select>(*fn).name;
        if (!selector) return 0;
        // A `def` in scope: its own lists, which are exact.
        const Scope* where = nullptr;
        if (const Decl* d = lookup(*selector, &where))
            if (d->kind == DeclKind::Def && d->decl) {
                const std::vector<std::uint32_t> masks = byNameMasksOfDef(as<DefDef>(*d->decl));
                return list < masks.size() ? masks[list] : 0;
            }
        return globals_.anyByNameMaskOf(*selector);
    }

    void walk(const Node* n, int depth) {
        if (n) walk(*n, depth);
    }

    void walk(const Node& n, int depth) {
        checkNativeStack(StackUse::Source);
        switch (n.kind) {
            case NodeKind::IntLit: case NodeKind::FloatLit: case NodeKind::StringLit:
            case NodeKind::CharLit: case NodeKind::BoolLit: case NodeKind::NullLit:
            case NodeKind::UnitLit: case NodeKind::InterpString: case NodeKind::Import:
                return;  // interpolations are rejected by the compiler
            case NodeKind::Ident: reference(as<Ident>(n).name, depth, n.pos); return;
            case NodeKind::Select: walk(as<Select>(n).qualifier.get(), depth); return;
            case NodeKind::Apply: {
                const auto& a = as<Apply>(n);
                walk(a.fn.get(), depth);
                // A by-name argument becomes a thunk at the call site (D47), so
                // it is a nested function here too: what it reads from an
                // enclosing scope has to be boxed exactly as a lambda's reads.
                const std::uint32_t mask = a.fn ? byNameMask(*a.fn) : 0;
                for (std::size_t k = 0; k < a.args.size(); ++k) {
                    if (k < kMaxByNameParams && (mask >> k) & 1u)
                        function({}, *a.args[k], depth + 1, /*hoisted=*/false);
                    else
                        walk(a.args[k].get(), depth);
                }
                return;
            }
            case NodeKind::TypeApply: walk(as<TypeApply>(n).fn.get(), depth); return;
            case NodeKind::Infix: {
                const auto& i = as<Infix>(n);
                walk(i.lhs.get(), depth);
                walk(i.rhs.get(), depth);
                return;
            }
            case NodeKind::Prefix: walk(as<Prefix>(n).operand.get(), depth); return;
            case NodeKind::Assign: {
                const auto& a = as<Assign>(n);
                walk(a.target.get(), depth);
                walk(a.value.get(), depth);
                return;
            }
            case NodeKind::If: {
                const auto& i = as<If>(n);
                walk(i.cond.get(), depth);
                walk(i.thenp.get(), depth);
                walk(i.elsep.get(), depth);
                return;
            }
            case NodeKind::While: {
                const auto& w = as<While>(n);
                walk(w.cond.get(), depth);
                walk(w.body.get(), depth);
                return;
            }
            case NodeKind::Return: walk(as<Return>(n).value.get(), depth); return;
            case NodeKind::Block: block(as<Block>(n), depth); return;
            case NodeKind::Lambda: {
                const auto& l = as<Lambda>(n);
                if (l.body) function(l.params, *l.body, depth + 1, /*hoisted=*/false);
                return;
            }
            case NodeKind::Typed: walk(as<Typed>(n).expr.get(), depth); return;
            case NodeKind::Parens: walk(as<Parens>(n).expr.get(), depth); return;
            case NodeKind::Tuple:
                for (const auto& e : as<Tuple>(n).elems) walk(e.get(), depth);
                return;
            case NodeKind::Splice: walk(as<Splice>(n).expr.get(), depth); return;
            case NodeKind::NamedArg: walk(as<NamedArg>(n).value.get(), depth); return;
            case NodeKind::ValDef: definitionBody(n, depth); return;
            case NodeKind::DefDef: definitionBody(n, depth); return;
            case NodeKind::New: {
                const auto& nw = as<New>(n);
                const std::uint32_t mask =
                    nw.type && nw.type->kind == TypeTree::Kind::Name
                        ? globals_.anyByNameMaskOf(nw.type->name)
                        : 0;
                for (std::size_t k = 0; k < nw.args.size(); ++k) {
                    if (k < kMaxByNameParams && (mask >> k) & 1u)
                        function({}, *nw.args[k], depth + 1, /*hoisted=*/false);
                    else
                        walk(nw.args[k].get(), depth);
                }
                return;
            }
            case NodeKind::Match: {
                const auto& m = as<Match>(n);
                walk(m.scrutinee.get(), depth);
                for (const CaseDef& c : m.cases) {
                    walkPatternPaths(*c.pattern, depth);  // stable identifiers and extractor objects
                    scopes_.emplace_back();
                    std::vector<std::string> vars;
                    patternVariables(*c.pattern, vars);
                    for (const auto& v : vars)
                        scopes_.back().names[v] = Decl{nullptr, depth, DeclKind::Param, -1};
                    walk(c.guard.get(), depth);
                    walk(c.body.get(), depth);
                    scopes_.pop_back();
                }
                return;
            }
            case NodeKind::TemplateDef:
            case NodeKind::For:
                return;  // templates are compiled on their own; For never survives Desugar
        }
    }

    // The expressions a pattern evaluates: the path of a stable identifier or
    // of an extractor object, and the value of a literal. Pattern variables
    // themselves are never boxed — like parameters they are bound once,
    // before any closure over them is created.
    void walkPatternPaths(const Pattern& p, int depth) {
        if (p.expr && (p.kind == Pattern::Kind::Stable || p.kind == Pattern::Kind::Extractor ||
                       p.kind == Pattern::Kind::Literal))
            walk(*p.expr, depth);
        for (const auto& a : p.args) walkPatternPaths(*a, depth);
    }

    // The initialiser of a val (a lazy one runs in a thunk) or a def body.
    void definitionBody(const Node& n, int depth) {
        if (n.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(n);
            if (!v.rhs) return;
            if (v.isLazy) function({}, *v.rhs, depth + 1, /*hoisted=*/true);
            else walk(*v.rhs, depth);
        } else {
            const auto& d = as<DefDef>(n);
            if (d.body) function(paramsOf(d), *d.body, depth + 1, /*hoisted=*/true);
        }
    }

    void block(const Block& b, int depth) {
        scopes_.emplace_back();
        scopes_.back().block = &b;
        for (std::size_t k = 0; k < b.stats.size(); ++k) {
            const Node& s = *b.stats[k];
            const int index = static_cast<int>(k);
            if (s.kind == NodeKind::ValDef) {
                const auto& v = as<ValDef>(s);
                const DeclKind kind = v.isLazy ? DeclKind::LazyVal
                                    : v.isVar  ? DeclKind::Var : DeclKind::Val;
                scopes_.back().names[v.name] = Decl{&s, depth, kind, index};
            } else if (s.kind == NodeKind::DefDef) {
                scopes_.back().names[as<DefDef>(s).name] = Decl{&s, depth, DeclKind::Def, index};
            }
        }
        // scopes_ may grow while walking; address the block scope by index.
        const std::size_t self = scopes_.size() - 1;
        for (std::size_t k = 0; k < b.stats.size(); ++k) {
            scopes_[self].current = static_cast<int>(k);
            walk(*b.stats[k], depth);
        }
        scopes_.pop_back();
    }
};

} // namespace

std::uint32_t byNameMaskOfParams(const std::vector<Param>& params) {
    std::uint32_t mask = 0;
    for (std::size_t k = 0; k < params.size(); ++k) {
        if (!params[k].byName) continue;
        if (k >= kMaxByNameParams)
            throw CompileError("a by-name parameter must be one of the first 32 parameters",
                               params[k].pos);
        if (params[k].repeated)
            throw CompileError("a repeated parameter cannot be by-name", params[k].pos);
        mask |= 1u << k;
    }
    return mask;
}

// The by-name masks of a def, one per parameter list. Desugar rewrote the extra
// lists of a curried def into nested lambdas (`def f(a)(b) = e` is
// `def f(a) = (b) => e`), so the chain is read back off the body.
std::vector<std::uint32_t> byNameMasksOfDef(const DefDef& d) {
    std::vector<std::uint32_t> masks{byNameMaskOfParams(paramsOf(d))};
    const Node* body = d.body.get();
    while (body && body->kind == NodeKind::Lambda && as<Lambda>(*body).fromCurriedDef) {
        const auto& l = as<Lambda>(*body);
        masks.push_back(byNameMaskOfParams(l.params));
        body = l.body.get();
    }
    while (masks.size() > 1 && masks.back() == 0) masks.pop_back();
    if (masks.size() == 1 && masks[0] == 0) masks.clear();
    return masks;
}

// ---------------------------------------------------------------------------
// Emission with stack-depth accounting
// ---------------------------------------------------------------------------

void Compiler::adjust(int delta) {
    fn_->depth += delta;
    if (fn_->depth < 0) throw std::logic_error("compiler: operand stack underflow");
    if (fn_->depth > fn_->maxDepth) fn_->maxDepth = fn_->depth;
}

void Compiler::emit(Op op, std::uint64_t operand, SourcePos pos, int stackEffect) {
    fn_->mod->emit(op, operand, line(pos));
    adjust(stackEffect);
}

std::size_t Compiler::emitJump(Op op, SourcePos pos, int stackEffect) {
    const std::size_t at = fn_->mod->emitJump(op, line(pos));
    adjust(stackEffect);
    return at;
}

// ---------------------------------------------------------------------------
// Scopes and name resolution
// ---------------------------------------------------------------------------

Compiler::LocalInfo Compiler::declareLocal(const std::string& name, BindingKind kind, bool boxed,
                                           std::vector<std::uint32_t> byNameMasks) {
    LocalInfo info{newSlot(), kind, boxed, false, std::move(byNameMasks)};
    fn_->scopes.back()[name] = info;
    return info;
}

const Compiler::LocalInfo* Compiler::findInFunction(FunctionState* f, const std::string& name) {
    for (auto it = f->scopes.rbegin(); it != f->scopes.rend(); ++it) {
        auto hit = it->find(name);
        if (hit != it->end()) return &hit->second;
    }
    return nullptr;
}

// Makes `name` (bound in some function enclosing `f`) available in `f`, adding
// a capture in every function between the binding and `f`. The capture slot
// is recorded in `f`'s outermost scope so later references reuse it.
Compiler::LocalInfo Compiler::captureInto(FunctionState* f, const std::string& name,
                                          SourcePos pos, bool* found) {
    if (const LocalInfo* own = findInFunction(f, name)) { *found = true; return *own; }
    if (!f->parent) { *found = false; return {}; }
    LocalInfo outer = captureInto(f->parent, name, pos, found);
    if (!*found) return {};
    LocalInfo mine{f->nextSlot++, outer.kind, outer.boxed, /*captured=*/true, outer.byNameMasks};
    f->mod->addCapture(outer.slot, mine.slot);
    f->scopes.front()[name] = mine;
    return mine;
}

// A bare name inside a template resolves: local (including `this`, the self
// alias and the constructor parameters inside the constructor) -> member of
// the template (own, inherited, Any's) -> global.
Compiler::Resolution Compiler::resolve(const std::string& name, SourcePos pos) {
    bool found = false;
    LocalInfo info = captureInto(fn_, name, pos, &found);
    if (found) {
        const BindingKind kind = info.kind;
        std::vector<std::uint32_t> masks = info.byNameMasks;
        return Resolution{RefKind::Local, std::move(info), kind, {}, nullptr, std::move(masks)};
    }
    if (const MemberInfo* m = memberOf(name))
        return Resolution{RefKind::Member, {}, BindingKind::Val, m->key, m, m->byNameMasks};
    if (const GlobalBinding* g = globals_.binding(name))
        return Resolution{RefKind::Global, {}, g->kind, g->key, nullptr, g->byNameMasks};
    if (name == "this")
        throw CompileError("this can be used only inside a class, trait or object", pos);
    if (name == "super") throw CompileError("'super' must be followed by a member selection", pos);
    throw CompileError("Not found: " + name, pos);
}

const std::string& Compiler::globalKey(const std::string& name) const {
    return globals_.binding(name)->key;  // declared in compileUnit step 1
}

void Compiler::loadLocal(const LocalInfo& info, SourcePos pos) {
    emit(info.boxed ? Op::PUSH_CELL : Op::PUSH_LOCAL, static_cast<std::uint64_t>(info.slot),
         pos, +1);
}

void Compiler::storeLocal(const LocalInfo& info, SourcePos pos) {
    emit(info.boxed ? Op::STORE_CELL : Op::STORE_LOCAL, static_cast<std::uint64_t>(info.slot),
         pos, -1);
}

void Compiler::analyseCaptures(const std::vector<Param>& params, const Node& body) {
    CaptureAnalysis(boxed_, globals_).function(params, body, 0, /*hoisted=*/false);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

void Compiler::compileExpr(const Node& n) {
    checkNativeStack(StackUse::Source);
    switch (n.kind) {
        case NodeKind::IntLit: {
            const auto& i = as<IntLit>(n);
            const std::size_t k = i.fitsLong ? fn_->mod->addInt(i.value)
                                             : fn_->mod->addBigInt(i.digits, i.base);
            emit(Op::PUSH_CONST, k, n.pos, +1);
            return;
        }
        case NodeKind::FloatLit:
            emit(Op::PUSH_CONST, fn_->mod->addDouble(as<FloatLit>(n).value), n.pos, +1);
            return;
        case NodeKind::StringLit:
            emit(Op::PUSH_CONST, fn_->mod->addString(as<StringLit>(n).value), n.pos, +1);
            return;
        case NodeKind::CharLit:
            emit(Op::PUSH_CONST, fn_->mod->addChar(as<CharLit>(n).value), n.pos, +1);
            return;
        case NodeKind::BoolLit:
            emit(as<BoolLit>(n).value ? Op::PUSH_TRUE : Op::PUSH_FALSE, 0, n.pos, +1);
            return;
        case NodeKind::NullLit: emit(Op::PUSH_NULL, 0, n.pos, +1); return;
        case NodeKind::UnitLit: emit(Op::PUSH_UNIT, 0, n.pos, +1); return;
        case NodeKind::InterpString:
            throw CompileError("string interpolation is not implemented yet", n.pos);
        case NodeKind::Tuple: compileTuple(as<Tuple>(n)); return;
        case NodeKind::Splice:
            throw CompileError("a splice must be the last argument of a function call", n.pos);
        case NodeKind::NamedArg:
            throw CompileError("named arguments are not implemented yet", n.pos);
        case NodeKind::Ident: compileIdent(as<Ident>(n)); return;
        case NodeKind::Select: compileSelect(as<Select>(n)); return;
        case NodeKind::Apply: compileApply(as<Apply>(n)); return;
        case NodeKind::TypeApply: {
            const auto& ta = as<TypeApply>(n);
            if (ta.fn->kind == NodeKind::Select && ta.types.size() == 1) {
                const auto& sel = as<Select>(*ta.fn);
                if (sel.name == "isInstanceOf" || sel.name == "asInstanceOf") {
                    compileInstanceOf(*sel.qualifier, *ta.types[0], sel.name == "asInstanceOf", n.pos);
                    return;
                }
            }
            compileExpr(*ta.fn);  // other type arguments are erased
            return;
        }
        case NodeKind::Assign: compileAssign(as<Assign>(n)); return;
        case NodeKind::If: compileIf(as<If>(n)); return;
        case NodeKind::While: compileWhile(as<While>(n)); return;
        case NodeKind::Return: compileReturn(as<Return>(n)); return;
        case NodeKind::Block: compileBlock(as<Block>(n)); return;
        case NodeKind::Lambda: {
            const auto& l = as<Lambda>(n);
            if (!l.body) throw CompileError("a function literal needs a body", n.pos);
            // A curried def's lambda owns its body's `return`s (Desugar), and
            // carries a real parameter list, so it may declare by-name
            // parameters (D47).
            compileFunction("<lambda>", l.params, *l.body,
                            l.ownsReturn ? FnShape::Def : FnShape::Lambda, n.pos,
                            /*paramless=*/false, /*allowByName=*/l.fromCurriedDef);
            return;
        }
        case NodeKind::ValDef:
        case NodeKind::DefDef:
        case NodeKind::Import:
            throw CompileError("definition used as an expression", n.pos);
        case NodeKind::TemplateDef:
            throw CompileError("classes, traits and objects must be defined at the top level "
                               "of a file", n.pos);
        case NodeKind::New: compileNew(as<New>(n)); return;
        case NodeKind::Match: compileMatch(as<Match>(n)); return;
        case NodeKind::For: throw std::logic_error("compiler: for-comprehension not desugared");
        case NodeKind::Infix:
        case NodeKind::Prefix:
        case NodeKind::Parens:
        case NodeKind::Typed:
            throw std::logic_error("compiler: node kind not desugared");
    }
}

void Compiler::compileIdent(const Ident& id) {
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Member) {  // this.name (virtual: an override in a subclass wins)
        loadThis(id.pos);
        emit(Op::SEND, fn_->mod->addSendSite(r.key, 0), id.pos, 0);
        // A by-name constructor parameter is a field holding the thunk (D47).
        if (r.member && r.member->byNameValue) emit(Op::FORCE_THUNK, 0, id.pos, 0);
        return;
    }
    if (r.ref == RefKind::Local) loadLocal(r.local, id.pos);
    else emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(r.key), id.pos, +1);
    // A by-name parameter holds the thunk the call site built: every read runs
    // it, as Scala re-evaluates a by-name argument at every use (D47).
    if (r.kind == BindingKind::ByNameParam) emit(Op::FORCE_THUNK, 0, id.pos, 0);
    else if (r.kind == BindingKind::ParamlessDef) emit(Op::CALL, 0, id.pos, 0);
    else if (r.kind == BindingKind::LazyVal || r.kind == BindingKind::Object)
        emit(Op::FORCE, 0, id.pos, 0);
}

// The by-name masks of the member `name` of the template being compiled.
const std::vector<std::uint32_t>* Compiler::byNameMasksOfMember(const std::string& name) const {
    const MemberInfo* m = memberOf(name);
    return m && !m->byNameMasks.empty() ? &m->byNameMasks : nullptr;
}

// The by-name masks of `member` declared by `object objectName` (or by the
// companion object of that name). The class of `object O` is the type `O.type`.
const std::vector<std::uint32_t>* Compiler::byNameMasksOfObjectMember(
    const std::string& objectName, const std::string& member) const {
    const ClassInfo* cls = globals_.findType(objectName + ".type");
    if (!cls) return nullptr;
    auto it = cls->members.find(member);
    if (it == cls->members.end() || it->second.byNameMasks.empty()) return nullptr;
    return &it->second.byNameMasks;
}

// The masks of the declaration the callee *head* names — the expression left
// after every application layer has been peeled off (D47). A head the compiler
// cannot resolve to a declaration has none, and its arguments are evaluated
// (D53).
const std::vector<std::uint32_t>* Compiler::byNameMasksOfCalleeHead(const Node& head) {
    const Node* fn = &head;
    while (fn->kind == NodeKind::TypeApply) fn = as<TypeApply>(*fn).fn.get();
    if (fn->kind == NodeKind::Ident) {
        const std::string& name = as<Ident>(*fn).name;
        bool found = false;
        const LocalInfo info = captureInto(fn_, name, fn->pos, &found);
        if (found) {
            if (info.byNameMasks.empty()) return nullptr;
            // The vector lives in the enclosing FunctionState's scope map, which
            // outlives this call.
            const LocalInfo* own = findInFunction(fn_, name);
            return own && !own->byNameMasks.empty() ? &own->byNameMasks : nullptr;
        }
        if (const std::vector<std::uint32_t>* m = byNameMasksOfMember(name)) return m;
        const GlobalBinding* g = globals_.binding(name);
        if (!g) return nullptr;
        if (!g->byNameMasks.empty()) return &g->byNameMasks;
        // `C(args)` on a class name: its primary constructor (a case class's
        // synthesised companion apply, or the universal creator apply).
        if (g->kind == BindingKind::Object)
            if (const ClassInfo* cls = globals_.findType(name))
                if (!cls->primaryByNameMasks.empty()) return &cls->primaryByNameMasks;
        return nullptr;
    }
    if (fn->kind != NodeKind::Select) return nullptr;
    const auto& sel = as<Select>(*fn);
    if (sel.qualifier->kind != NodeKind::Ident) return nullptr;
    const std::string& qual = as<Ident>(*sel.qualifier).name;
    if (qual == "this") return byNameMasksOfMember(sel.name);
    if (qual == "super") return nullptr;
    bool found = false;
    captureInto(fn_, qual, fn->pos, &found);
    if (found || memberOf(qual)) return nullptr;  // a local or a field: dynamic
    const GlobalBinding* g = globals_.binding(qual);
    if (!g) return nullptr;
    // A builtin object declares the by-name signature of its `apply` (D47).
    if (g->kind == BindingKind::Builtin)
        return sel.name == "apply" && !g->byNameMasks.empty() ? &g->byNameMasks : nullptr;
    if (g->kind != BindingKind::Object) return nullptr;
    return byNameMasksOfObjectMember(qual, sel.name);
}

// Bit k set: argument k of the application whose callee expression is `fnNode`
// is by-name (D47). The head is reached by peeling application layers, so a
// curried `def f(a)(b: => T)` is resolved at the list that carries it.
std::uint32_t Compiler::byNameMaskOfCallee(const Node& fnNode) {
    const Node* fn = &fnNode;
    std::size_t list = 0;
    while (fn->kind == NodeKind::TypeApply || fn->kind == NodeKind::Apply) {
        if (fn->kind == NodeKind::Apply) { ++list; fn = as<Apply>(*fn).fn.get(); }
        else fn = as<TypeApply>(*fn).fn.get();
    }
    const std::vector<std::uint32_t>* masks = byNameMasksOfCalleeHead(*fn);
    return masks && list < masks->size() ? (*masks)[list] : 0;
}

void Compiler::compileApply(const Apply& a) {
    // Type arguments are erased: recv.m[T](args) is a send like recv.m(args).
    const Node* fn = a.fn.get();
    while (fn->kind == NodeKind::TypeApply) fn = as<TypeApply>(*fn).fn.get();
    const std::uint32_t byName = byNameMaskOfCallee(*a.fn);
    bool named = false;
    for (const auto& arg : a.args) named = named || arg->kind == NodeKind::NamedArg;
    if (named) {
        if (fn->kind != NodeKind::Select)
            throw CompileError("named arguments are not implemented yet", a.pos);
        compileNamedSend(as<Select>(*fn), a.args, a.pos);
        return;
    }
    if (fn->kind == NodeKind::Select) {
        const auto& sel = as<Select>(*fn);
        if (sel.qualifier->kind == NodeKind::Ident &&
            as<Ident>(*sel.qualifier).name == "super") {
            compileSuperSend(sel.name, a.args, a.pos);
            return;
        }
        if (a.args.size() == 1 && (sel.name == "&&" || sel.name == "||")) {
            compileShortCircuit(*sel.qualifier, *a.args[0], sel.name == "&&", a.pos);
            return;
        }
        Op fast;
        if (a.args.size() == 1 && a.args[0]->kind != NodeKind::Splice &&
            isFastBinary(sel.name, &fast)) {
            compileExpr(*sel.qualifier);
            compileExpr(*a.args[0]);
            emit(fast, 0, a.pos, -1);
            return;
        }
        compileExpr(*sel.qualifier);
        for (std::size_t k = 0; k < a.args.size(); ++k) {
            const Node& arg = *a.args[k];
            if (arg.kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg.pos);
            if (k < kMaxByNameParams && (byName >> k) & 1u) compileByNameArgument(arg);
            else compileExpr(arg);
        }
        const auto n = static_cast<std::uint32_t>(a.args.size());
        emit(Op::SEND_APPLY, sendSite(sel.name, n), a.pos, -static_cast<int>(n));
        return;
    }
    if (fn->kind == NodeKind::Ident) {
        const auto& id = as<Ident>(*fn);
        const Resolution r = resolve(id.name, id.pos);
        if (r.ref == RefKind::Member) {  // f(args) inside a template: this.f(args)
            loadThis(a.pos);
            for (std::size_t k = 0; k < a.args.size(); ++k) {
                const Node& arg = *a.args[k];
                if (arg.kind == NodeKind::Splice)
                    throw CompileError("splices are only supported in function calls", arg.pos);
                if (k < kMaxByNameParams && (byName >> k) & 1u) compileByNameArgument(arg);
                else compileExpr(arg);
            }
            const auto n = static_cast<std::uint32_t>(a.args.size());
            emit(Op::SEND_APPLY, fn_->mod->addSendSite(r.key, n), a.pos, -static_cast<int>(n));
            return;
        }
        if (r.ref == RefKind::Global && r.kind == BindingKind::Object) {
            // C(args) where C's companion apply is the synthesised one: new C(args).
            const ClassInfo* cls = globals_.findType(id.name);
            if (cls && cls->isCase && cls->kind == ClassKind::Class &&
                cls->companionTermKey == r.key && !cls->companionHasApply) {
                compileNewOf(*cls, a.args, a.pos);
                return;
            }
        }
    }
    compileExpr(*fn);
    compileArgsAndCall(a.args, a.pos, byName);
}

// A by-name argument is compiled as a zero-argument function; the callee's
// every read of the parameter runs it (D47).
void Compiler::compileByNameArgument(const Node& arg) {
    compileFunction("<by-name>", {}, arg, FnShape::Lambda, arg.pos);
}

void Compiler::compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos,
                                  std::uint32_t byNameMask) {
    const bool spread = !args.empty() && args.back()->kind == NodeKind::Splice;
    for (std::size_t k = 0; k < args.size(); ++k) {
        const Node& arg = *args[k];
        const bool isByName = k < kMaxByNameParams && ((byNameMask >> k) & 1u) != 0;
        if (arg.kind == NodeKind::Splice) {
            if (k + 1 != args.size())
                throw CompileError("a splice must be the last argument", arg.pos);
            if (isByName)
                throw CompileError("a splice cannot supply a by-name parameter", arg.pos);
            compileExpr(*as<Splice>(arg).expr);
        } else if (isByName) {
            compileByNameArgument(arg);
        } else {
            compileExpr(arg);
        }
    }
    const int n = static_cast<int>(args.size());
    // CALL_SPREAD n: [f a1..an list] -> [r], so both forms pop n+1 and push 1.
    if (spread) emit(Op::CALL_SPREAD, static_cast<std::uint64_t>(n - 1), pos, -n);
    else        emit(Op::CALL, static_cast<std::uint64_t>(n), pos, -n);
}

void Compiler::compileSelect(const Select& s) {
    if (s.qualifier->kind == NodeKind::Ident && as<Ident>(*s.qualifier).name == "super") {
        compileSuperSend(s.name, {}, s.pos);
        return;
    }
    compileExpr(*s.qualifier);
    if (s.name == "unary_-") { emit(Op::NEG, 0, s.pos, 0); return; }
    if (s.name == "unary_!") { emit(Op::NOT, 0, s.pos, 0); return; }
    emit(Op::SEND, sendSite(s.name, 0), s.pos, 0);
    // A by-name constructor parameter of the template being compiled is a
    // private field holding the thunk, so `this.v` forces it too (D47).
    const MemberInfo* m = memberOf(s.name);
    if (m && m->byNameValue) emit(Op::FORCE_THUNK, 0, s.pos, 0);
}

// a && b  ==>  a; JUMP_IF_FALSE Lf; b; JUMP Lend; Lf: PUSH_FALSE; Lend:
// a || b  ==>  a; JUMP_IF_TRUE  Lt; b; JUMP Lend; Lt: PUSH_TRUE;  Lend:
void Compiler::compileShortCircuit(const Node& lhs, const Node& rhs, bool isAnd, SourcePos pos) {
    compileExpr(lhs);
    const std::size_t toShort = emitJump(isAnd ? Op::JUMP_IF_FALSE : Op::JUMP_IF_TRUE, pos, -1);
    compileExpr(rhs);
    const std::size_t toEnd = emitJump(Op::JUMP, pos, 0);
    fn_->mod->patchJumpTo(toShort, fn_->mod->pos());
    adjust(-1);  // the rhs value is not on the stack on this path
    emit(isAnd ? Op::PUSH_FALSE : Op::PUSH_TRUE, 0, pos, +1);
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
}

void Compiler::compileAssign(const Assign& a) {
    // Desugar rewrote `o.x = v` and `f(i) = v` into sends (DESIGN 3.4).
    if (a.target->kind != NodeKind::Ident)
        throw std::logic_error("compiler: assignment target not desugared");
    const auto& id = as<Ident>(*a.target);
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Member) {  // a var field: this.x_=(v), which yields ()
        if (r.member->kind != MemberKind::Var)
            throw CompileError("Reassignment to val " + id.name, a.pos);
        const MemberInfo* setter = memberOf(setterName(id.name));
        if (!setter)
            throw CompileError(id.name + " cannot be assigned here: it has no setter", a.pos);
        loadThis(a.pos);
        compileExpr(*a.value);
        emit(Op::SEND, fn_->mod->addSendSite(setter->key, 1), a.pos, -1);
        return;
    }
    if (r.kind != BindingKind::Var) throw CompileError("Reassignment to val " + id.name, a.pos);
    compileExpr(*a.value);
    if (r.ref == RefKind::Local) {
        if (r.local.captured && !r.local.boxed)
            throw std::logic_error("compiler: captured var is not boxed");
        storeLocal(r.local, a.pos);
    } else {
        emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(r.key), a.pos, -1);
    }
    emit(Op::PUSH_UNIT, 0, a.pos, +1);
}

void Compiler::compileIf(const If& i) {
    compileExpr(*i.cond);
    const std::size_t toElse = emitJump(Op::JUMP_IF_FALSE, i.pos, -1);
    compileExpr(*i.thenp);                       // depth d+1
    const std::size_t toEnd = emitJump(Op::JUMP, i.pos, 0);
    fn_->mod->patchJumpTo(toElse, fn_->mod->pos());
    adjust(-1);                                  // else branch starts at depth d
    compileExpr(*i.elsep);                       // back to d+1
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
}

void Compiler::compileWhile(const While& w) {
    const std::size_t top = fn_->mod->pos();
    compileExpr(*w.cond);
    const std::size_t toEnd = emitJump(Op::JUMP_IF_FALSE, w.pos, -1);
    compileExpr(*w.body);
    emit(Op::POP, 0, w.pos, -1);
    fn_->mod->emitJumpBack(top, line(w.pos));
    fn_->mod->patchJumpTo(toEnd, fn_->mod->pos());
    emit(Op::PUSH_UNIT, 0, w.pos, +1);
}

void Compiler::compileReturn(const Return& r) {
    if (!fn_->allowsReturn)
        throw CompileError(fn_->isTopLevel ? "return outside a method definition"
                                           : "return inside a lambda is not supported",
                           r.pos);
    if (r.value) compileExpr(*r.value);
    else emit(Op::PUSH_UNIT, 0, r.pos, +1);
    emit(Op::RETURN, 0, r.pos, -1);
    adjust(+1);  // the expression `return e` has type Nothing; keep the stack shape
}

void Compiler::compileBlock(const Block& b) {
    // Two definitions of one name in the same block are an error; shadowing
    // in a nested block is legal. `_` may be bound any number of times.
    {
        std::unordered_set<std::string> seen;
        for (const auto& s : b.stats) {
            const std::string* name = nullptr;
            if (s->kind == NodeKind::ValDef) name = &as<ValDef>(*s).name;
            else if (s->kind == NodeKind::DefDef) name = &as<DefDef>(*s).name;
            if (name && *name != "_" && !seen.insert(*name).second)
                throw CompileError(*name + " is already defined", s->pos);
        }
    }
    compileStats(b.stats, 0, b.pos);
}

// The body of a block, from statement `from` (an auxiliary constructor skips
// its leading `this(...)` call). Leaves exactly one value on the stack.
void Compiler::compileStats(const std::vector<NodePtr>& stats, std::size_t from, SourcePos pos) {
    fn_->scopes.emplace_back();
    // 1. Declare every definition of the block; boxed ones get a Cell now.
    for (std::size_t k = from; k < stats.size(); ++k) {
        const NodePtr& s = stats[k];
        if (s->kind == NodeKind::DefDef) {
            const auto& d = as<DefDef>(*s);
            const LocalInfo info =
                declareLocal(d.name, kindOf(d), boxed_.count(s.get()) > 0, byNameMasksOfDef(d));
            if (info.boxed) emit(Op::MAKE_CELL, static_cast<std::uint64_t>(info.slot), d.pos, 0);
        } else if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            const LocalInfo info = declareLocal(v.name, kindOf(v), boxed_.count(s.get()) > 0);
            if (info.boxed) emit(Op::MAKE_CELL, static_cast<std::uint64_t>(info.slot), v.pos, 0);
        }
    }
    // 2. Hoisted local defs and lazy vals (their thunks), in order.
    for (std::size_t k = from; k < stats.size(); ++k) {
        const NodePtr& s = stats[k];
        if (s->kind == NodeKind::DefDef) {
            const auto& d = as<DefDef>(*s);
            compileFunction(d.name, paramsOf(d), bodyOf(d), FnShape::Def, d.pos,
                            /*paramless=*/false, /*allowByName=*/true);
            storeLocal(*findInFunction(fn_, d.name), d.pos);
        } else if (s->kind == NodeKind::ValDef && as<ValDef>(*s).isLazy) {
            const auto& v = as<ValDef>(*s);
            compileLazyThunk(rhsOf(v), v.pos);
            storeLocal(*findInFunction(fn_, v.name), v.pos);
        }
    }
    // 3. Statements in order; the last expression is the block's value.
    bool valueOnStack = false;
    for (std::size_t k = from; k < stats.size(); ++k) {
        const Node& s = *stats[k];
        const bool last = (k + 1 == stats.size());
        if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import) continue;
        if (s.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(s);
            if (v.isLazy) continue;  // hoisted (step 2)
            compileExpr(rhsOf(v));
            storeLocal(*findInFunction(fn_, v.name), v.pos);
            continue;
        }
        compileExpr(s);
        if (last) valueOnStack = true;
        else emit(Op::POP, 0, s.pos, -1);
    }
    if (!valueOnStack) emit(Op::PUSH_UNIT, 0, pos, +1);
    fn_->scopes.pop_back();
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

void Compiler::compileFunction(const std::string& name, const std::vector<Param>& params,
                               const Node& body, FnShape shape, SourcePos pos,
                               bool paramless, bool allowByName) {
    for (const Param& p : params)
        if (p.defaultValue)
            throw CompileError("default parameter values are not implemented yet", p.pos);
    if (!allowByName) refuseByNameParams(params, "a function literal");
    const std::uint32_t byNameMask = allowByName ? byNameMaskOfParams(params) : 0;
    const bool method = shape == FnShape::Method;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(name);
    mod->setMethod(method);
    mod->setParamless(paramless);
    FunctionState fs;
    fs.mod = mod.get();
    fs.parent = method ? nullptr : fn_;  // methods capture nothing (Design note 9)
    fs.allowsReturn = shape != FnShape::Lambda;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    if (method) {
        const LocalInfo self{newSlot(), BindingKind::Param, false, false};
        fs.scopes.back()["this"] = self;
        if (tmpl_ && !tmpl_->selfName.empty()) fs.scopes.back()[tmpl_->selfName] = self;
    }
    for (std::size_t k = 0; k < params.size(); ++k) {
        const Param& p = params[k];
        const int slot = newSlot();
        const bool byName = k < kMaxByNameParams && ((byNameMask >> k) & 1u) != 0;
        if (p.name != "_")
            fs.scopes.back()[p.name] =
                LocalInfo{slot, byName ? BindingKind::ByNameParam : BindingKind::Param, false,
                          false};
    }
    const int arity = static_cast<int>(params.size()) + (method ? 1 : 0);
    mod->setArity(arity);
    mod->setVariadic(!params.empty() && params.back().repeated);
    analyseCaptures(params, body);
    compileExpr(body);
    emit(Op::RETURN, 0, pos, -1);
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    // In the enclosing function: push the captured slot values, then MAKE_FN.
    // A boxed slot holds its Cell, so PUSH_LOCAL shares the Cell itself.
    for (const auto& spec : mod->captureSpecs())
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(spec.parentSlot), pos, +1);
    const int nCaps = mod->captureCount();
    const std::size_t index = fn_->mod->addBlock(std::move(mod));
    emit(Op::MAKE_FN, index, pos, 1 - nCaps);
}

void Compiler::compileLazyThunk(const Node& rhs, SourcePos pos) {
    compileFunction("<lazy>", {}, rhs, FnShape::Lambda, pos);
    emit(Op::MAKE_LAZY, 0, pos, 0);
}

// ---------------------------------------------------------------------------
// Compilation units
// ---------------------------------------------------------------------------

CompiledUnit Compiler::compileUnit(const CompilationUnit& unit, UnitMode mode,
                                   int replResultIndex) {
    // A failed compilation must not leave half-declared names behind (a REPL
    // line with an error defines nothing).
    const GlobalTable snapshot = globals_;
    try {
        CompiledUnit out;
        out.module = std::make_unique<BytecodeModule>();
        out.module->setName("<top>");
        FunctionState top;
        top.mod = out.module.get();
        top.isTopLevel = true;
        top.scopes.emplace_back();
        fn_ = &top;
        boxed_.clear();
        globals_.beginUnit();
        // 1. Declare every top-level name; validate @main.
        const DefDef* main = nullptr;
        std::vector<const TemplateDef*> templates;
        std::unordered_map<const TemplateDef*, std::string> typeKeys;
        for (const auto& s : unit.stats) {
            if (s->kind == NodeKind::DefDef) {
                const auto& d = as<DefDef>(*s);
                const std::string& key = globals_.declare(d.name, kindOf(d));
                globals_.setByNameMasks(d.name, byNameMasksOfDef(d));
                if (d.isMain()) {
                    if (main) throw CompileError("only one @main method is allowed per file", d.pos);
                    main = &d;
                }
                if (mode == UnitMode::Repl) out.definitions.push_back({"def " + d.name, key});
            } else if (s->kind == NodeKind::ValDef) {
                const auto& v = as<ValDef>(*s);
                const std::string& key = globals_.declare(v.name, kindOf(v));
                if (mode == UnitMode::Repl && v.name.rfind('<', 0) != 0)
                    out.definitions.push_back(
                        {std::string(v.isLazy ? "lazy val " : v.isVar ? "var " : "val ") + v.name,
                         key});
            } else if (s->kind == NodeKind::TemplateDef) {
                const auto& t = as<TemplateDef>(*s);
                templates.push_back(&t);
                if (t.kind == TemplateKind::Object) {
                    const std::string termKey = globals_.declare(t.name, BindingKind::Object);
                    typeKeys[&t] = globals_.declareType(t.name + ".type");
                    if (mode == UnitMode::Repl && !t.synthetic)
                        out.definitions.push_back(
                            {std::string("// defined ") +
                                 (t.isCase ? "case object " : "object ") + t.name,
                             termKey});
                } else {
                    typeKeys[&t] = globals_.declareType(t.name);
                    if (mode == UnitMode::Repl)
                        out.definitions.push_back(
                            {std::string("// defined ") +
                                 (t.kind == TemplateKind::Trait ? "trait "
                                  : t.isCase                    ? "case class "
                                                                : "class ") +
                                 t.name,
                             typeKeys[&t]});
                }
            }
        }
        if (main) {
            const auto& lists = main->paramLists;
            if (main->curried)
                throw CompileError("@main methods take no parameters or a single repeated "
                                   "String parameter", main->pos);
            const bool noParams = lists.empty() || (lists.size() == 1 && lists[0].empty());
            const auto isString = [](const TypeTree* t) {
                return t && t->kind == TypeTree::Kind::Name &&
                       (t->name == "String" || t->name == "Predef.String" ||
                        t->name == "scala.Predef.String" || t->name == "java.lang.String");
            };
            const bool varargs = lists.size() == 1 && lists[0].size() == 1 &&
                                 lists[0][0].repeated && isString(lists[0][0].type.get());
            // Scala 3 parses typed @main parameters from the command line
            // (FromString); Phase 1 does not (D27).
            const bool typed = lists.size() == 1 && !lists[0].empty() &&
                               !(lists[0].size() == 1 && lists[0][0].repeated);
            if (typed)
                throw CompileError("typed @main parameters are not supported (D27); take "
                                   "`args: String*` and convert the strings", main->pos);
            if (!noParams && !varargs)
                throw CompileError("@main methods take no parameters or a single repeated "
                                   "String parameter", main->pos);
            out.mainName = main->name;
            out.mainKey = globalKey(main->name);
            out.mainTakesArgs = varargs;
        }
        // 1b. Describe the templates, parents first, and link the companions.
        const std::vector<const TemplateDef*> sorted = sortTemplates(templates);
        for (const TemplateDef* t : sorted) globals_.defineType(buildClassInfo(*t, typeKeys.at(t)));
        linkCompanions(sorted);
        // 2. Hoisted templates, object holders, top-level defs and lazy vals.
        for (const TemplateDef* t : sorted)
            compileTemplate(*t, *globals_.findTypeByKey(typeKeys.at(t)));
        for (const TemplateDef* t : sorted)
            if (t->kind == TemplateKind::Object)
                compileObjectHolder(*globals_.findTypeByKey(typeKeys.at(t)),
                                    globals_.binding(t->name)->key, t->pos);
        for (const auto& s : unit.stats) {
            if (s->kind == NodeKind::DefDef) {
                const auto& d = as<DefDef>(*s);
                compileFunction(d.name, paramsOf(d), bodyOf(d), FnShape::Def, d.pos,
                                /*paramless=*/false, /*allowByName=*/true);
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(globalKey(d.name)), d.pos, -1);
            } else if (s->kind == NodeKind::ValDef && as<ValDef>(*s).isLazy) {
                const auto& v = as<ValDef>(*s);
                compileLazyThunk(rhsOf(v), v.pos);
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(globalKey(v.name)), v.pos, -1);
            }
        }
        // 3. Boxing analysis for code that runs in the top-level frame (top-level
        //    names themselves are globals and never boxed).
        for (const auto& s : unit.stats) {
            if (s->kind == NodeKind::ValDef) {
                if (!as<ValDef>(*s).isLazy) analyseCaptures({}, rhsOf(as<ValDef>(*s)));
            } else if (s->kind != NodeKind::DefDef && s->kind != NodeKind::Import &&
                       s->kind != NodeKind::TemplateDef) {
                analyseCaptures({}, *s);
            }
        }
        // 4. Initialisers and statements in order.
        for (std::size_t k = 0; k < unit.stats.size(); ++k) {
            const Node& s = *unit.stats[k];
            const bool last = (k + 1 == unit.stats.size());
            if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import ||
                s.kind == NodeKind::TemplateDef)
                continue;
            if (s.kind == NodeKind::ValDef) {
                const auto& v = as<ValDef>(s);
                if (v.isLazy) continue;  // hoisted (step 2)
                compileExpr(rhsOf(v));
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(globalKey(v.name)), v.pos, -1);
                continue;
            }
            compileExpr(s);
            if (mode == UnitMode::Repl && last) {
                out.resultName = "res" + std::to_string(replResultIndex);
                out.resultKey = globals_.declare(out.resultName, BindingKind::Val);
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(out.resultKey), s.pos, -1);
            } else {
                emit(Op::POP, 0, s.pos, -1);
            }
        }
        emit(Op::PUSH_UNIT, 0, SourcePos{}, +1);
        emit(Op::RETURN, 0, SourcePos{}, -1);
        top.mod->setLocalCount(top.nextSlot);
        top.mod->setMaxStack(top.maxDepth);
        fn_ = nullptr;
        return out;
    } catch (...) {
        fn_ = nullptr;
        tmpl_ = nullptr;
        globals_ = snapshot;
        throw;
    }
}

} // namespace protoScala
