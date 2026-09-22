/*
 * AST rendering — the S-expression form used by the unit tests.
 *
 * Format (one line, single spaces):
 *   IntLit            (int <value>)        — `digits` when the value does not fit a long
 *   FloatLit          (float <source text>) — with a leading '-' when negated
 *   StringLit         (str "<escaped>")     — \n \t \" \\ escaped
 *   CharLit           (char '<c>')
 *   BoolLit/Null/Unit true false / null / ()
 *   InterpString      (interp <interpolator>)
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
 *   ValDef            (val x [: T] <rhs>), (var x ...), (lazy-val x ...)
 *   DefDef            (def [@ann ...] name [[A B]] [(<list>...)] [: T] <body>),
 *                     each list `(<param>...)`, param `name:T`, `name:=> T`,
 *                     `name:T*`, with ` = <default>` appended
 *   Import            (import <text>)
 *   CompilationUnit   (unit <stat>...)
 *   TypeTree          Name, a.b.C, C[A, B], (A, B) => R, (A, B), => T, T*, ?, A | B
 */
#include "frontend/AST.h"

namespace protoScala {

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
        case NodeKind::InterpString:
            out += "(interp " + as<InterpString>(n).interpolator + ")";
            break;
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
            out += x.isLazy ? "(lazy-val " : x.isVar ? "(var " : "(val ";
            out += x.name;
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

std::string dump(const CompilationUnit& u) {
    std::string out = "(unit";
    renderChildren(out, u.stats);
    out += ')';
    return out;
}

} // namespace protoScala
