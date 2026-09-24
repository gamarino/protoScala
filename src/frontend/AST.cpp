/*
 * AST rendering — the S-expression form used by the unit tests.
 *
 * Format (one line, single spaces):
 *   IntLit            (int <value>)        — `digits` when the value does not fit a long
 *   FloatLit          (float <source text>) — with a leading '-' when negated
 *   StringLit         (str "<escaped>")     — \n \t \" \\ escaped
 *   CharLit           (char '<c>')
 *   BoolLit/Null/Unit true false / null / ()
 *   InterpString      (interp <interpolator> "lit" <arg> ["%spec"] ... "lit")
 *   Ident / Select    name / (. <qualifier> name)
 *   Apply / TypeApply (apply <fn> <arg>...) / (tapply <fn> <type>...)
 *   Infix / Prefix    (infix <op> <lhs> <rhs>) / (prefix <op> <e>)
 *   Assign            (= <target> <value>)
 *   If / While        (if c t) or (if c t e) / (while c b)
 *   Return            (return) or (return v)
 *   Block             (block <stat>...)
 *   Lambda            (lambda (<param>...) <body>), param `name` or `name:<type>`
 *   Typed / Parens    (typed e T) / (parens e)
 *   Tuple / Splice    (tuple a b ...) / (splice e)
 *   NamedArg          (named n v)
 *   ValDef            (val [mods] x [: T] [<rhs>]), (var x ...), (lazy-val x ...);
 *                     no <rhs> for an abstract member
 *   DefDef            (def [@ann ...] [mods] name [[A B]] [(<list>...)] [: T] [<body>]),
 *                     each list `(<param>...)`, param `name:T`, `name:=> T`,
 *                     `name:T*`, with ` = <default>` appended; no <body> for
 *                     an abstract member; name `this` for an auxiliary constructor
 *                     [mods]: ` private`, ` override`, ` abstract` (only the
 *                     ones set, in that order)
 *   Import            (import <text>)
 *   TemplateDef       (<kind> [abstract] [final] [sealed] Name [[A B]]
 *                      [(<class-param>...)] [(extends <parent>...)] [(self s)]
 *                      (body <stat>...))
 *                     <kind>: class, case-class, trait, object, case-object;
 *                     <class-param>: `[private ]val x:Int`, `[private ]var x:Int`
 *                     or `x:Int`, with ` = <default>` appended;
 *                     <parent>: `T`, or `(T <arg>...)` with an argument list
 *   New               (new T <arg>...)
 *   Match             (match <scrutinee> (case <pat> [(if <guard>)] <body>)...)
 *   For               (for-yield|for-do <enum>... <body>), <enum> one of
 *                     (<- <pat> <expr>), (if <expr>), (= <pat> <expr>)
 *   pattern ValDef    (val-pat [mods] <pat> <rhs>), (var-pat ...)
 *   Pattern           _, x, a literal as its expression, (stable <path>),
 *                     (: <pat> <type>), (@ x <pat>), (| <pat>...),
 *                     (unapply <path> <pat>...), (tuple-pat <pat>...),
 *                     _* or (_* rest)
 *   CompilationUnit   (unit <stat>...)
 *   TypeTree          Name, a.b.C, C[A, B], (A, B) => R, (A, B), => T, T*, ?, A | B
 */
#include "frontend/AST.h"

#include <stdexcept>

namespace protoScala {

ValDef::ValDef(SourcePos p) : Node(NodeKind::ValDef, p) {}
ValDef::~ValDef() = default;

namespace {

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

void appendEscaped(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += c; break;
        }
    }
}

void renderType(std::string& out, const TypeTree& t);

void renderTypeList(std::string& out, const std::vector<TypePtr>& ts, std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
        if (k) out += ", ";
        renderType(out, *ts[k]);
    }
}

void renderType(std::string& out, const TypeTree& t) {
    switch (t.kind) {
        case TypeTree::Kind::Name:
            out += t.name;
            break;
        case TypeTree::Kind::Applied:
            out += t.name;
            out += '[';
            renderTypeList(out, t.args, t.args.size());
            out += ']';
            break;
        case TypeTree::Kind::Function:
            out += '(';
            renderTypeList(out, t.args, t.args.empty() ? 0 : t.args.size() - 1);
            out += ") => ";
            if (!t.args.empty()) renderType(out, *t.args.back());
            break;
        case TypeTree::Kind::Tuple:
            out += '(';
            renderTypeList(out, t.args, t.args.size());
            out += ')';
            break;
        case TypeTree::Kind::ByName:
            out += "=> ";
            renderType(out, *t.args.front());
            break;
        case TypeTree::Kind::Repeated:
            renderType(out, *t.args.front());
            out += '*';
            break;
        case TypeTree::Kind::Wildcard:
            out += '?';
            break;
        case TypeTree::Kind::Infix:
            renderType(out, *t.args[0]);
            out += ' ';
            out += t.name;
            out += ' ';
            renderType(out, *t.args[1]);
            break;
    }
}

void render(std::string& out, const Node& n);

void renderPattern(std::string& out, const Pattern& p) {
    using K = Pattern::Kind;
    switch (p.kind) {
        case K::Wildcard: out += '_'; return;
        case K::Var: out += p.name; return;
        case K::Literal: render(out, *p.expr); return;
        case K::Stable:
            out += "(stable ";
            render(out, *p.expr);
            out += ')';
            return;
        case K::Typed:
            out += "(: ";
            renderPattern(out, *p.args[0]);
            out += ' ';
            renderType(out, *p.type);
            out += ')';
            return;
        case K::Bind:
            out += "(@ " + p.name + ' ';
            renderPattern(out, *p.args[0]);
            out += ')';
            return;
        case K::Alt: case K::Extractor: case K::Tuple:
            out += p.kind == K::Alt ? "(|" : p.kind == K::Tuple ? "(tuple-pat" : "(unapply ";
            if (p.kind == K::Extractor) render(out, *p.expr);
            for (const PatternPtr& a : p.args) {
                out += ' ';
                renderPattern(out, *a);
            }
            out += ')';
            return;
        case K::SeqWildcard:
            out += p.name.empty() ? "_*" : "(_* " + p.name + ")";
            return;
    }
}

void renderChildren(std::string& out, const std::vector<NodePtr>& nodes) {
    for (const NodePtr& c : nodes) {
        out += ' ';
        render(out, *c);
    }
}

// `name`, `name:T`, `name:=> T`, `name:T*`, each with ` = <default>` when present.
void renderParam(std::string& out, const Param& p) {
    out += p.name;
    if (p.type) {
        out += ':';
        // The flags may duplicate a ByName / Repeated type tree; render each once.
        if (p.byName && p.type->kind != TypeTree::Kind::ByName) out += "=> ";
        renderType(out, *p.type);
        if (p.repeated && p.type->kind != TypeTree::Kind::Repeated) out += '*';
    }
    if (p.defaultValue) {
        out += " = ";
        render(out, *p.defaultValue);
    }
}

void renderParams(std::string& out, const std::vector<Param>& params) {
    out += '(';
    for (std::size_t k = 0; k < params.size(); ++k) {
        if (k) out += ' ';
        renderParam(out, params[k]);
    }
    out += ')';
}

void renderMods(std::string& out, const Modifiers& m) {
    if (m.isPrivate) out += " private";
    if (m.isOverride) out += " override";
    if (m.isAbstract) out += " abstract";
}

void renderClassParam(std::string& out, const Param& p) {
    if (p.mods.isPrivate) out += "private ";
    if (p.isVal) out += "val ";
    if (p.isVar) out += "var ";
    renderParam(out, p);
}

void renderParent(std::string& out, const ParentRef& p) {
    if (!p.hasArgs) {
        renderType(out, *p.type);
        return;
    }
    out += '(';
    renderType(out, *p.type);
    renderChildren(out, p.args);
    out += ')';
}

void render(std::string& out, const Node& n) {
    switch (n.kind) {
        case NodeKind::IntLit: {
            const auto& x = as<IntLit>(n);
            out += "(int ";
            out += x.fitsLong ? std::to_string(x.value) : x.digits;
            out += ')';
            break;
        }
        case NodeKind::FloatLit:
            out += "(float " + as<FloatLit>(n).text + ")";
            break;
        case NodeKind::StringLit:
            out += "(str \"";
            appendEscaped(out, as<StringLit>(n).value);
            out += "\")";
            break;
        case NodeKind::CharLit: {
            std::string c;
            appendUtf8(c, as<CharLit>(n).value);
            out += "(char '";
            appendEscaped(out, c);
            out += "')";
            break;
        }
        case NodeKind::BoolLit:
            out += as<BoolLit>(n).value ? "true" : "false";
            break;
        case NodeKind::NullLit:
            out += "null";
            break;
        case NodeKind::UnitLit:
            out += "()";
            break;
        case NodeKind::InterpString: {
            // (interp s "lit0" <arg0> ["%spec0"] "lit1" ...): the parsed form,
            // so a test can see the split without reaching into the node.
            const auto& x = as<InterpString>(n);
            out += "(interp " + x.interpolator;
            for (std::size_t k = 0; k < x.args.size(); ++k) {
                out += " \"";
                appendEscaped(out, x.literals[k]);
                out += "\" ";
                render(out, *x.args[k]);
                if (!x.specs[k].empty()) out += " \"" + x.specs[k] + '"';
            }
            out += " \"";
            appendEscaped(out, x.literals.back());
            out += "\")";
            break;
        }
        case NodeKind::Ident:
            out += as<Ident>(n).name;
            break;
        case NodeKind::Select: {
            const auto& x = as<Select>(n);
            out += "(. ";
            render(out, *x.qualifier);
            out += ' ' + x.name + ')';
            break;
        }
        case NodeKind::Apply: {
            const auto& x = as<Apply>(n);
            out += "(apply ";
            render(out, *x.fn);
            renderChildren(out, x.args);
            out += ')';
            break;
        }
        case NodeKind::TypeApply: {
            const auto& x = as<TypeApply>(n);
            out += "(tapply ";
            render(out, *x.fn);
            for (const TypePtr& t : x.types) {
                out += ' ';
                renderType(out, *t);
            }
            out += ')';
            break;
        }
        case NodeKind::Infix: {
            const auto& x = as<Infix>(n);
            out += "(infix " + x.op + ' ';
            render(out, *x.lhs);
            out += ' ';
            render(out, *x.rhs);
            out += ')';
            break;
        }
        case NodeKind::Prefix: {
            const auto& x = as<Prefix>(n);
            out += "(prefix " + x.op + ' ';
            render(out, *x.operand);
            out += ')';
            break;
        }
        case NodeKind::Assign: {
            const auto& x = as<Assign>(n);
            out += "(= ";
            render(out, *x.target);
            out += ' ';
            render(out, *x.value);
            out += ')';
            break;
        }
        case NodeKind::If: {
            const auto& x = as<If>(n);
            out += "(if ";
            render(out, *x.cond);
            out += ' ';
            render(out, *x.thenp);
            if (x.elsep) {
                out += ' ';
                render(out, *x.elsep);
            }
            out += ')';
            break;
        }
        case NodeKind::While: {
            const auto& x = as<While>(n);
            out += "(while ";
            render(out, *x.cond);
            out += ' ';
            render(out, *x.body);
            out += ')';
            break;
        }
        case NodeKind::Return: {
            const auto& x = as<Return>(n);
            out += "(return";
            if (x.value) {
                out += ' ';
                render(out, *x.value);
            }
            out += ')';
            break;
        }
        case NodeKind::Block:
            out += "(block";
            renderChildren(out, as<Block>(n).stats);
            out += ')';
            break;
        case NodeKind::Lambda: {
            const auto& x = as<Lambda>(n);
            out += "(lambda ";
            renderParams(out, x.params);
            out += ' ';
            render(out, *x.body);
            out += ')';
            break;
        }
        case NodeKind::Typed: {
            const auto& x = as<Typed>(n);
            out += "(typed ";
            render(out, *x.expr);
            out += ' ';
            renderType(out, *x.type);
            out += ')';
            break;
        }
        case NodeKind::Parens:
            out += "(parens ";
            render(out, *as<Parens>(n).expr);
            out += ')';
            break;
        case NodeKind::Tuple:
            out += "(tuple";
            renderChildren(out, as<Tuple>(n).elems);
            out += ')';
            break;
        case NodeKind::Splice:
            out += "(splice ";
            render(out, *as<Splice>(n).expr);
            out += ')';
            break;
        case NodeKind::NamedArg: {
            const auto& x = as<NamedArg>(n);
            out += "(named " + x.name + ' ';
            render(out, *x.value);
            out += ')';
            break;
        }
        case NodeKind::ValDef: {
            const auto& x = as<ValDef>(n);
            if (x.pattern) {
                out += x.isVar ? "(var-pat" : "(val-pat";
                renderMods(out, x.mods);
                out += ' ';
                renderPattern(out, *x.pattern);
                out += ' ';
                render(out, *x.rhs);
                out += ')';
                break;
            }
            out += x.isLazy ? "(lazy-val" : x.isVar ? "(var" : "(val";
            renderMods(out, x.mods);
            out += ' ' + x.name;
            if (x.type) {
                out += " : ";
                renderType(out, *x.type);
            }
            if (x.rhs) {
                out += ' ';
                render(out, *x.rhs);
            }
            out += ')';
            break;
        }
        case NodeKind::DefDef: {
            const auto& x = as<DefDef>(n);
            out += "(def";
            for (const std::string& a : x.annotations) out += " @" + a;
            renderMods(out, x.mods);
            out += ' ' + x.name;
            if (!x.typeParams.empty()) {
                out += " [";
                for (std::size_t k = 0; k < x.typeParams.size(); ++k) {
                    if (k) out += ' ';
                    out += x.typeParams[k];
                }
                out += ']';
            }
            if (!x.paramLists.empty()) {
                out += " (";
                for (std::size_t k = 0; k < x.paramLists.size(); ++k) {
                    if (k) out += ' ';
                    renderParams(out, x.paramLists[k]);
                }
                out += ')';
            }
            if (x.resultType) {
                out += " : ";
                renderType(out, *x.resultType);
            }
            if (x.body) {
                out += ' ';
                render(out, *x.body);
            }
            out += ')';
            break;
        }
        case NodeKind::Import:
            out += "(import " + as<Import>(n).text + ")";
            break;
        case NodeKind::TemplateDef: {
            const auto& x = as<TemplateDef>(n);
            out += '(';
            switch (x.kind) {
                case TemplateKind::Class:  out += x.isCase ? "case-class" : "class"; break;
                case TemplateKind::Trait:  out += "trait"; break;
                case TemplateKind::Object: out += x.isCase ? "case-object" : "object"; break;
                case TemplateKind::Enum:   out += "enum"; break;
            }
            if (x.mods.isAbstract) out += " abstract";
            if (x.mods.isFinal) out += " final";
            if (x.mods.isSealed) out += " sealed";
            out += ' ' + x.name;
            if (!x.typeParams.empty()) {
                out += " [";
                for (std::size_t k = 0; k < x.typeParams.size(); ++k) {
                    if (k) out += ' ';
                    out += x.typeParams[k];
                }
                out += ']';
            }
            if (x.hasParamClause) {
                out += " (";
                for (std::size_t k = 0; k < x.ctorParams.size(); ++k) {
                    if (k) out += ' ';
                    renderClassParam(out, x.ctorParams[k]);
                }
                out += ')';
            }
            if (!x.parents.empty()) {
                out += " (extends";
                for (const ParentRef& p : x.parents) {
                    out += ' ';
                    renderParent(out, p);
                }
                out += ')';
            }
            if (!x.selfName.empty()) out += " (self " + x.selfName + ")";
            out += " (body";
            renderChildren(out, x.body);
            out += "))";
            break;
        }
        case NodeKind::New: {
            const auto& x = as<New>(n);
            out += "(new ";
            renderType(out, *x.type);
            renderChildren(out, x.args);
            out += ')';
            break;
        }
        case NodeKind::Match: {
            const auto& x = as<Match>(n);
            out += "(match ";
            render(out, *x.scrutinee);
            for (const CaseDef& c : x.cases) {
                out += " (case ";
                renderPattern(out, *c.pattern);
                if (c.guard) {
                    out += " (if ";
                    render(out, *c.guard);
                    out += ')';
                }
                out += ' ';
                render(out, *c.body);
                out += ')';
            }
            out += ')';
            break;
        }
        case NodeKind::For: {
            const auto& x = as<For>(n);
            out += x.isYield ? "(for-yield" : "(for-do";
            for (const Enumerator& en : x.enums) {
                using EK = Enumerator::Kind;
                out += en.kind == EK::Generator ? " (<- " : en.kind == EK::Guard ? " (if " : " (= ";
                if (en.pattern) {
                    renderPattern(out, *en.pattern);
                    out += ' ';
                }
                render(out, *en.expr);
                out += ')';
            }
            out += ' ';
            render(out, *x.body);
            out += ')';
            break;
        }
        // Phase 4.
        case NodeKind::Try: {
            const auto& x = as<Try>(n);
            out += "(try ";
            render(out, *x.body);
            for (const CaseDef& c : x.cases) {
                out += " (case ";
                renderPattern(out, *c.pattern);
                if (c.guard) {
                    out += " (if ";
                    render(out, *c.guard);
                    out += ')';
                }
                out += ' ';
                render(out, *c.body);
                out += ')';
            }
            if (x.finallyBody) {
                out += " (finally ";
                render(out, *x.finallyBody);
                out += ')';
            }
            out += ')';
            break;
        }
        case NodeKind::Throw: {
            out += "(throw ";
            render(out, *as<Throw>(n).value);
            out += ')';
            break;
        }
        case NodeKind::Super: {
            const auto& x = as<Super>(n);
            out += x.qualifier.empty() ? "super" : "(super " + x.qualifier + ")";
            break;
        }
        case NodeKind::ExtensionDef: {
            const auto& x = as<ExtensionDef>(n);
            out += "(extension (" + x.receiverName + " ";
            renderType(out, *x.receiverType);
            out += ')';
            renderChildren(out, x.members);
            out += ')';
            break;
        }
    }
}

} // namespace

std::string dump(const Node& n) {
    std::string out;
    render(out, n);
    return out;
}

std::string dump(const TypeTree& t) {
    std::string out;
    renderType(out, t);
    return out;
}

// The variables a pattern binds, in source order (Alt contributes none: the
// compiler rejects variables in alternatives). Used by Desugar (pattern value
// definitions) and by the compiler (capture analysis, alternative checking).
void patternVariables(const Pattern& p, std::vector<std::string>& out) {
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

std::string dump(const Pattern& p) {
    std::string out;
    renderPattern(out, p);
    return out;
}

std::string dump(const CompilationUnit& u) {
    std::string out = "(unit";
    renderChildren(out, u.stats);
    out += ')';
    return out;
}

namespace {

// Moves every child subtree of `n` to `out`, leaving `n` childless.
// Visits every child *slot* of `n` (a NodePtr lvalue, possibly null) exactly
// once, without copying or moving anything. Both the tree teardown below and
// `rebase` walk the tree through this one table, so a node kind that grows a
// child cannot be forgotten by one of them and remembered by the other.
template <typename F>
void eachChildSlot(Node& n, F f) {
    auto takeParams = [&](std::vector<Param>& ps) { for (auto& p : ps) f(p.defaultValue); };
    switch (n.kind) {
        case NodeKind::Select: f(as<Select>(n).qualifier); return;
        case NodeKind::Apply: {
            auto& a = as<Apply>(n);
            f(a.fn);
            for (auto& arg : a.args) f(arg);
            return;
        }
        case NodeKind::TypeApply: f(as<TypeApply>(n).fn); return;
        case NodeKind::Infix: f(as<Infix>(n).lhs); f(as<Infix>(n).rhs); return;
        case NodeKind::Prefix: f(as<Prefix>(n).operand); return;
        case NodeKind::Assign: f(as<Assign>(n).target); f(as<Assign>(n).value); return;
        case NodeKind::If: {
            auto& i = as<If>(n);
            f(i.cond); f(i.thenp); f(i.elsep);
            return;
        }
        case NodeKind::While: f(as<While>(n).cond); f(as<While>(n).body); return;
        case NodeKind::Return: f(as<Return>(n).value); return;
        case NodeKind::Block: for (auto& st : as<Block>(n).stats) f(st); return;
        case NodeKind::Lambda: takeParams(as<Lambda>(n).params); f(as<Lambda>(n).body); return;
        case NodeKind::Typed: f(as<Typed>(n).expr); return;
        case NodeKind::Parens: f(as<Parens>(n).expr); return;
        case NodeKind::Tuple: for (auto& e : as<Tuple>(n).elems) f(e); return;
        case NodeKind::Splice: f(as<Splice>(n).expr); return;
        case NodeKind::NamedArg: f(as<NamedArg>(n).value); return;
        case NodeKind::ValDef: f(as<ValDef>(n).rhs); return;
        case NodeKind::DefDef: {
            auto& d = as<DefDef>(n);
            for (auto& list : d.paramLists) takeParams(list);
            f(d.body);
            return;
        }
        case NodeKind::TemplateDef: {
            auto& t = as<TemplateDef>(n);
            takeParams(t.ctorParams);
            for (auto& p : t.parents) for (auto& a : p.args) f(a);
            for (auto& s : t.body) f(s);
            for (auto& c : t.enumCases) {      // Phase 4: `enum` cases
                takeParams(c.params);
                for (auto& a : c.parentArgs) f(a);
            }
            return;
        }
        case NodeKind::New: for (auto& a : as<New>(n).args) f(a); return;
        case NodeKind::Match: {  // patterns hold only literals and paths: freed normally
            auto& m = as<Match>(n);
            f(m.scrutinee);
            for (auto& c : m.cases) { f(c.guard); f(c.body); }
            return;
        }
        case NodeKind::For: {
            auto& fo = as<For>(n);
            for (auto& en : fo.enums) f(en.expr);
            f(fo.body);
            return;
        }
        // Phase 4.
        case NodeKind::Try: {
            auto& t = as<Try>(n);
            f(t.body);
            for (auto& c : t.cases) { f(c.guard); f(c.body); }
            f(t.finallyBody);
            return;
        }
        case NodeKind::Throw: f(as<Throw>(n).value); return;
        case NodeKind::ExtensionDef:
            for (auto& m : as<ExtensionDef>(n).members) f(m);
            return;
        // Phase 3: an interpolation's holes are parsed sub-expressions.
        case NodeKind::InterpString: for (auto& a : as<InterpString>(n).args) f(a); return;
        default: return;  // leaves
    }
}

void releaseChildren(Node& n, std::vector<NodePtr>& out) {
    eachChildSlot(n, [&out](NodePtr& c) { if (c) out.push_back(std::move(c)); });
}

} // namespace

TypePtr cloneType(const TypeTree& t) {
    auto c = std::make_unique<TypeTree>();
    c->kind = t.kind;
    c->name = t.name;
    c->pos = t.pos;
    for (const TypePtr& a : t.args) c->args.push_back(cloneType(*a));
    return c;
}

NodePtr cloneSimpleExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::IntLit: {
            const auto& x = as<IntLit>(n);
            auto c = std::make_unique<IntLit>(x.pos);
            c->value = x.value;
            c->fitsLong = x.fitsLong;
            c->digits = x.digits;
            c->base = x.base;
            return c;
        }
        case NodeKind::FloatLit: {
            auto c = std::make_unique<FloatLit>(n.pos);
            c->value = as<FloatLit>(n).value;
            c->text = as<FloatLit>(n).text;
            return c;
        }
        case NodeKind::StringLit: {
            auto c = std::make_unique<StringLit>(n.pos);
            c->value = as<StringLit>(n).value;
            return c;
        }
        case NodeKind::CharLit: {
            auto c = std::make_unique<CharLit>(n.pos);
            c->value = as<CharLit>(n).value;
            return c;
        }
        case NodeKind::BoolLit: return std::make_unique<BoolLit>(n.pos, as<BoolLit>(n).value);
        case NodeKind::NullLit: return std::make_unique<NullLit>(n.pos);
        case NodeKind::UnitLit: return std::make_unique<UnitLit>(n.pos);
        case NodeKind::Ident: return std::make_unique<Ident>(n.pos, as<Ident>(n).name);
        case NodeKind::Select: {
            const auto& s = as<Select>(n);
            return std::make_unique<Select>(s.pos, cloneSimpleExpr(*s.qualifier), s.name);
        }
        default:
            throw std::logic_error("cloneSimpleExpr: not a literal or a path");
    }
}

PatternPtr clonePattern(const Pattern& p) {
    auto c = std::make_unique<Pattern>();
    c->kind = p.kind;
    c->pos = p.pos;
    c->name = p.name;
    if (p.expr) c->expr = cloneSimpleExpr(*p.expr);
    if (p.type) c->type = cloneType(*p.type);
    for (const PatternPtr& a : p.args) c->args.push_back(clonePattern(*a));
    return c;
}

void rebase(Node& n, SourcePos at) {
    // A hole's sub-expression was parsed from its own little source text, so its
    // line and column mean nothing in the enclosing file. Collapsing the whole
    // subtree onto the hole's position makes every diagnostic point at the
    // interpolated string instead of at column 1 of a text the reader never saw.
    n.pos = at;
    eachChildSlot(n, [at](NodePtr& c) { if (c) rebase(*c, at); });
}

void destroyTree(NodePtr root) {
    std::vector<NodePtr> work;
    if (root) work.push_back(std::move(root));
    while (!work.empty()) {
        NodePtr n = std::move(work.back());
        work.pop_back();
        releaseChildren(*n, work);
    }  // each node is freed childless
}

} // namespace protoScala
