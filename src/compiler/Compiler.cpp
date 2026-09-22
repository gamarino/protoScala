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

// ---------------------------------------------------------------------------
// CaptureAnalysis: decides which declarations of one function body live in a
// Cell. Scopes map names to their declaration and the function depth (0 =
// the analysed function, +1 per nested lambda / local def / lazy thunk)
// where they are bound. A reference from a deeper function boxes:
//   - a var (every closure must see the same variable),
//   - a local def (hoisted, so it may be captured before it is stored),
//   - a val / lazy val when some local def lies between the declaration and
//     the reference (the def is hoisted above the val's initialiser).
// Parameters (decl == nullptr) are never boxed: they are immutable and bound
// before any hoisted MAKE_FN runs.
//
// The pass also rejects forward references that would read a slot before its
// initialiser ran: a reference from a block statement (directly, or from a
// lambda / lazy thunk created by it) to a val or var of the same block
// declared at or after that statement. References through a local def are
// legal (the def is hoisted and the declaration is boxed). A lazy val
// referenced that way from a lambda or thunk is boxed instead; a direct
// forward reference to a lazy val is rejected as not supported yet.
// ---------------------------------------------------------------------------
class CaptureAnalysis {
public:
    explicit CaptureAnalysis(std::unordered_set<const Node*>& boxed) : boxed_(boxed) {}

    void function(const std::vector<Param>& params, const Node& body, int depth, bool isDef) {
        if (static_cast<int>(isDefLevel_.size()) <= depth) isDefLevel_.resize(depth + 1);
        isDefLevel_[depth] = isDef;
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
    std::vector<Scope> scopes_;
    std::vector<bool> isDefLevel_;

    const Decl* lookup(const std::string& name, const Scope** where) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto hit = it->names.find(name);
            if (hit != it->names.end()) { *where = &*it; return &hit->second; }
        }
        return nullptr;
    }

    bool defBetween(int declDepth, int refDepth) const {
        for (int d = declDepth + 1; d <= refDepth; ++d)
            if (isDefLevel_[d]) return true;
        return false;
    }

    void reference(const std::string& name, int depth, SourcePos pos) {
        const Scope* scope = nullptr;
        const Decl* d = lookup(name, &scope);
        if (!d || !d->decl) return;
        const bool throughDef = defBetween(d->depth, depth);
        const bool forward = scope->block && d->index >= scope->current &&
                             d->kind != DeclKind::Def && !throughDef;
        if (forward) {
            if (d->kind == DeclKind::LazyVal) {
                if (d->depth == depth)
                    throw CompileError("forward reference to lazy value " + name +
                                       " is not supported yet", pos);
                boxed_.insert(d->decl);  // the closure must see the later MAKE_LAZY
                return;
            }
            const Node& at = *scope->block->stats[static_cast<std::size_t>(scope->current)];
            const std::string over =
                at.kind == NodeKind::ValDef ? as<ValDef>(at).name : name;
            throw CompileError("forward reference to value " + name +
                               " extends over definition of value " + over, pos);
        }
        if (d->depth >= depth) return;
        const bool box = d->kind == DeclKind::Var || d->kind == DeclKind::Def ||
                         ((d->kind == DeclKind::Val || d->kind == DeclKind::LazyVal) && throughDef);
        if (box) boxed_.insert(d->decl);
    }

    void walk(const Node* n, int depth) {
        if (n) walk(*n, depth);
    }

    void walk(const Node& n, int depth) {
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
                for (const auto& arg : a.args) walk(arg.get(), depth);
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
                if (l.body) function(l.params, *l.body, depth + 1, /*isDef=*/false);
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
        }
    }

    // The initialiser of a val (a lazy one runs in a thunk) or a def body.
    void definitionBody(const Node& n, int depth) {
        if (n.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(n);
            if (!v.rhs) return;
            if (v.isLazy) function({}, *v.rhs, depth + 1, /*isDef=*/false);
            else walk(*v.rhs, depth);
        } else {
            const auto& d = as<DefDef>(n);
            if (d.body) function(paramsOf(d), *d.body, depth + 1, /*isDef=*/true);
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

Compiler::LocalInfo Compiler::declareLocal(const std::string& name, BindingKind kind, bool boxed) {
    LocalInfo info{newSlot(), kind, boxed, false};
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
    LocalInfo mine{f->nextSlot++, outer.kind, outer.boxed, /*captured=*/true};
    f->mod->addCapture(outer.slot, mine.slot);
    f->scopes.front()[name] = mine;
    return mine;
}

Compiler::Resolution Compiler::resolve(const std::string& name, SourcePos pos) {
    bool found = false;
    LocalInfo info = captureInto(fn_, name, pos, &found);
    if (found) return Resolution{RefKind::Local, info, info.kind};
    if (auto g = globals_.find(name)) return Resolution{RefKind::Global, {}, *g};
    throw CompileError("Not found: " + name, pos);
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
    CaptureAnalysis(boxed_).function(params, body, 0, /*isDef=*/false);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

void Compiler::compileExpr(const Node& n) {
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
        case NodeKind::Tuple: throw CompileError("tuples are not implemented yet", n.pos);
        case NodeKind::Splice:
            throw CompileError("a splice must be the last argument of a function call", n.pos);
        case NodeKind::NamedArg:
            throw CompileError("named arguments are not implemented yet", n.pos);
        case NodeKind::Ident: compileIdent(as<Ident>(n)); return;
        case NodeKind::Select: compileSelect(as<Select>(n)); return;
        case NodeKind::Apply: compileApply(as<Apply>(n)); return;
        case NodeKind::TypeApply: compileExpr(*as<TypeApply>(n).fn); return;  // erased
        case NodeKind::Assign: compileAssign(as<Assign>(n)); return;
        case NodeKind::If: compileIf(as<If>(n)); return;
        case NodeKind::While: compileWhile(as<While>(n)); return;
        case NodeKind::Return: compileReturn(as<Return>(n)); return;
        case NodeKind::Block: compileBlock(as<Block>(n)); return;
        case NodeKind::Lambda: {
            const auto& l = as<Lambda>(n);
            if (!l.body) throw CompileError("a function literal needs a body", n.pos);
            compileFunction("<lambda>", l.params, *l.body, /*isDef=*/false, n.pos);
            return;
        }
        case NodeKind::ValDef:
        case NodeKind::DefDef:
        case NodeKind::Import:
            throw CompileError("definition used as an expression", n.pos);
        case NodeKind::Infix:
        case NodeKind::Prefix:
        case NodeKind::Parens:
        case NodeKind::Typed:
            throw std::logic_error("compiler: node kind not desugared");
    }
}

void Compiler::compileIdent(const Ident& id) {
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Local) loadLocal(r.local, id.pos);
    else emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(id.name), id.pos, +1);
    if (r.kind == BindingKind::ParamlessDef) emit(Op::CALL, 0, id.pos, 0);
    else if (r.kind == BindingKind::LazyVal) emit(Op::FORCE, 0, id.pos, 0);
}

void Compiler::compileApply(const Apply& a) {
    for (const auto& arg : a.args) {
        if (arg->kind == NodeKind::NamedArg)
            throw CompileError("named arguments are not implemented yet", arg->pos);
    }
    // Type arguments are erased: recv.m[T](args) is a send like recv.m(args).
    const Node* fn = a.fn.get();
    while (fn->kind == NodeKind::TypeApply) fn = as<TypeApply>(*fn).fn.get();
    if (fn->kind == NodeKind::Select) {
        const auto& sel = as<Select>(*fn);
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
        for (const auto& arg : a.args) {
            if (arg->kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg->pos);
            compileExpr(*arg);
        }
        const auto n = static_cast<std::uint32_t>(a.args.size());
        emit(Op::SEND, fn_->mod->addSendSite(sel.name, n), a.pos, -static_cast<int>(n));
        return;
    }
    compileExpr(*fn);
    compileArgsAndCall(a.args, a.pos);
}

void Compiler::compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos) {
    const bool spread = !args.empty() && args.back()->kind == NodeKind::Splice;
    for (std::size_t k = 0; k < args.size(); ++k) {
        const Node& arg = *args[k];
        if (arg.kind == NodeKind::Splice) {
            if (k + 1 != args.size())
                throw CompileError("a splice must be the last argument", arg.pos);
            compileExpr(*as<Splice>(arg).expr);
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
    compileExpr(*s.qualifier);
    if (s.name == "unary_-") { emit(Op::NEG, 0, s.pos, 0); return; }
    if (s.name == "unary_!") { emit(Op::NOT, 0, s.pos, 0); return; }
    emit(Op::SEND, fn_->mod->addSendSite(s.name, 0), s.pos, 0);
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
    if (a.target->kind != NodeKind::Ident)
        throw CompileError("assignment to fields and indexed elements is not implemented yet",
                           a.pos);
    const auto& id = as<Ident>(*a.target);
    const Resolution r = resolve(id.name, id.pos);
    if (r.kind != BindingKind::Var) throw CompileError("Reassignment to val " + id.name, a.pos);
    compileExpr(*a.value);
    if (r.ref == RefKind::Local) {
        if (r.local.captured && !r.local.boxed)
            throw std::logic_error("compiler: captured var is not boxed");
        storeLocal(r.local, a.pos);
    } else {
        emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(id.name), a.pos, -1);
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
    fn_->scopes.emplace_back();
    // 1. Declare every definition of the block; boxed ones get a Cell now.
    for (const auto& s : b.stats) {
        if (s->kind == NodeKind::DefDef) {
            const auto& d = as<DefDef>(*s);
            const LocalInfo info = declareLocal(d.name, kindOf(d), boxed_.count(s.get()) > 0);
            if (info.boxed) emit(Op::MAKE_CELL, static_cast<std::uint64_t>(info.slot), d.pos, 0);
        } else if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            const LocalInfo info = declareLocal(v.name, kindOf(v), boxed_.count(s.get()) > 0);
            if (info.boxed) emit(Op::MAKE_CELL, static_cast<std::uint64_t>(info.slot), v.pos, 0);
        }
    }
    // 2. Hoisted local defs.
    for (const auto& s : b.stats) {
        if (s->kind != NodeKind::DefDef) continue;
        const auto& d = as<DefDef>(*s);
        compileFunction(d.name, paramsOf(d), bodyOf(d), /*isDef=*/true, d.pos);
        storeLocal(*findInFunction(fn_, d.name), d.pos);
    }
    // 3. Statements in order; the last expression is the block's value.
    bool valueOnStack = false;
    for (std::size_t k = 0; k < b.stats.size(); ++k) {
        const Node& s = *b.stats[k];
        const bool last = (k + 1 == b.stats.size());
        if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import) continue;
        if (s.kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(s);
            if (v.isLazy) compileLazyThunk(rhsOf(v), v.pos);
            else compileExpr(rhsOf(v));
            storeLocal(*findInFunction(fn_, v.name), v.pos);
            continue;
        }
        compileExpr(s);
        if (last) valueOnStack = true;
        else emit(Op::POP, 0, s.pos, -1);
    }
    if (!valueOnStack) emit(Op::PUSH_UNIT, 0, b.pos, +1);
    fn_->scopes.pop_back();
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

void Compiler::compileFunction(const std::string& name, const std::vector<Param>& params,
                               const Node& body, bool isDef, SourcePos pos) {
    for (const Param& p : params) {
        if (p.defaultValue)
            throw CompileError("default parameter values are not implemented yet", p.pos);
        if (p.byName) throw CompileError("by-name parameters are not supported yet", p.pos);
    }
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(name);
    FunctionState fs;
    fs.mod = mod.get();
    fs.parent = fn_;
    fs.allowsReturn = isDef;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    for (const Param& p : params) {
        const int slot = newSlot();
        if (p.name != "_") fs.scopes.back()[p.name] = LocalInfo{slot, BindingKind::Param, false, false};
    }
    mod->setArity(static_cast<int>(params.size()));
    mod->setVariadic(!params.empty() && params.back().repeated);
    analyseCaptures(params, body);
    compileExpr(body);
    emit(Op::RETURN, 0, pos, -1);
    mod->setLocalCount(fs.nextSlot - static_cast<int>(params.size()));
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
    compileFunction("<lazy>", {}, rhs, /*isDef=*/false, pos);
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
        // 1. Declare every top-level name; validate @main.
        const DefDef* main = nullptr;
        for (const auto& s : unit.stats) {
            if (s->kind == NodeKind::DefDef) {
                const auto& d = as<DefDef>(*s);
                globals_.declare(d.name, kindOf(d));
                if (d.isMain()) {
                    if (main) throw CompileError("only one @main method is allowed per file", d.pos);
                    main = &d;
                }
                if (mode == UnitMode::Repl) out.definitions.push_back("def " + d.name);
            } else if (s->kind == NodeKind::ValDef) {
                const auto& v = as<ValDef>(*s);
                globals_.declare(v.name, kindOf(v));
                if (mode == UnitMode::Repl)
                    out.definitions.push_back(
                        std::string(v.isLazy ? "lazy val " : v.isVar ? "var " : "val ") + v.name);
            }
        }
        if (main) {
            const auto& lists = main->paramLists;
            const bool noParams = lists.empty() || (lists.size() == 1 && lists[0].empty());
            const bool varargs = lists.size() == 1 && lists[0].size() == 1 && lists[0][0].repeated;
            if (!noParams && !varargs)
                throw CompileError("@main methods take no parameters or a single repeated "
                                   "String parameter", main->pos);
            out.mainName = main->name;
            out.mainTakesArgs = varargs;
        }
        // 2. Hoisted top-level defs.
        for (const auto& s : unit.stats) {
            if (s->kind != NodeKind::DefDef) continue;
            const auto& d = as<DefDef>(*s);
            compileFunction(d.name, paramsOf(d), bodyOf(d), /*isDef=*/true, d.pos);
            emit(Op::STORE_GLOBAL, top.mod->addSymbol(d.name), d.pos, -1);
        }
        // 3. Boxing analysis for code that runs in the top-level frame (top-level
        //    names themselves are globals and never boxed).
        for (const auto& s : unit.stats) {
            if (s->kind == NodeKind::ValDef) analyseCaptures({}, rhsOf(as<ValDef>(*s)));
            else if (s->kind != NodeKind::DefDef && s->kind != NodeKind::Import) analyseCaptures({}, *s);
        }
        // 4. Initialisers and statements in order.
        for (std::size_t k = 0; k < unit.stats.size(); ++k) {
            const Node& s = *unit.stats[k];
            const bool last = (k + 1 == unit.stats.size());
            if (s.kind == NodeKind::DefDef || s.kind == NodeKind::Import) continue;
            if (s.kind == NodeKind::ValDef) {
                const auto& v = as<ValDef>(s);
                if (v.isLazy) compileLazyThunk(rhsOf(v), v.pos);
                else compileExpr(rhsOf(v));
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(v.name), v.pos, -1);
                continue;
            }
            compileExpr(s);
            if (mode == UnitMode::Repl && last) {
                out.resultName = "res" + std::to_string(replResultIndex);
                globals_.declare(out.resultName, BindingKind::Val);
                emit(Op::STORE_GLOBAL, top.mod->addSymbol(out.resultName), s.pos, -1);
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
        globals_ = snapshot;
        throw;
    }
}

} // namespace protoScala
