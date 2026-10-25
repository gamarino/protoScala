/*
 * CompileTailRec — `@tailrec` methods (SLS 3 `scala.annotation.tailrec`).
 *
 * Scala guarantees that a method annotated `@tailrec` runs its self-calls in
 * tail position as a loop, and rejects at compile time a method it cannot
 * rewrite. protoScala does both with existing opcodes and no new run-time
 * concept: a tail self-call evaluates its arguments onto the operand stack,
 * stores them into the parameter slots in reverse, drops whatever else the
 * stack holds and jumps back to word 0 with JUMP_BACK, which polls the GC
 * safepoint exactly as a `while` loop does. The frame and its ProtoContext are
 * reused, as a loop's are.
 *
 * Tail positions are computed from the desugared tree before the body is
 * compiled (collectTail / collectReturns): the branches of an `if`, the last
 * expression of a block, the bodies of `match` cases, the right operand of
 * `&&` and `||`, a `return`'s value, and the `catch` cases of a `try` without
 * a `finally` -- never a `try` body (scalac 3.9 rejects a self-call there).
 * Desugar's own value-discarding block and Double widening are transparent,
 * because both are idempotent: the loop's eventual result passes through them.
 *
 * Captures need nothing special. A parameter is never boxed (CaptureAnalysis):
 * a closure copies its value when it is created, so a closure built in an
 * earlier iteration keeps the value it saw. A captured local `var` of the body
 * gets its Cell from the MAKE_CELL at the start of its block, which every
 * iteration runs again.
 */
#include "compiler/Compiler.h"
#include "runtime/StackGuard.h"

#include <algorithm>

namespace protoScala {

namespace {

constexpr const char* kNotInTailPosition =
    "Cannot rewrite recursive call: it is not in tail position";

bool isStatementOnly(const Node& n) {
    return n.kind == NodeKind::ValDef || n.kind == NodeKind::DefDef ||
           n.kind == NodeKind::Import || n.kind == NodeKind::TypeDef ||
           n.kind == NodeKind::TemplateDef || n.kind == NodeKind::ExtensionDef;
}

// The applications whose value is the value of `n`, when `n` is itself in tail
// position.
void collectTail(const Node* n, std::unordered_set<const Node*>& out) {
    if (!n) return;
    checkNativeStack(StackUse::Source);
    switch (n->kind) {
        case NodeKind::If:
            collectTail(as<If>(*n).thenp.get(), out);
            collectTail(as<If>(*n).elsep.get(), out);
            return;
        case NodeKind::Block: {
            const auto& b = as<Block>(*n);
            if (b.stats.empty()) return;
            if (b.valueDiscard) { collectTail(b.stats.front().get(), out); return; }
            if (!isStatementOnly(*b.stats.back())) collectTail(b.stats.back().get(), out);
            return;
        }
        case NodeKind::Match:
            for (const CaseDef& c : as<Match>(*n).cases) collectTail(c.body.get(), out);
            return;
        case NodeKind::Try: {
            const auto& t = as<Try>(*n);
            if (t.finallyBody) return;
            for (const CaseDef& c : t.cases) collectTail(c.body.get(), out);
            return;
        }
        case NodeKind::Apply: {
            const auto& a = as<Apply>(*n);
            if (a.doubleWidening && a.fn->kind == NodeKind::Select) {
                collectTail(as<Select>(*a.fn).qualifier.get(), out);
                return;
            }
            if (a.args.size() == 1 && a.fn->kind == NodeKind::Select) {
                const std::string& op = as<Select>(*a.fn).name;
                if (op == "&&" || op == "||") {
                    collectTail(a.args[0].get(), out);
                    return;
                }
            }
            out.insert(n);
            return;
        }
        default:
            return;
    }
}

// The values of the `return`s of a body are in tail position, except inside a
// `try` body and inside the handlers of a `try` with a `finally`, which must
// still run. Nested functions own their returns (a lambda cannot `return` at
// all here) and are not entered.
void collectReturns(const Node* n, bool guarded, std::unordered_set<const Node*>& out) {
    if (!n) return;
    checkNativeStack(StackUse::Source);
    auto walk = [&](const Node* c) { collectReturns(c, guarded, out); };
    switch (n->kind) {
        case NodeKind::Return: {
            const auto& r = as<Return>(*n);
            if (!guarded) collectTail(r.value.get(), out);
            walk(r.value.get());
            return;
        }
        case NodeKind::Try: {
            const auto& t = as<Try>(*n);
            collectReturns(t.body.get(), true, out);
            for (const CaseDef& c : t.cases) {
                collectReturns(c.guard.get(), true, out);
                collectReturns(c.body.get(), guarded || t.finallyBody != nullptr, out);
            }
            collectReturns(t.finallyBody.get(), true, out);
            return;
        }
        case NodeKind::InterpString:
            for (const NodePtr& a : as<InterpString>(*n).args) walk(a.get());
            return;
        case NodeKind::Select: walk(as<Select>(*n).qualifier.get()); return;
        case NodeKind::Apply:
            walk(as<Apply>(*n).fn.get());
            for (const NodePtr& a : as<Apply>(*n).args) walk(a.get());
            return;
        case NodeKind::TypeApply: walk(as<TypeApply>(*n).fn.get()); return;
        case NodeKind::Infix: walk(as<Infix>(*n).lhs.get()); walk(as<Infix>(*n).rhs.get()); return;
        case NodeKind::Prefix: walk(as<Prefix>(*n).operand.get()); return;
        case NodeKind::Assign: walk(as<Assign>(*n).target.get()); walk(as<Assign>(*n).value.get()); return;
        case NodeKind::If:
            walk(as<If>(*n).cond.get());
            walk(as<If>(*n).thenp.get());
            walk(as<If>(*n).elsep.get());
            return;
        case NodeKind::While: walk(as<While>(*n).cond.get()); walk(as<While>(*n).body.get()); return;
        case NodeKind::Block:
            for (const NodePtr& s : as<Block>(*n).stats) walk(s.get());
            return;
        case NodeKind::Typed: walk(as<Typed>(*n).expr.get()); return;
        case NodeKind::Parens: walk(as<Parens>(*n).expr.get()); return;
        case NodeKind::Tuple:
            for (const NodePtr& e : as<Tuple>(*n).elems) walk(e.get());
            return;
        case NodeKind::Splice: walk(as<Splice>(*n).expr.get()); return;
        case NodeKind::NamedArg: walk(as<NamedArg>(*n).value.get()); return;
        case NodeKind::ValDef: if (!as<ValDef>(*n).isLazy) walk(as<ValDef>(*n).rhs.get()); return;
        case NodeKind::New:
            for (const NodePtr& a : as<New>(*n).args) walk(a.get());
            return;
        case NodeKind::Match:
            walk(as<Match>(*n).scrutinee.get());
            for (const CaseDef& c : as<Match>(*n).cases) {
                walk(c.guard.get());
                walk(c.body.get());
            }
            return;
        case NodeKind::Throw: walk(as<Throw>(*n).value.get()); return;
        default:
            return;  // literals, names, nested functions and definitions
    }
}

} // namespace

void Compiler::checkTailRecOverridable(const DefDef& d, bool finalOwner) {
    if (d.mods.isPrivate || d.mods.isFinal || finalOwner) return;
    throw CompileError("TailRec optimisation not applicable, method " + d.name +
                           " is neither private nor final so can be overridden",
                       d.pos);
}

void Compiler::beginTailRec(TailRecState& state, const TailRecTarget& target,
                            FunctionState& owner, const Node& body) {
    state.target = &target;
    state.owner = &owner;
    // Desugar folded a curried method's later parameter lists into lambdas
    // (`def f(a)(b) = e` is `def f(a) = (b) => e`); they are read back here.
    state.lists.push_back(&target.def->paramLists.front());
    const Node* b = target.def->body.get();
    while (b && b->kind == NodeKind::Lambda && as<Lambda>(*b).fromCurriedDef) {
        state.lists.push_back(&as<Lambda>(*b).params);
        b = as<Lambda>(*b).body.get();
    }
    collectTail(&body, state.sites);
    collectReturns(&body, false, state.sites);
}

bool Compiler::matchSelfCall(const Apply& a, std::vector<const std::vector<NodePtr>*>& argLists) {
    TailRecState& st = *fn_->tailrec;
    const TailRecTarget& t = *st.target;
    const std::string& name = t.def->name;
    argLists.clear();
    const Node* head = &a;
    while (head->kind == NodeKind::Apply || head->kind == NodeKind::TypeApply) {
        if (head->kind == NodeKind::Apply) {
            argLists.push_back(&as<Apply>(*head).args);
            head = as<Apply>(*head).fn.get();
        } else {
            head = as<TypeApply>(*head).fn.get();
        }
    }
    // A partial application (`f(a)` of a curried `f`) is not a call of `f`.
    if (argLists.size() != st.lists.size()) return false;
    std::reverse(argLists.begin(), argLists.end());

    if (head->kind == NodeKind::Ident) {
        if (as<Ident>(*head).name != name || t.kind == TailRecTarget::Kind::Extension) return false;
        if (importedTerm(name) && !localOrMemberShadows(name)) return false;
        const Resolution r = resolve(name, head->pos);
        switch (t.kind) {
            case TailRecTarget::Kind::Global: {
                if (r.ref != RefKind::Global) return false;
                if (globals_.overloadArities(name).empty()) return r.key == t.key;
                const GlobalBinding::Overload* alt =
                    globals_.overloadFor(name, argLists.front()->size());
                return alt && alt->key == t.key;
            }
            case TailRecTarget::Kind::Member:
                return r.ref == RefKind::Member && r.key == t.key;
            case TailRecTarget::Kind::Local:
                // The local def reaches its own body as a capture; a local of the
                // body, or a parameter, of the same name shadows it.
                if (r.ref != RefKind::Local) return false;
                for (FunctionState* f = fn_; f; f = f->parent) {
                    const LocalInfo* li = findInFunction(f, name);
                    if (!li || !li->captured) return false;
                    if (f == st.owner) return true;
                }
                return false;
            case TailRecTarget::Kind::Extension:
                return false;
        }
        return false;
    }

    if (head->kind != NodeKind::Select) return false;
    const auto& sel = as<Select>(*head);
    if (sel.name != name) return false;
    if (t.kind != TailRecTarget::Kind::Member && t.kind != TailRecTarget::Kind::Extension)
        return false;
    // `this.f(...)` (or the self alias, or an extension's receiver) keeps the
    // receiver in slot 0. Any other receiver is a value of a type the compiler
    // does not know (D4), so the call stays an ordinary send.
    bool self = false;
    if (sel.qualifier->kind == NodeKind::Ident) {
        const std::string& q = as<Ident>(*sel.qualifier).name;
        const bool alias = t.kind == TailRecTarget::Kind::Member
                               ? q == "this" || (tmpl_ && !tmpl_->selfName.empty() &&
                                                 q == tmpl_->selfName)
                               : q == t.receiverAlias;
        bool found = false;
        if (alias) captureInto(fn_, q, sel.qualifier->pos, &found);
        if (found) {
            self = true;
            for (FunctionState* f = fn_; f; f = f->parent) {
                const LocalInfo* li = findInFunction(f, q);
                const bool receiverSlot = f->mod->isMethod() && li && li->slot == 0 && !li->captured;
                if (!li || (!li->captured && !receiverSlot)) { self = false; break; }
                if (f == st.owner || receiverSlot) break;
            }
        }
    }
    if (!self) {
        st.otherReceiver = true;
        return false;
    }
    if (t.kind == TailRecTarget::Kind::Member) {
        const MemberInfo* m = memberOf(name);
        return m && m->key == t.key;
    }
    return true;
}

Compiler::LocalInfo Compiler::outerParamSlot(FunctionState& owner, const std::string& name,
                                             SourcePos pos) {
    auto it = owner.scopes.front().find(name);
    if (it != owner.scopes.front().end() && it->second.captured) return it->second;
    bool found = false;
    const LocalInfo outer = captureInto(owner.parent, name, pos, &found);
    if (!found) throw std::logic_error("compiler: parameter " + name + " of a curried method not found");
    LocalInfo mine{owner.nextSlot++, outer.kind, outer.boxed, /*captured=*/true, outer.byNameMasks};
    owner.mod->addCapture(outer.slot, mine.slot);
    owner.scopes.front()[name] = mine;
    return mine;
}

void Compiler::compileTailCall(const Apply& a,
                               const std::vector<const std::vector<NodePtr>*>& argLists) {
    TailRecState& st = *fn_->tailrec;
    if (fn_ != st.owner || !st.sites.count(&a)) throw CompileError(kNotInTailPosition, a.pos);
    if (!fn_->finallys.empty())
        throw std::logic_error("compiler: a tail call inside a try with a finally");
    ++st.calls;
    FunctionState& owner = *fn_;
    const std::string& name = st.target->def->name;
    const std::size_t innermost = st.lists.size() - 1;
    const int base = owner.mod->isMethod() ? 1 : 0;

    // Where each pushed value goes, in push order. The innermost list is the
    // owner's own parameters; an outer list of a curried method lives in the
    // owner's captures. An outer parameter that a later list rebinds, or `_`,
    // cannot be read by the body, so its new value is evaluated and dropped.
    struct Dest {
        int slot;     // -1: discard
        bool widen;   // an outer Double parameter (the prologue widens the innermost list)
    };
    auto destFor = [&](std::size_t list, std::size_t k) -> Dest {
        const Param& p = (*st.lists[list])[k];
        if (list == innermost) return {base + static_cast<int>(k), false};
        if (p.name == "_") return {-1, false};
        for (std::size_t l = list + 1; l < st.lists.size(); ++l)
            for (const Param& q : *st.lists[l])
                if (q.name == p.name) return {-1, false};
        const bool widen = !p.byName && !p.repeated && isDoubleTypeName(p.type.get());
        return {outerParamSlot(owner, p.name, a.pos).slot, widen};
    };
    auto store = [&](const Dest& d, SourcePos pos) {
        if (d.slot < 0) {
            emit(Op::POP, 0, pos, -1);
            return;
        }
        if (d.widen) emit(Op::SEND, sendSite("toDouble", 0), pos, 0);
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(d.slot), pos, -1);
    };
    auto push = [&](const Node& arg, std::uint32_t byName, std::size_t k) {
        if (k < kMaxByNameParams && (byName >> k) & 1u) compileByNameArgument(arg);
        else compileExpr(arg);
    };

    // 1. Every argument, in source order, as a call evaluates them.
    std::vector<Dest> dests;
    std::vector<std::vector<bool>> given(st.lists.size());
    for (std::size_t list = 0; list < st.lists.size(); ++list) {
        const std::vector<Param>& params = *st.lists[list];
        const std::vector<NodePtr>& args = *argLists[list];
        given[list].assign(params.size(), false);
        const std::uint32_t byName = byNameMaskOfParams(params);
        const bool variadic = !params.empty() && params.back().repeated;
        const std::size_t fixed = variadic ? params.size() - 1 : params.size();
        const bool named = std::any_of(args.begin(), args.end(), [](const NodePtr& x) {
            return x->kind == NodeKind::NamedArg;
        });
        std::size_t positional = args.size();
        const std::vector<std::string> keywords =
            named ? splitNamedArgs(args, &positional) : std::vector<std::string>{};
        if (variadic && !keywords.empty())
            throw CompileError(name + " does not take named arguments: it has a repeated parameter",
                               args[positional]->pos);
        for (std::size_t k = 0; k < positional && k < fixed; ++k) {
            const Node& arg = *args[k];
            if (arg.kind == NodeKind::Splice)
                throw CompileError("a splice can only supply a repeated parameter", arg.pos);
            push(arg, byName, k);
            dests.push_back(destFor(list, k));
            given[list][k] = true;
        }
        if (!variadic && positional > fixed)
            throw CompileError("too many arguments for method " + name, args[fixed]->pos);
        if (variadic && positional >= fixed) {
            // The repeated parameter: the List the callee's prologue would have
            // built from the remaining arguments, a splice's elements included.
            emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(globalKey("__varargs")), a.pos, +1);
            bool spread = false;
            for (std::size_t k = fixed; k < positional; ++k) {
                const Node& arg = *args[k];
                if (arg.kind == NodeKind::Splice) {
                    if (k + 1 != positional)
                        throw CompileError("a splice must be the last argument", arg.pos);
                    spread = true;
                    compileExpr(*as<Splice>(arg).expr);
                } else {
                    compileExpr(arg);
                }
            }
            const int n = static_cast<int>(positional - fixed);
            if (spread) emit(Op::CALL_SPREAD, static_cast<std::uint64_t>(n - 1), a.pos, -n);
            else emit(Op::CALL, static_cast<std::uint64_t>(n), a.pos, -n);
            dests.push_back(destFor(list, fixed));
            given[list][fixed] = true;
        }
        for (std::size_t i = 0; i < keywords.size(); ++i) {
            const Node& arg = *args[positional + i];
            std::size_t k = 0;
            while (k < params.size() && params[k].name != keywords[i]) ++k;
            if (k == params.size())
                throw CompileError(name + " has no parameter named '" + keywords[i] + "'", arg.pos);
            if (given[list][k])
                throw CompileError(name + " received parameter '" + keywords[i] + "' twice",
                                   arg.pos);
            push(*as<NamedArg>(arg).value, byName, k);
            dests.push_back(destFor(list, k));
            given[list][k] = true;
        }
    }
    // 2. Rebind the parameters; the last value pushed is on top.
    for (auto it = dests.rbegin(); it != dests.rend(); ++it) store(*it, a.pos);
    // 3. Defaults, in parameter order, after the explicit arguments: a default
    //    reads the NEW values of the parameters before it, as the default
    //    getter of a call would. It sees the method's parameters and captures
    //    only, never a local of the body that happens to be in scope here.
    for (std::size_t list = 0; list < st.lists.size(); ++list) {
        const std::vector<Param>& params = *st.lists[list];
        for (std::size_t k = 0; k < params.size(); ++k) {
            if (given[list][k]) continue;
            const Param& p = params[k];
            if (!p.defaultValue)
                throw CompileError(name + " is missing argument '" + p.name + "'", a.pos);
            const Dest d = destFor(list, k);
            std::vector<std::unordered_map<std::string, LocalInfo>> hidden;
            while (owner.scopes.size() > 1) {
                hidden.push_back(std::move(owner.scopes.back()));
                owner.scopes.pop_back();
            }
            owner.tailrec = nullptr;  // a call in a default is not part of the loop
            auto restore = [&] {
                owner.tailrec = &st;
                while (!hidden.empty()) {
                    owner.scopes.push_back(std::move(hidden.back()));
                    hidden.pop_back();
                }
            };
            try {
                compileExpr(*p.defaultValue);
            } catch (...) {
                restore();
                throw;
            }
            restore();
            store(d, p.pos);
        }
    }
    // 4. Whatever the enclosing expression had pushed is dead: drop it, then loop.
    while (fn_->depth > 0) emit(Op::POP, 0, a.pos, -1);
    fn_->mod->emitJumpBack(0, a.pos.line);
    adjust(+1);  // the call's value, for the stack shape of the code that follows
}

} // namespace protoScala
