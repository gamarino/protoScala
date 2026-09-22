/*
 * Compiler members for pattern matching (DESIGN §5.3). `e match { cases }`
 * is a decision cascade: the scrutinee lives in a local slot; each pattern
 * tests and extracts from slots and binds its variables in the case's scope;
 * a failed test jumps to the next case; the guard runs after binding; no
 * matching case raises MatchError.
 *
 * Class membership is tested with TEST_PROTO, the per-class marker attribute
 * every instance's chain answers (Design note 5). protoCore's own
 * `isInstanceOf` will replace it once the fix on the platform branch lands:
 * see docs/platform/ISINSTANCEOF-FIX.md.
 */
#include "compiler/Compiler.h"
#include "runtime/StackGuard.h"

#include <unordered_map>

namespace protoScala {

void Compiler::compileMatch(const Match& m) {
    compileExpr(*m.scrutinee);
    const int scrutinee = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(scrutinee), m.pos, -1);
    std::vector<std::size_t> toEnd;
    for (const CaseDef& c : m.cases) {
        checkDistinctVariables(*c.pattern);
        fn_->scopes.emplace_back();
        std::vector<std::size_t> fail;
        compilePattern(*c.pattern, scrutinee, fail);
        if (c.guard) {
            compileExpr(*c.guard);
            fail.push_back(emitJump(Op::JUMP_IF_FALSE, c.pos, -1));
        }
        compileExpr(*c.body);
        toEnd.push_back(emitJump(Op::JUMP, c.pos, 0));
        adjust(-1);  // the next case starts without this case's value
        for (std::size_t f : fail) fn_->mod->patchJumpTo(f, fn_->mod->pos());
        fn_->scopes.pop_back();
    }
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(scrutinee), m.pos, +1);
    emit(Op::MATCH_ERROR, 0, m.pos, 0);  // throws; the pushed value stands for the result at the join
    for (std::size_t j : toEnd) fn_->mod->patchJumpTo(j, fn_->mod->pos());
}

void Compiler::bindPattern(const std::string& name, int slot, SourcePos pos) {
    if (name == "_") return;
    const LocalInfo info = declareLocal(name, BindingKind::Val, /*boxed=*/false);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(info.slot), pos, -1);
}

void Compiler::emitProtoTest(const std::string& typeKey, int slot, SourcePos pos,
                             std::vector<std::size_t>& fail) {
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::TEST_PROTO, fn_->mod->addSymbol(typeKey), pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, pos, -1));
}

// Reads the attributes `keys` of the value in `slot` into fresh slots and
// matches `subs` against them. One sub-pattern per key: every caller has
// already reported a mismatch as a user error (a constructor pattern of the
// wrong arity), so a mismatch here is a compiler bug.
void Compiler::extractInto(const std::vector<std::string>& keys, int slot,
                           const std::vector<PatternPtr>& subs, std::vector<std::size_t>& fail,
                           SourcePos pos) {
    if (subs.size() != keys.size())
        throw std::logic_error("compiler: extractInto with " + std::to_string(subs.size()) +
                               " sub-patterns for " + std::to_string(keys.size()) + " fields");
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::UNAPPLY_FIELDS, fn_->mod->addNames(keys), pos, static_cast<int>(keys.size()) - 1);
    std::vector<int> slots(keys.size());
    for (std::size_t k = keys.size(); k-- > 0;) {
        slots[k] = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(slots[k]), pos, -1);
    }
    for (std::size_t k = 0; k < subs.size(); ++k) compilePattern(*subs[k], slots[k], fail);
}

void Compiler::checkNoVariables(const Pattern& p) const {
    std::vector<std::string> vars;
    patternVariables(p, vars);
    if (!vars.empty())
        throw CompileError("Illegal variable " + vars.front() + " in pattern alternative", p.pos);
}

// One pattern binds each name once (scalac 3.9.0: "duplicate pattern
// variable: x"). Without the check the second binding would shadow the first
// in the case's scope and the pattern would match any pair, not equal ones.
void Compiler::checkDistinctVariables(const Pattern& p) const {
    std::vector<std::string> vars;
    patternVariables(p, vars);
    std::unordered_set<std::string> seen;
    for (const std::string& v : vars)
        if (!seen.insert(v).second)
            throw CompileError("duplicate pattern variable: " + v, p.pos);
}

void Compiler::compilePattern(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    checkNativeStack(StackUse::Source);
    using K = Pattern::Kind;
    switch (p.kind) {
        case K::Wildcard:
            return;
        case K::Var:
            bindPattern(p.name, slot, p.pos);
            return;
        case K::Literal:
        case K::Stable:  // the pattern's value == the scrutinee (SLS 8.1.4-8.1.5)
            compileExpr(*p.expr);
            emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
            emit(Op::EQ, 0, p.pos, -1);
            fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
            return;
        case K::Typed:
            if (compileTypeTest(*p.type, slot, p.pos, /*inPattern=*/true))
                fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
            compilePattern(*p.args[0], slot, fail);
            return;
        case K::Bind:
            compilePattern(*p.args[0], slot, fail);
            bindPattern(p.name, slot, p.pos);
            return;
        case K::Alt: {
            std::vector<std::size_t> matched;
            for (std::size_t k = 0; k < p.args.size(); ++k) {
                checkNoVariables(*p.args[k]);
                if (k + 1 == p.args.size()) {
                    compilePattern(*p.args[k], slot, fail);
                    break;
                }
                std::vector<std::size_t> next;
                compilePattern(*p.args[k], slot, next);
                matched.push_back(emitJump(Op::JUMP, p.pos, 0));
                for (std::size_t f : next) fn_->mod->patchJumpTo(f, fn_->mod->pos());
            }
            for (std::size_t j : matched) fn_->mod->patchJumpTo(j, fn_->mod->pos());
            return;
        }
        case K::Tuple: {
            const std::size_t n = p.args.size();
            if (n > kMaxTupleArity)
                throw CompileError("tuple patterns of more than 22 elements are not supported (D32)",
                                   p.pos);
            emitProtoTest(tupleTypeKey(static_cast<unsigned>(n)), slot, p.pos, fail);
            std::vector<std::string> keys;
            for (std::size_t k = 1; k <= n; ++k) keys.push_back("_" + std::to_string(k));
            extractInto(keys, slot, p.args, fail, p.pos);
            return;
        }
        case K::Extractor:
            compileExtractor(p, slot, fail);
            return;
        case K::SeqWildcard:
            throw CompileError("a sequence wildcard is only allowed as the last argument of a sequence "
                               "pattern such as List(a, rest*)", p.pos);
    }
}

void Compiler::compileExtractor(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    const std::string& name = p.name;
    bool shadowed = false;  // a local or member with the extractor's name
    if (p.expr->kind == NodeKind::Ident) {
        bool found = false;
        captureInto(fn_, name, p.pos, &found);
        shadowed = found || memberOf(name) != nullptr;
    }
    const GlobalBinding* term = globals_.binding(name);
    // A built-in extractor keeps its fast path only while nothing shadows it:
    // no local or member of that name (`shadowed`) and no user global. `List`
    // is one of the runtime's own globals; `::` is a method of the list
    // prototype and has no global at all, so any global named `::` is a user
    // definition and takes over.
    const bool userDefined = shadowed || (term && term->kind != BindingKind::Builtin);
    // h :: t on a List: getAt(0) and the tail slice (DESIGN §5.3, §6).
    if (name == "::" && !userDefined) {
        if (p.args.size() != 2) throw CompileError("the :: pattern takes two patterns", p.pos);
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::ConsList), p.pos, 0);
        fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::UNCONS, 0, p.pos, +1);
        const int tail = newSlot();
        const int head = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(tail), p.pos, -1);
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(head), p.pos, -1);
        compilePattern(*p.args[0], head, fail);
        compilePattern(*p.args[1], tail, fail);
        return;
    }
    // List(p1, ..., pn[, rest*]) on a List.
    if (name == "List" && !userDefined && term) {
        compileListPattern(p, slot, fail);
        return;
    }
    // A case class whose companion keeps the synthesised unapply (or a tuple
    // class): its fields, read by key (DESIGN §5.3).
    const ClassInfo* cls = shadowed ? nullptr : globals_.findType(name);
    const bool synthesised = cls && cls->isCase && cls->kind == ClassKind::Class &&
                             ((cls->builtin && cls->companionTermKey.empty()) ||
                              (!cls->companionHasUnapply && term && term->key == cls->companionTermKey));
    if (synthesised) {
        if (p.args.size() != cls->fields.size())
            throw CompileError("wrong number of arguments for pattern " + name + ": expected " +
                                   std::to_string(cls->fields.size()) + ", found " +
                                   std::to_string(p.args.size()), p.pos);
        emitProtoTest(cls->key, slot, p.pos, fail);
        extractInto(cls->fields, slot, p.args, fail, p.pos);
        return;
    }
    if (!shadowed && !term && p.expr->kind == NodeKind::Ident)
        throw CompileError("Not found: " + name, p.pos);
    // A custom extractor (Scala 3): X.unapply(v) is a Boolean for X(), else a
    // value answering isEmpty/get whose get is the single result or a tuple.
    // The parameter type of unapply is not checked (types are erased, D29).
    compileExpr(*p.expr);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("unapply", 1), p.pos, -1);
    const int result = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(result), p.pos, -1);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(result), p.pos, +1);
    if (p.args.empty()) {
        fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
        return;
    }
    emit(Op::SEND, fn_->mod->addSendSite("isEmpty", 0), p.pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_TRUE, p.pos, -1));
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(result), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("get", 0), p.pos, 0);
    const int got = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(got), p.pos, -1);
    if (p.args.size() == 1) {
        compilePattern(*p.args[0], got, fail);
        return;
    }
    std::vector<std::string> keys;
    for (std::size_t k = 1; k <= p.args.size(); ++k) keys.push_back("_" + std::to_string(k));
    extractInto(keys, got, p.args, fail, p.pos);
}

// List(p1, ..., pn) / List(p1, ..., rest*): a size test, the elements by
// index, the rest by drop (DESIGN §5.3 sequence patterns).
void Compiler::compileListPattern(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    const bool seq = !p.args.empty() && p.args.back()->kind == Pattern::Kind::SeqWildcard;
    const std::size_t fixed = p.args.size() - (seq ? 1 : 0);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::List), p.pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("length", 0), p.pos, 0);
    emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(fixed)), p.pos, +1);
    emit(seq ? Op::GE : Op::EQ, 0, p.pos, -1);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
    for (std::size_t k = 0; k < fixed; ++k) {
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(k)), p.pos, +1);
        emit(Op::SEND, fn_->mod->addSendSite("apply", 1), p.pos, -1);
        const int element = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(element), p.pos, -1);
        compilePattern(*p.args[k], element, fail);
    }
    // `List(a, rest*)` binds the tail; `List(a, _*)` (no binder name) drops it.
    // bindPattern applies the `_` rule, so no local is ever named `_`.
    if (seq && !p.args.back()->name.empty()) {
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(fixed)), p.pos, +1);
        emit(Op::SEND, fn_->mod->addSendSite("drop", 1), p.pos, -1);
        const int rest = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(rest), p.pos, -1);
        bindPattern(p.args.back()->name, rest, p.pos);
    }
}

bool Compiler::compileTypeTest(const TypeTree& t, int slot, SourcePos pos, bool inPattern) {
    std::string name;
    switch (t.kind) {
        case TypeTree::Kind::Name: case TypeTree::Kind::Applied: name = t.name; break;
        case TypeTree::Kind::Tuple: name = "Tuple" + std::to_string(t.args.size()); break;
        case TypeTree::Kind::Function: name = "Function"; break;
        default: throw CompileError("this type cannot be tested at run time", pos);
    }
    for (const char* prefix : {"scala.", "java.lang.", "Predef."})
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
    static const std::unordered_map<std::string, TypeCode> builtin = {
        {"Int", TypeCode::Integer}, {"Long", TypeCode::Integer}, {"Short", TypeCode::Integer},
        {"Byte", TypeCode::Integer}, {"BigInt", TypeCode::Integer}, {"Integer", TypeCode::Integer},
        {"Double", TypeCode::Double}, {"Float", TypeCode::Double}, {"Boolean", TypeCode::Boolean},
        {"Char", TypeCode::Char}, {"String", TypeCode::String}, {"Unit", TypeCode::Unit},
        {"List", TypeCode::List}, {"Function", TypeCode::Function}, {"AnyRef", TypeCode::AnyRef},
        {"Object", TypeCode::AnyRef}, {"AnyVal", TypeCode::AnyVal}, {"Null", TypeCode::Null},
        {"Nothing", TypeCode::Nothing}};
    if (name == "Any") {
        if (inPattern) return false;  // `case x: Any` matches everything
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::NonNull), pos, 0);
        return true;
    }
    auto code = builtin.find(name);
    if (code == builtin.end() && name.rfind("Function", 0) == 0 && name.size() > 8 &&
        name.find_first_not_of("0123456789", 8) == std::string::npos)
        code = builtin.find("Function");  // Function0..Function22
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    if (code != builtin.end()) {
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(code->second), pos, 0);
        return true;
    }
    const ClassInfo* c = globals_.findType(name);
    if (!c) throw CompileError("Not found: type " + name, pos);
    emit(Op::TEST_PROTO, fn_->mod->addSymbol(c->key), pos, 0);
    return true;
}

// x.isInstanceOf[T] / x.asInstanceOf[T] (LANGUAGE §3): only the class of T is
// checked (erasure); null is an instance of nothing and casts to anything (D29).
void Compiler::compileInstanceOf(const Node& value, const TypeTree& t, bool cast, SourcePos pos) {
    compileExpr(value);
    const int slot = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(slot), pos, -1);
    if (!cast) {
        compileTypeTest(t, slot, pos, /*inPattern=*/false);
        return;
    }
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::Null), pos, 0);
    const std::size_t isNull = emitJump(Op::JUMP_IF_TRUE, pos, -1);
    compileTypeTest(t, slot, pos, /*inPattern=*/false);
    const std::size_t ok = emitJump(Op::JUMP_IF_TRUE, pos, -1);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::CAST_FAIL, fn_->mod->addString(dump(t)), pos, 0);
    adjust(-1);  // CAST_FAIL never returns
    fn_->mod->patchJumpTo(isNull, fn_->mod->pos());
    fn_->mod->patchJumpTo(ok, fn_->mod->pos());
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
}

} // namespace protoScala
