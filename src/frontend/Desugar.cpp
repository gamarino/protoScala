#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/StackGuard.h"

namespace protoScala {

namespace {

class Desugarer {
public:
    NodePtr expr(NodePtr n) {
        if (!n) return n;
        try {
            checkNativeStack(StackUse::Source);
        } catch (...) {
            destroyTree(std::move(n));  // n may be deep: free it without recursion
            throw;
        }
        switch (n->kind) {
            case NodeKind::Infix:  return infix(std::move(n));
            case NodeKind::Prefix: {
                auto& p = as<Prefix>(*n);
                return std::make_unique<Select>(p.pos, expr(std::move(p.operand)),
                                                "unary_" + p.op);
            }
            case NodeKind::Parens: return expr(std::move(as<Parens>(*n).expr));
            case NodeKind::Typed: {
                // `(e: Unit)`: the expected type Unit discards e's value.
                auto& t = as<Typed>(*n);
                const bool unit = isUnitType(t.type.get());
                NodePtr e = expr(std::move(t.expr));
                return unit ? discardValue(std::move(e)) : std::move(e);
            }
            case NodeKind::If: {
                // `if c then t` has type Unit: t runs for its effect and the
                // expression yields () on both paths.
                auto& i = as<If>(*n);
                i.cond = expr(std::move(i.cond));
                i.thenp = expr(std::move(i.thenp));
                if (i.elsep) {
                    i.elsep = expr(std::move(i.elsep));
                } else {
                    i.thenp = discardValue(std::move(i.thenp));
                    i.elsep = std::make_unique<UnitLit>(i.pos);
                }
                return n;
            }
            case NodeKind::While: {
                auto& w = as<While>(*n);
                w.cond = expr(std::move(w.cond));
                w.body = expr(std::move(w.body));
                return n;
            }
            case NodeKind::Select: {
                auto& s = as<Select>(*n);
                s.qualifier = expr(std::move(s.qualifier));
                return n;
            }
            case NodeKind::Apply: {
                auto& a = as<Apply>(*n);
                a.fn = expr(std::move(a.fn));
                for (auto& arg : a.args) arg = expr(std::move(arg));
                return n;
            }
            case NodeKind::TypeApply: {
                auto& t = as<TypeApply>(*n);
                t.fn = expr(std::move(t.fn));
                return n;
            }
            case NodeKind::Assign: {
                auto& a = as<Assign>(*n);
                if (a.target->kind == NodeKind::Select) {  // obj.x = v  →  obj.x_=(v)
                    auto& s = as<Select>(*a.target);
                    return expr(call(a.pos, std::move(s.qualifier), s.name + "_=",
                                     std::move(a.value)));
                }
                if (a.target->kind == NodeKind::Apply) {   // f(args) = v  →  f.update(args, v)
                    auto& target = as<Apply>(*a.target);
                    auto up = std::make_unique<Apply>(
                        a.pos, std::make_unique<Select>(a.pos, std::move(target.fn), "update"));
                    up->args = std::move(target.args);
                    up->args.push_back(std::move(a.value));
                    return expr(std::move(up));
                }
                a.target = expr(std::move(a.target));
                a.value = expr(std::move(a.value));
                return n;
            }
            case NodeKind::Return: {
                auto& r = as<Return>(*n);
                r.value = expr(std::move(r.value));
                return n;
            }
            case NodeKind::Block: {
                stats(as<Block>(*n).stats);
                return n;
            }
            case NodeKind::Lambda: {
                auto& l = as<Lambda>(*n);
                params(l.params);
                l.body = expr(std::move(l.body));
                return n;
            }
            case NodeKind::Tuple:
                for (auto& e : as<Tuple>(*n).elems) e = expr(std::move(e));
                return n;
            case NodeKind::Splice: {
                auto& s = as<Splice>(*n);
                s.expr = expr(std::move(s.expr));
                return n;
            }
            case NodeKind::NamedArg: {
                auto& a = as<NamedArg>(*n);
                a.value = expr(std::move(a.value));
                return n;
            }
            case NodeKind::ValDef: {
                auto& v = as<ValDef>(*n);
                v.rhs = expr(std::move(v.rhs));
                if (v.rhs && isUnitType(v.type.get())) v.rhs = discardValue(std::move(v.rhs));
                return n;
            }
            case NodeKind::DefDef: return defDef(std::move(n));
            case NodeKind::TemplateDef: {
                auto& t = as<TemplateDef>(*n);
                params(t.ctorParams);
                for (auto& p : t.parents)
                    for (auto& arg : p.args) arg = expr(std::move(arg));
                stats(t.body);
                return n;
            }
            case NodeKind::New: {
                for (auto& arg : as<New>(*n).args) arg = expr(std::move(arg));
                return n;
            }
            case NodeKind::Match: {
                auto& m = as<Match>(*n);
                m.scrutinee = expr(std::move(m.scrutinee));
                for (CaseDef& c : m.cases) {
                    if (c.guard) c.guard = expr(std::move(c.guard));
                    c.body = expr(std::move(c.body));
                }
                return n;
            }
            case NodeKind::For: return expr(forExpr(as<For>(*n)));  // rewrite, then desugar it
            default:
                return n;  // literals, identifiers, imports, interpolations
        }
    }

    // A statement list: pattern vals are expanded, then every statement is desugared.
    void stats(std::vector<NodePtr>& ss) {
        expandPatternVals(ss);
        for (auto& s : ss) s = expr(std::move(s));
    }

    // Every case class of `ss` gets a companion object with the synthesised
    // apply and unapply (DESIGN §4.5), unless the companion defines them.
    void synthesizeCompanions(std::vector<NodePtr>& ss) {
        for (std::size_t k = 0; k < ss.size(); ++k) {
            if (ss[k]->kind != NodeKind::TemplateDef) continue;
            const auto& c = as<TemplateDef>(*ss[k]);
            if (!c.isCase || c.kind != TemplateKind::Class) continue;
            TemplateDef* companion = nullptr;
            for (auto& s : ss)
                if (s->kind == NodeKind::TemplateDef &&
                    as<TemplateDef>(*s).kind == TemplateKind::Object &&
                    as<TemplateDef>(*s).name == c.name)
                    companion = &as<TemplateDef>(*s);
            if (!companion) {
                auto obj = std::make_unique<TemplateDef>(c.pos);
                obj->kind = TemplateKind::Object;
                obj->name = c.name;
                obj->synthetic = true;
                companion = obj.get();
                ss.insert(ss.begin() + static_cast<std::ptrdiff_t>(k + 1), std::move(obj));
            }
            auto defines = [&](const char* name) {
                for (const auto& m : companion->body)
                    if (m->kind == NodeKind::DefDef && as<DefDef>(*m).name == name) return true;
                return false;
            };
            if (!defines("apply")) companion->body.push_back(syntheticApply(c));
            if (!defines("unapply")) companion->body.push_back(syntheticUnapply(c.pos));
        }
    }

private:
    int tempCounter_ = 0;
    int patternCounter_ = 0;  // <pN>, <bN>

    // `Unit` / `scala.Unit` as written (types are not resolved in Phase 1).
    static bool isUnitType(const TypeTree* t) {
        return t && t->kind == TypeTree::Kind::Name && (t->name == "Unit" || t->name == "scala.Unit");
    }

    // True when the (desugared) expression is known to yield () already, so
    // value discarding needs no extra code.
    static bool yieldsUnit(const Node& n) {
        switch (n.kind) {
            case NodeKind::UnitLit: case NodeKind::While: case NodeKind::Assign:
            case NodeKind::Return:
                return true;
            case NodeKind::If: {
                const auto& i = as<If>(n);
                return i.elsep && yieldsUnit(*i.thenp) && yieldsUnit(*i.elsep);
            }
            case NodeKind::Block: {
                const auto& b = as<Block>(n);
                if (b.stats.empty()) return true;
                const Node& last = *b.stats.back();
                return last.kind == NodeKind::ValDef || last.kind == NodeKind::DefDef ||
                       last.kind == NodeKind::Import || yieldsUnit(last);
            }
            default:
                return false;
        }
    }

    // Value discarding (Scala 3 reference, "Value Discarding"): an expression
    // whose expected type is Unit is evaluated for its effect and () is its
    // value: `e` becomes `{ e; () }`.
    static NodePtr discardValue(NodePtr e) {
        if (!e || yieldsUnit(*e)) return e;
        const SourcePos pos = e->pos;
        auto block = std::make_unique<Block>(pos);
        block->stats.push_back(std::move(e));
        block->stats.push_back(std::make_unique<UnitLit>(pos));
        return block;
    }

    // The `return e` statements that return from a def declared `: Unit`
    // discard e's value. Nested defs own their returns and are skipped;
    // lambdas are searched (a return in them targets the enclosing def).
    static void discardReturnValues(Node& n) {
        checkNativeStack(StackUse::Source);
        auto visit = [](NodePtr& c) { if (c) discardReturnValues(*c); };
        switch (n.kind) {
            case NodeKind::Return: {
                auto& r = as<Return>(n);
                visit(r.value);
                if (r.value) r.value = discardValue(std::move(r.value));
                return;
            }
            case NodeKind::DefDef: return;
            case NodeKind::Select: visit(as<Select>(n).qualifier); return;
            case NodeKind::Apply: {
                auto& a = as<Apply>(n);
                visit(a.fn);
                for (auto& arg : a.args) visit(arg);
                return;
            }
            case NodeKind::TypeApply: visit(as<TypeApply>(n).fn); return;
            case NodeKind::Assign: visit(as<Assign>(n).target); visit(as<Assign>(n).value); return;
            case NodeKind::If: {
                auto& i = as<If>(n);
                visit(i.cond); visit(i.thenp); visit(i.elsep);
                return;
            }
            case NodeKind::While: visit(as<While>(n).cond); visit(as<While>(n).body); return;
            case NodeKind::Block: for (auto& s : as<Block>(n).stats) visit(s); return;
            case NodeKind::Lambda: visit(as<Lambda>(n).body); return;
            case NodeKind::Tuple: for (auto& e : as<Tuple>(n).elems) visit(e); return;
            case NodeKind::Splice: visit(as<Splice>(n).expr); return;
            case NodeKind::NamedArg: visit(as<NamedArg>(n).value); return;
            case NodeKind::ValDef: visit(as<ValDef>(n).rhs); return;
            case NodeKind::New: for (auto& a : as<New>(n).args) visit(a); return;
            case NodeKind::Match: {
                auto& m = as<Match>(n);
                visit(m.scrutinee);
                for (CaseDef& c : m.cases) {
                    visit(c.guard);
                    visit(c.body);
                }
                return;
            }
            default: return;  // literals, identifiers, imports
        }
    }

    void params(std::vector<Param>& ps) {
        for (auto& p : ps) p.defaultValue = expr(std::move(p.defaultValue));
    }

    // `a`, `a.b`, `a.b.c`: a receiver that can be evaluated twice safely.
    static bool isSimplePath(const Node& n) {
        return n.kind == NodeKind::Ident ||
               (n.kind == NodeKind::Select && isSimplePath(*as<Select>(n).qualifier));
    }

    static NodePtr call(SourcePos pos, NodePtr receiver, const std::string& name, NodePtr arg) {
        auto sel = std::make_unique<Select>(pos, std::move(receiver), name);
        auto app = std::make_unique<Apply>(pos, std::move(sel));
        app->args.push_back(std::move(arg));
        return app;
    }

    NodePtr infix(NodePtr n) {
        auto& in = as<Infix>(*n);
        const SourcePos pos = in.pos;
        NodePtr lhs = expr(std::move(in.lhs));
        NodePtr rhs = expr(std::move(in.rhs));
        const std::string op = in.op;
        if (isAssignmentOperator(op)) {
            const std::string base = op.substr(0, op.size() - 1);
            if (lhs->kind == NodeKind::Ident) {
                const auto& id = as<Ident>(*lhs);
                auto target = std::make_unique<Ident>(id.pos, id.name);
                auto value = call(pos, std::move(lhs), base, std::move(rhs));
                return std::make_unique<Assign>(pos, std::move(target), std::move(value));
            }
            if (lhs->kind == NodeKind::Select && isSimplePath(*as<Select>(*lhs).qualifier)) {
                // obj.x op= v  →  obj.x_=(obj.x op v) (Q19 of Phase 1: no op= member lookup)
                auto& s = as<Select>(*lhs);
                NodePtr receiver = cloneSimpleExpr(*s.qualifier);
                const std::string setter = s.name + "_=";
                NodePtr value = call(pos, std::move(lhs), base, std::move(rhs));
                return call(pos, std::move(receiver), setter, std::move(value));
            }
            return call(pos, std::move(lhs), op, std::move(rhs));
        }
        if (!isRightAssociative(op)) return call(pos, std::move(lhs), op, std::move(rhs));
        const NodeKind k = lhs->kind;
        const bool simple = k == NodeKind::Ident || k == NodeKind::IntLit ||
                            k == NodeKind::FloatLit || k == NodeKind::StringLit ||
                            k == NodeKind::CharLit || k == NodeKind::BoolLit ||
                            k == NodeKind::NullLit;
        if (simple) return call(pos, std::move(rhs), op, std::move(lhs));
        const std::string temp = "<ra" + std::to_string(tempCounter_++) + ">";
        auto block = std::make_unique<Block>(pos);
        auto val = std::make_unique<ValDef>(pos);
        val->name = temp;
        val->rhs = std::move(lhs);
        block->stats.push_back(std::move(val));
        block->stats.push_back(
            call(pos, std::move(rhs), op, std::make_unique<Ident>(pos, temp)));
        return block;
    }

    NodePtr defDef(NodePtr n) {
        auto& d = as<DefDef>(*n);
        for (auto& list : d.paramLists) params(list);
        NodePtr body = expr(std::move(d.body));
        if (body && isUnitType(d.resultType.get())) {
            discardReturnValues(*body);
            body = discardValue(std::move(body));
        }
        // def f(a)(b)(c) = e  →  def f(a) = (b) => (c) => e; the innermost
        // lambda's body is the def's body, so `return` returns from it.
        bool innermost = true;
        d.curried = d.paramLists.size() > 1;
        while (d.paramLists.size() > 1) {
            auto lambda = std::make_unique<Lambda>(d.pos);
            lambda->ownsReturn = innermost;
            innermost = false;
            lambda->params = std::move(d.paramLists.back());
            d.paramLists.pop_back();
            lambda->body = std::move(body);
            body = std::move(lambda);
        }
        d.body = std::move(body);
        return n;
    }

    // --- Case-class companions -------------------------------------------

    static NodePtr syntheticApply(const TemplateDef& c) {
        auto d = std::make_unique<DefDef>(c.pos);
        d->name = "apply";
        d->synthetic = true;
        auto made = std::make_unique<New>(c.pos);
        made->type = std::make_unique<TypeTree>();
        made->type->kind = TypeTree::Kind::Name;
        made->type->name = c.name;
        made->type->pos = c.pos;
        made->hasArgs = true;
        std::vector<Param> ps;
        for (const Param& p : c.ctorParams) {
            Param q;
            q.name = p.name;
            q.pos = p.pos;
            q.repeated = p.repeated;
            if (p.type) q.type = cloneType(*p.type);
            ps.push_back(std::move(q));
            NodePtr arg = std::make_unique<Ident>(p.pos, p.name);
            if (p.repeated) arg = std::make_unique<Splice>(p.pos, std::move(arg));
            made->args.push_back(std::move(arg));
        }
        d->paramLists.push_back(std::move(ps));
        d->body = std::move(made);
        return d;
    }

    static NodePtr syntheticUnapply(SourcePos pos) {  // Scala 3: unapply(x) returns x
        auto d = std::make_unique<DefDef>(pos);
        d->name = "unapply";
        d->synthetic = true;
        Param p;
        p.name = "<u>";
        p.pos = pos;
        std::vector<Param> ps;
        ps.push_back(std::move(p));
        d->paramLists.push_back(std::move(ps));
        d->body = std::make_unique<Ident>(pos, "<u>");
        return d;
    }

    // --- Patterns ---------------------------------------------------------

    static PatternPtr makePattern(Pattern::Kind k, SourcePos pos) {
        auto p = std::make_unique<Pattern>();
        p->kind = k;
        p->pos = pos;
        return p;
    }

    // The variables a pattern binds, in source order (Alt contributes none:
    // the compiler rejects variables in alternatives).
    static void patternVariables(const Pattern& p, std::vector<std::string>& out) {
        switch (p.kind) {
            case Pattern::Kind::Var: if (p.name != "_") out.push_back(p.name); return;
            case Pattern::Kind::Bind: out.push_back(p.name); patternVariables(*p.args[0], out); return;
            case Pattern::Kind::SeqWildcard: if (!p.name.empty()) out.push_back(p.name); return;
            case Pattern::Kind::Alt: return;
            default:
                for (const auto& a : p.args) patternVariables(*a, out);
                return;
        }
    }

    // Patterns that always match (types are erased, so a tuple pattern is
    // trusted to meet tuples; a non-tuple element raises MatchError).
    static bool irrefutable(const Pattern& p) {
        switch (p.kind) {
            case Pattern::Kind::Wildcard: case Pattern::Kind::Var: return true;
            case Pattern::Kind::Bind: return irrefutable(*p.args[0]);
            case Pattern::Kind::Tuple:
                for (const auto& a : p.args) if (!irrefutable(*a)) return false;
                return true;
            default: return false;
        }
    }

    std::string fresh(const char* prefix) {
        return std::string("<") + prefix + std::to_string(patternCounter_++) + ">";
    }

    // p => body, as a lambda: `x => body` for a variable, `_ => body` for a
    // wildcard, otherwise `<pN> => <pN> match { case p => body }`.
    NodePtr lambdaFor(PatternPtr p, NodePtr body, SourcePos pos) {
        auto lambda = std::make_unique<Lambda>(pos);
        Param param;
        param.pos = pos;
        if (p->kind == Pattern::Kind::Var || p->kind == Pattern::Kind::Wildcard) {
            param.name = p->kind == Pattern::Kind::Var ? p->name : "_";
            lambda->params.push_back(std::move(param));
            lambda->body = std::move(body);
            return lambda;
        }
        param.name = fresh("p");
        auto m = std::make_unique<Match>(pos);
        m->scrutinee = std::make_unique<Ident>(pos, param.name);
        CaseDef c;
        c.pos = pos;
        c.pattern = std::move(p);
        c.body = std::move(body);
        m->cases.push_back(std::move(c));
        lambda->params.push_back(std::move(param));
        lambda->body = std::move(m);
        return lambda;
    }

    // <pN> => <pN> match { case p => true; case _ => false }
    NodePtr matchesLambda(const Pattern& p, SourcePos pos) {
        auto lambda = std::make_unique<Lambda>(pos);
        Param param;
        param.pos = pos;
        param.name = fresh("p");
        auto m = std::make_unique<Match>(pos);
        m->scrutinee = std::make_unique<Ident>(pos, param.name);
        CaseDef yes;
        yes.pos = pos;
        yes.pattern = clonePattern(p);
        yes.body = std::make_unique<BoolLit>(pos, true);
        CaseDef no;
        no.pos = pos;
        no.pattern = makePattern(Pattern::Kind::Wildcard, pos);
        no.body = std::make_unique<BoolLit>(pos, false);
        m->cases.push_back(std::move(yes));
        m->cases.push_back(std::move(no));
        lambda->params.push_back(std::move(param));
        lambda->body = std::move(m);
        return lambda;
    }

    // The name bound to the whole value matched by `p`, adding `<bN> @` when
    // `p` binds none.
    std::string boundName(PatternPtr& p) {
        if (p->kind == Pattern::Kind::Var && p->name != "_") return p->name;
        if (p->kind == Pattern::Kind::Bind) return p->name;
        auto bind = makePattern(Pattern::Kind::Bind, p->pos);
        bind->name = fresh("b");
        bind->args.push_back(std::move(p));
        p = std::move(bind);
        return p->name;
    }

    // --- For-comprehensions ------------------------------------------------

    struct Generator {
        PatternPtr pattern;
        NodePtr source;
        SourcePos pos;
    };

    // Scala 3 reference, "For-comprehensions": each guard and value definition
    // attaches to the generator before it; the generators then fold from the
    // right into flatMap ... map (yield) or foreach ... foreach.
    NodePtr forExpr(For& f) {
        std::vector<Generator> gens;
        for (Enumerator& en : f.enums) {
            switch (en.kind) {
                case Enumerator::Kind::Generator: {
                    Generator g{std::move(en.pattern), std::move(en.expr), en.pos};
                    if (!irrefutable(*g.pattern))
                        g.source = call(en.pos, std::move(g.source), "withFilter",
                                        matchesLambda(*g.pattern, en.pos));
                    gens.push_back(std::move(g));
                    break;
                }
                case Enumerator::Kind::Guard: {
                    Generator& g = gens.back();
                    g.source = call(en.pos, std::move(g.source), "withFilter",
                                    lambdaFor(clonePattern(*g.pattern), std::move(en.expr), en.pos));
                    break;
                }
                case Enumerator::Kind::Value: {
                    // p1 <- e; p2 = v  →
                    //   (x1 @ p1, x2 @ p2) <- e.map { case x1 @ p1 => val x2 @ p2 = v; (x1, x2) }
                    Generator& g = gens.back();
                    const std::string n1 = boundName(g.pattern);
                    const std::string n2 = boundName(en.pattern);
                    auto body = std::make_unique<Block>(en.pos);
                    auto vd = std::make_unique<ValDef>(en.pos);
                    if (en.pattern->kind == Pattern::Kind::Var) vd->name = en.pattern->name;
                    else vd->pattern = clonePattern(*en.pattern);
                    vd->rhs = std::move(en.expr);
                    body->stats.push_back(std::move(vd));
                    auto tuple = std::make_unique<Tuple>(en.pos);
                    tuple->elems.push_back(std::make_unique<Ident>(en.pos, n1));
                    tuple->elems.push_back(std::make_unique<Ident>(en.pos, n2));
                    body->stats.push_back(std::move(tuple));
                    g.source = call(en.pos, std::move(g.source), "map",
                                    lambdaFor(clonePattern(*g.pattern), std::move(body), en.pos));
                    auto both = makePattern(Pattern::Kind::Tuple, en.pos);
                    both->args.push_back(std::move(g.pattern));
                    both->args.push_back(std::move(en.pattern));
                    g.pattern = std::move(both);
                    break;
                }
            }
        }
        return foldGenerators(gens, 0, std::move(f.body), f.isYield);
    }

    NodePtr foldGenerators(std::vector<Generator>& gens, std::size_t i, NodePtr body, bool isYield) {
        Generator& g = gens[i];
        const bool last = i + 1 == gens.size();
        NodePtr inner = last ? std::move(body) : foldGenerators(gens, i + 1, std::move(body), isYield);
        const char* method = !isYield ? "foreach" : last ? "map" : "flatMap";
        return call(g.pos, std::move(g.source), method,
                    lambdaFor(std::move(g.pattern), std::move(inner), g.pos));
    }

    // --- Pattern value definitions ----------------------------------------

    // val p = e  →  val <tN> = e match { case p => (v1, ..., vn) }; val vi = <tN>._i
    void expandPatternVals(std::vector<NodePtr>& ss) {
        std::vector<NodePtr> out;
        for (NodePtr& s : ss) {
            if (s->kind != NodeKind::ValDef || !as<ValDef>(*s).pattern) {
                out.push_back(std::move(s));
                continue;
            }
            auto& v = as<ValDef>(*s);
            const SourcePos pos = v.pos;
            std::vector<std::string> vars;
            patternVariables(*v.pattern, vars);
            auto m = std::make_unique<Match>(pos);
            m->scrutinee = std::move(v.rhs);
            CaseDef c;
            c.pos = pos;
            c.pattern = std::move(v.pattern);
            if (vars.empty()) {
                c.body = std::make_unique<UnitLit>(pos);
            } else if (vars.size() == 1) {
                c.body = std::make_unique<Ident>(pos, vars[0]);
            } else {
                auto t = std::make_unique<Tuple>(pos);
                for (const auto& name : vars) t->elems.push_back(std::make_unique<Ident>(pos, name));
                c.body = std::move(t);
            }
            m->cases.push_back(std::move(c));
            auto val = [&](std::string name, NodePtr rhs) {
                auto d = std::make_unique<ValDef>(pos);
                d->name = std::move(name);
                d->isVar = v.isVar;
                d->mods = v.mods;
                d->rhs = std::move(rhs);
                out.push_back(std::move(d));
            };
            if (vars.empty()) {
                out.push_back(std::move(m));
            } else if (vars.size() == 1) {
                val(vars[0], std::move(m));
            } else {
                const std::string temp = "<t" + std::to_string(tempCounter_++) + ">";
                val(temp, std::move(m));
                for (std::size_t k = 0; k < vars.size(); ++k)
                    val(vars[k], std::make_unique<Select>(pos, std::make_unique<Ident>(pos, temp),
                                                          "_" + std::to_string(k + 1)));
            }
        }
        ss = std::move(out);
    }
};

} // namespace

void desugar(CompilationUnit& unit) {
    Desugarer ds;
    ds.synthesizeCompanions(unit.stats);
    ds.stats(unit.stats);
}

NodePtr desugarExpr(NodePtr e) {
    Desugarer ds;
    return ds.expr(std::move(e));
}

} // namespace protoScala
