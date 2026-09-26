#include <algorithm>
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
            // Phase 4: a try's body, its catch bodies and its finally body are
            // ordinary expressions in the same frame, so they are desugared like
            // any other (P2).
            case NodeKind::Throw: {
                auto& t = as<Throw>(*n);
                t.value = expr(std::move(t.value));
                return n;
            }
            case NodeKind::Try: {
                auto& t = as<Try>(*n);
                t.body = expr(std::move(t.body));
                for (CaseDef& c : t.cases) {
                    if (c.guard) c.guard = expr(std::move(c.guard));
                    c.body = expr(std::move(c.body));
                }
                if (t.finallyBody) t.finallyBody = expr(std::move(t.finallyBody));
                return n;
            }
            case NodeKind::ExtensionDef: {
                // Each member is an ordinary def; the compiler installs it on the
                // receiver type's prototype.
                auto& e = as<ExtensionDef>(*n);
                for (auto& m : e.members) m = expr(std::move(m));
                return n;
            }
            case NodeKind::For: return expr(forExpr(as<For>(*n)));  // rewrite, then desugar it
            case NodeKind::InterpString: {
                auto& s = as<InterpString>(*n);
                for (NodePtr& a : s.args) a = expr(std::move(a));
                // s / raw / f are compiled directly (CONCAT, or a call of __fmt).
                if (s.interpolator == "s" || s.interpolator == "raw" || s.interpolator == "f")
                    return n;
                // Phase 4: any other interpolator is lowered to
                // `StringContext(<literals>).<name>(<args>)`, exactly as Scala
                // lowers it, so a custom interpolator is an EXTENSION METHOD on
                // StringContext and needs nothing else. This is what closes Phase
                // 3's restriction to s, f and raw. `new` rather than the companion
                // apply, so the lowering does not depend on a companion existing.
                auto ctor = std::make_unique<New>(s.pos);
                ctor->type = nameType("StringContext", s.pos);
                ctor->hasArgs = true;
                for (const std::string& lit : s.literals) {
                    auto piece = std::make_unique<StringLit>(s.pos);
                    piece->value = lit;
                    ctor->args.push_back(std::move(piece));
                }
                auto call = std::make_unique<Apply>(
                    s.pos, std::make_unique<Select>(s.pos, std::move(ctor), s.interpolator));
                for (NodePtr& a : s.args) call->args.push_back(std::move(a));
                return call;   // its arguments are already desugared
            }
            default:
                return n;  // literals, identifiers, imports
        }
    }

    // A statement list: pattern vals are expanded, then every statement is desugared.
    void stats(std::vector<NodePtr>& ss) {
        expandPatternVals(ss);
        for (auto& s : ss) s = expr(std::move(s));
    }

    // Phase 4: `enum` is lowered entirely in the frontend (DESIGN §4.5), to a
    // sealed abstract class, one `case object` or `case class` per case inside
    // the companion, and the generated `values` / `valueOf` / `fromOrdinal`. No
    // new compiler concept, no new opcode and no runtime prototype: pattern
    // matching, toString, equals, hashCode and unapply all come from Phase 2's
    // case-class machinery, and Task 7's lifting gives each case the qualified
    // name `Color.Red` that Scala requires.
    void expandEnums(std::vector<NodePtr>& ss) {
        std::vector<NodePtr> out;
        for (NodePtr& s : ss) {
            if (!s) {
                out.push_back(std::move(s));
                continue;
            }
            if (s->kind == NodeKind::TemplateDef) {
                TemplateDef& t = as<TemplateDef>(*s);
                if (t.kind == TemplateKind::Enum) {
                    expandEnum(t, out);
                    continue;
                }
                // Recurse into the body: an `enum` nested in an `object` must be
                // expanded into its sealed class, its companion and its cases
                // BEFORE liftNestedTemplates runs, or the lift moves an
                // unexpanded Enum template to the top level and every reference
                // to it fails with "Not found". A `class` or a `trait` body is
                // walked too; a template nested in one is refused by the lift
                // (D80), and expanding first makes that message the one a reader
                // sees instead of a resolution failure.
                //
                // This matters much more since Phase 6, because a module file is
                // desugared into an `object` (D91): without it, a module that
                // defines an `enum` is unusable.
                expandEnums(t.body);
            }
            out.push_back(std::move(s));
        }
        mergeEnumCompanions(out);
        ss = std::move(out);
    }

    // Scala has exactly one companion object per enum, so a hand-written
    // `object <Name>` is the *same* object as the one `expandEnum` generates for
    // the cases and for `values` / `valueOf` / `fromOrdinal`, not a second one.
    // Two objects of one name reached the compiler as two templates sharing a
    // single type key, whose ClassInfo described one body while the other was
    // compiled against it -- `info.members.at("fromOrdinal")` then threw
    // `std::out_of_range("unordered_map::at")` and escaped as
    // `protoscala: internal error`, which D74 calls a bug. The generated
    // companion absorbs the hand-written one and keeps its own position, right
    // after the sealed class.
    void mergeEnumCompanions(std::vector<NodePtr>& ss) {
        for (NodePtr& g : ss) {
            if (!g || g->kind != NodeKind::TemplateDef) continue;
            TemplateDef& gen = as<TemplateDef>(*g);
            if (!gen.enumCompanion || gen.kind != TemplateKind::Object) continue;
            for (NodePtr& u : ss) {
                if (!u || u.get() == g.get() || u->kind != NodeKind::TemplateDef) continue;
                TemplateDef& user = as<TemplateDef>(*u);
                if (user.enumCompanion || user.kind != TemplateKind::Object ||
                    user.name != gen.name)
                    continue;
                // The hand-written object owns the modifiers, the parents and the
                // self alias; the generated one owns the cases and the generated
                // methods, which come first so a hand-written member can call
                // them.
                gen.mods = user.mods;
                gen.isCase = user.isCase;
                gen.selfName = user.selfName;
                gen.parents = std::move(user.parents);
                for (NodePtr& m : user.body) gen.body.push_back(std::move(m));
                u.reset();   // the statement list tolerates a null (expandEnums does)
                break;
            }
        }
        ss.erase(std::remove(ss.begin(), ss.end(), nullptr), ss.end());
    }

    // Phase 4: a template nested in an `object` is LIFTED to the top level with
    // a qualified name — `object O { class C }` becomes `class O.C` plus an empty
    // `object O` — because a class prototype is built by top-level code and
    // `.` cannot occur in a Scala identifier, so `@O.C` can never collide with a
    // type key a user could write. Nesting is arbitrarily deep (`O.P.C`).
    //
    // Nesting in a `class` or a `trait` is still rejected: such a template would
    // capture the enclosing instance, which needs a per-instance class (D80).
    void liftNestedTemplates(std::vector<NodePtr>& ss) {
        std::vector<NodePtr> lifted;
        LiftScope unitScope;
        for (const NodePtr& s : ss)
            if (s && s->kind == NodeKind::TemplateDef)
                unitScope.names.push_back(as<TemplateDef>(*s).name);
        std::vector<LiftScope> scopes{std::move(unitScope)};
        for (auto& s : ss)
            if (s->kind == NodeKind::TemplateDef) liftFrom(as<TemplateDef>(*s), lifted, scopes);
        if (lifted.empty()) return;
        // The lifted templates come FIRST, so the object they were written in can
        // reference them from its own body.
        lifted.reserve(lifted.size() + ss.size());
        for (auto& s : ss) lifted.push_back(std::move(s));
        ss = std::move(lifted);
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

    // `t.name` prefixed onto every sibling name a parent reference names, before
    // the siblings themselves are renamed: `case class Leaf(...) extends T`
    // inside `object Ast` has to become `... extends Ast.T`, because the parent
    // is resolved before any template scope exists.
    // One enclosing level: the qualified prefix of the templates declared there
    // ("" at the unit's top level) and their names.
    struct LiftScope {
        std::string prefix;
        std::vector<std::string> names;
    };

    // Resolves a parent name against the enclosing scopes, INNERMOST FIRST, which
    // is Scala's scoping. Qualifying against the immediate siblings alone was
    // enough while only hand-written templates nested, and stopped being enough
    // when `enum` started expanding inside an `object`: the generated
    // `case object Debug extends Level` sits in the companion `E.Level`, while
    // `Level` is a sibling of the OUTER object `E`, so it has to become
    // `E.Level` and one level of siblings cannot see that.
    static void qualifySibling(TypeTree& ty, const std::vector<LiftScope>& scopes) {
        if (ty.kind != TypeTree::Kind::Name && ty.kind != TypeTree::Kind::Applied) return;
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            for (const std::string& name : it->names)
                if (ty.name == name) {
                    if (!it->prefix.empty()) ty.name = it->prefix + "." + name;
                    return;  // a top-level name needs no prefix, and must not be shadowed
                }
    }

    // A `Name` type tree, for a generated parent reference.
    static TypePtr nameType(const std::string& name, SourcePos pos) {
        auto t = std::make_unique<TypeTree>();
        t->kind = TypeTree::Kind::Name;
        t->name = name;
        t->pos = pos;
        return t;
    }

    void expandEnum(TemplateDef& e, std::vector<NodePtr>& out) {
        // 1. `sealed abstract class <Name>(<params>) extends <parents> with Enum`,
        //    carrying every member of the enum body that is not a case.
        auto cls = std::make_unique<TemplateDef>(e.pos);
        cls->kind = TemplateKind::Class;
        cls->mods = e.mods;
        cls->mods.isAbstract = true;
        cls->mods.isSealed = true;
        cls->name = e.name;
        cls->typeParams = e.typeParams;
        cls->hasParamClause = e.hasParamClause;
        cls->ctorParams = std::move(e.ctorParams);
        cls->parents = std::move(e.parents);
        cls->selfName = e.selfName;
        cls->body = std::move(e.body);
        ParentRef enumParent;
        enumParent.type = nameType(kEnumMarkerKey, e.pos);
        enumParent.type->kind = TypeTree::Kind::Builtin;
        enumParent.pos = e.pos;
        cls->parents.push_back(std::move(enumParent));

        // 2. `object <Name>`, holding one template per case. Task 7 lifts them
        //    out with the qualified names `<Name>.<Case>`, which is exactly what
        //    Scala requires at the use site.
        auto companion = std::make_unique<TemplateDef>(e.pos);
        companion->kind = TemplateKind::Object;
        companion->name = e.name;
        companion->enumCompanion = true;  // mergeEnumCompanions may absorb a hand-written one
        bool allSingletons = true;
        std::vector<std::string> singletons;   // the cases `values` covers
        for (std::size_t i = 0; i < e.enumCases.size(); ++i) {
            TemplateDef::EnumCase& c = e.enumCases[i];
            // `case C` is a case object; `case C()` is a zero-parameter case
            // class, as in Scala, so that `E.C()` finds a companion `apply`.
            const bool singleton = c.params.empty() && !c.hasParens;
            allSingletons = allSingletons && singleton;
            if (singleton) singletons.push_back(c.name);
            auto cse = std::make_unique<TemplateDef>(c.pos);
            cse->kind = singleton ? TemplateKind::Object : TemplateKind::Class;
            cse->isCase = true;
            cse->name = c.name;
            cse->hasParamClause = c.hasParens;
            cse->ctorParams = std::move(c.params);
            ParentRef p;
            p.type = nameType(e.name, c.pos);
            p.args = std::move(c.parentArgs);
            p.hasArgs = c.hasParentArgs;
            p.pos = c.pos;
            cse->parents.push_back(std::move(p));
            // `ordinal` is an ordinary val, so no runtime support is needed for
            // it. Ordinals count EVERY case in declaration order, exactly as
            // Scala's do, even though `values` covers only the singletons.
            auto ordinal = std::make_unique<ValDef>(c.pos);
            ordinal->name = "ordinal";
            ordinal->type = nameType("Int", c.pos);
            auto lit = std::make_unique<IntLit>(c.pos);
            lit->value = static_cast<long long>(i);
            ordinal->rhs = std::move(lit);
            cse->body.push_back(std::move(ordinal));
            companion->body.push_back(std::move(cse));
        }

        // 3. `values`, `valueOf` and `fromOrdinal`, generated as SOURCE TEXT and
        //    parsed, so the generated code cannot drift from what a user could
        //    have written by hand. scalac defines `values` and `valueOf` only
        //    when every case is a singleton, and `fromOrdinal` always, covering
        //    the singletons alone; the messages below are scalac's own.
        std::string src;
        if (allSingletons) {
            src += "def values: List[" + e.name + "] = ";
            for (const std::string& c : singletons) src += c + " :: ";
            src += "Nil\n";
            src += "def valueOf(name: String): " + e.name + " =\n";
            for (const std::string& c : singletons)
                src += "  if name == \"" + c + "\" then " + c + " else\n";
            src += "  throw new IllegalArgumentException(\"enum " + e.name +
                   " has no case with name: \" + name)\n";
        }
        src += "def fromOrdinal(n: Int): " + e.name + " =\n";
        for (const std::string& c : singletons)
            src += "  if n == " + c + ".ordinal then " + c + " else\n";
        src += "  throw new NoSuchElementException(\"enum " + e.name +
               " has no case with ordinal: \" + n)\n";
        std::unique_ptr<CompilationUnit> generated = parseSource(src);
        for (NodePtr& m : generated->stats) {
            rebase(*m, e.pos);   // the generated text has no coordinates of its own
            companion->body.push_back(std::move(m));
        }
        generated->stats.clear();

        out.push_back(std::move(cls));
        out.push_back(std::move(companion));
    }

    void liftFrom(TemplateDef& t, std::vector<NodePtr>& out, std::vector<LiftScope>& scopes) {
        checkNativeStack(StackUse::Source);
        const bool isObject = t.kind == TemplateKind::Object;
        LiftScope own;
        own.prefix = t.name;
        for (const NodePtr& m : t.body)
            if (m && m->kind == NodeKind::TemplateDef) own.names.push_back(as<TemplateDef>(*m).name);
        scopes.push_back(std::move(own));
        std::vector<NodePtr> keep;
        for (NodePtr& m : t.body) {
            if (!m || m->kind != NodeKind::TemplateDef) {
                keep.push_back(std::move(m));
                continue;
            }
            auto& n = as<TemplateDef>(*m);
            if (!isObject)
                throw ParseError("classes, traits and objects must be defined at the top level "
                                 "of a file or in an object",
                                 n.pos, false);
            for (ParentRef& p : n.parents)
                if (p.type) qualifySibling(*p.type, scopes);
            n.name = t.name + "." + n.name;
            liftFrom(n, out, scopes);  // deeper nesting first
            out.push_back(std::move(m));
        }
        scopes.pop_back();
        t.body = std::move(keep);
    }

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
            case NodeKind::Try: {
                auto& t = as<Try>(n);
                visit(t.body);
                for (CaseDef& c : t.cases) {
                    visit(c.guard);
                    visit(c.body);
                }
                visit(t.finallyBody);
                return;
            }
            case NodeKind::Throw: visit(as<Throw>(n).value); return;
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
            lambda->fromCurriedDef = true;
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
                    // Scala 3: `case p <- xs` always filters. Without `case`,
                    // only a refutable pattern does (D35, D36).
                    if (en.hasCase || !irrefutable(*g.pattern))
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
    ds.expandEnums(unit.stats);
    ds.liftNestedTemplates(unit.stats);
    ds.synthesizeCompanions(unit.stats);
    ds.stats(unit.stats);
}

void desugarModule(CompilationUnit& unit, const std::string& objectName) {
    // A module file is a library: an @main in it would never be called, and a
    // silently ignored one is a trap (D91).
    for (const NodePtr& s : unit.stats) {
        if (s->kind == NodeKind::DefDef && static_cast<const DefDef&>(*s).isMain())
            throw ParseError("a module may not define an @main method: " + objectName +
                                 " is imported, not run",
                             s->pos, false);
        // Same argument for `object X extends App` (D104): in a module it would
        // never be run, and a silently ignored entry point is the trap D91
        // refused. The test is the written parent name -- desugar resolves no
        // types -- so a trait that extends App elsewhere is not caught here.
        if (s->kind != NodeKind::TemplateDef) continue;
        const auto& t = static_cast<const TemplateDef&>(*s);
        if (t.kind != TemplateKind::Object) continue;
        for (const ParentRef& p : t.parents)
            if (p.type && p.type->kind == TypeTree::Kind::Name && p.type->name == "App")
                throw ParseError("a module may not define an App object: " + objectName +
                                     " is imported, not run",
                                 t.pos, false);
    }
    auto obj = std::make_unique<TemplateDef>(unit.stats.empty() ? SourcePos{}
                                                                : unit.stats.front()->pos);
    obj->kind = TemplateKind::Object;
    obj->name = objectName;
    obj->synthetic = true;
    // Two kinds of statement stay OUTSIDE the synthetic object, because neither
    // is a member of anything:
    //   - an `import`, which the compiler resolves for the whole unit (D96). A
    //     module that imports another module would otherwise have its import
    //     silently skipped, because a template body ignores Import nodes.
    //   - an `extension`, which must be at the top level of a file and is
    //     session-wide once installed (D82). Its body therefore sees the
    //     module's globals, not its members.
    std::vector<NodePtr> hoisted;
    std::vector<NodePtr> members;
    for (NodePtr& s : unit.stats) {
        if (s->kind == NodeKind::Import || s->kind == NodeKind::ExtensionDef)
            hoisted.push_back(std::move(s));
        else
            members.push_back(std::move(s));
    }
    obj->body = std::move(members);
    unit.stats.clear();
    for (NodePtr& s : hoisted) unit.stats.push_back(std::move(s));
    unit.stats.push_back(std::move(obj));
    // From here it is an ordinary unit with one `object` in it, so Phase 4's
    // lifting, companion synthesis and sibling qualification all apply
    // unchanged: a module needs no lifting code of its own.
    desugar(unit);
}

NodePtr desugarExpr(NodePtr e) {
    Desugarer ds;
    return ds.expr(std::move(e));
}

} // namespace protoScala
