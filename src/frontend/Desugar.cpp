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
                for (auto& s : as<Block>(*n).stats) s = expr(std::move(s));
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
            default:
                return n;  // literals, identifiers, imports, interpolations
        }
    }

private:
    int tempCounter_ = 0;

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
            default: return;  // literals, identifiers, imports
        }
    }

    void params(std::vector<Param>& ps) {
        for (auto& p : ps) p.defaultValue = expr(std::move(p.defaultValue));
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
};

} // namespace

void desugar(CompilationUnit& unit) {
    Desugarer ds;
    for (auto& s : unit.stats) s = ds.expr(std::move(s));
}

NodePtr desugarExpr(NodePtr e) {
    Desugarer ds;
    return ds.expr(std::move(e));
}

} // namespace protoScala
