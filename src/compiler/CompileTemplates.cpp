/*
 * Compiler members for classes, traits and objects (Phase 2 plan, Task 7):
 * class descriptions (ClassInfo), MAKE_CLASS, methods, constructors, setters,
 * singleton holders, `new`, `super`, named-argument sends.
 */
#include "compiler/Compiler.h"
#include "frontend/Linearizer.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace protoScala {

namespace {

std::string typeNameOf(const TypeTree& t) {
    if (t.kind == TypeTree::Kind::Name || t.kind == TypeTree::Kind::Applied) return t.name;
    if (t.kind == TypeTree::Kind::Tuple) return "Tuple" + std::to_string(t.args.size());
    return "";
}

const char* kindWord(ClassKind k) {
    return k == ClassKind::Trait ? "trait" : k == ClassKind::Object ? "object" : "class";
}

const std::vector<Param>& paramsOfDef(const DefDef& d) {
    static const std::vector<Param> none;
    return d.paramLists.empty() ? none : d.paramLists[0];
}

} // namespace

const MemberInfo* Compiler::memberOf(const std::string& name) const {
    if (!tmpl_) return nullptr;
    auto it = tmpl_->info->members.find(name);
    return it == tmpl_->info->members.end() ? nullptr : &it->second;
}

// `e.name` inside a template or its companion: a private member of either is
// reached through its class-qualified key (D5, Design note 8).
std::string Compiler::selectKey(const std::string& name) const {
    if (!tmpl_) return name;
    for (const ClassInfo* c : {tmpl_->info, tmpl_->companion}) {
        if (!c) continue;
        auto it = c->members.find(name);
        if (it != c->members.end() && it->second.key != name) return it->second.key;
    }
    return name;
}

std::size_t Compiler::sendSite(const std::string& name, std::uint32_t argc) {
    const std::string key = selectKey(name);
    return fn_->mod->addSendSite(key, argc, key == name ? std::string() : name);
}

std::size_t Compiler::kwSendSite(const std::string& name, std::uint32_t positional,
                                 const std::vector<std::string>& keywords) {
    const std::string key = selectKey(name);
    return fn_->mod->addKwSendSite(key, positional, keywords,
                                   key == name ? std::string() : name);
}

void Compiler::loadThis(SourcePos pos) {
    bool found = false;
    const LocalInfo info = captureInto(fn_, "this", pos, &found);
    if (!found) throw CompileError("this can be used only inside a class, trait or object", pos);
    loadLocal(info, pos);
}

const ClassInfo& Compiler::resolveType(const TypeTree& t, SourcePos pos) const {
    std::string name = typeNameOf(t);
    if (name.empty()) throw CompileError("a class or trait name is expected here", pos);
    for (const char* prefix : {"scala.", "java.lang."})
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
    // A template lifted out of an `object` has a dotted name, and Scala's scoping
    // sees a sibling without the qualifier, so the enclosing prefixes are tried
    // innermost first.
    for (const std::string& candidate : scopedNames(name))
        if (const ClassInfo* c = globals_.findType(candidate)) return *c;
    throw CompileError("Not found: type " + name, pos);
}

// Parents first; a template of this unit may extend another one defined below it.
std::vector<const TemplateDef*> Compiler::sortTemplates(
    const std::vector<const TemplateDef*>& ts) const {
    std::unordered_map<std::string, const TemplateDef*> byName;
    for (const TemplateDef* t : ts)
        if (t->kind != TemplateKind::Object) byName[t->name] = t;
    std::vector<const TemplateDef*> out;
    std::unordered_map<const TemplateDef*, int> state;  // 1: visiting, 2: done
    std::function<void(const TemplateDef*)> visit = [&](const TemplateDef* t) {
        if (state[t] == 2) return;
        if (state[t] == 1)
            throw CompileError("cyclic inheritance: " + t->name + " extends itself", t->pos);
        state[t] = 1;
        for (const ParentRef& p : t->parents) {
            auto it = byName.find(typeNameOf(*p.type));
            if (it != byName.end()) visit(it->second);
        }
        state[t] = 2;
        out.push_back(t);
    };
    for (const TemplateDef* t : ts) visit(t);
    return out;
}

const ClassInfo* Compiler::superclassOf(const ClassInfo& info) const {
    for (std::size_t k = 1; k < info.linearization.size(); ++k) {
        const ClassInfo* c = globals_.findTypeByKey(info.linearization[k]);
        if (c && c->kind == ClassKind::Class && !c->builtin) return c;
    }
    return nullptr;
}

ClassInfo Compiler::buildClassInfo(const TemplateDef& t, const std::string& typeKey) const {
    ClassInfo c;
    c.name = t.name;
    c.key = typeKey;
    c.kind = t.kind == TemplateKind::Trait    ? ClassKind::Trait
             : t.kind == TemplateKind::Object ? ClassKind::Object
                                              : ClassKind::Class;
    c.isCase = t.isCase;
    c.isAbstract = t.mods.isAbstract || c.kind == ClassKind::Trait;
    c.isFinal = t.mods.isFinal || c.kind == ClassKind::Object;
    c.isSealed = t.mods.isSealed;

    // Parents.
    std::vector<const ClassInfo*> parents;
    for (std::size_t k = 0; k < t.parents.size(); ++k) {
        const ParentRef& p = t.parents[k];
        const ClassInfo& pi = resolveType(*p.type, p.pos);
        if (pi.kind == ClassKind::Object)
            throw CompileError("an object cannot be extended: " + pi.name, p.pos);
        if (pi.isFinal) throw CompileError("cannot extend final class " + pi.name, p.pos);
        if (k > 0 && pi.kind != ClassKind::Trait)
            throw CompileError("class " + pi.name +
                                   " is not a trait; only the first parent may be a class",
                               p.pos);
        if (c.isCase && pi.isCase && pi.kind == ClassKind::Class)
            throw CompileError("case-to-case inheritance is prohibited: case class " + t.name +
                                   " extends case class " + pi.name,
                               p.pos);
        if (p.hasArgs && c.kind == ClassKind::Trait)
            throw CompileError("a trait may not pass arguments to its parents", p.pos);
        if (p.hasArgs && pi.kind == ClassKind::Trait && pi.primaryArity == 0)
            throw CompileError("trait " + pi.name + " takes no arguments", p.pos);
        parents.push_back(&pi);
    }
    std::vector<std::vector<std::string>> lins;
    for (const ClassInfo* p : parents) lins.push_back(p->linearization);
    if (lins.empty()) lins.push_back({kAnyRefKey, kAnyKey});
    c.linearization = linearize(c.key, lins);

    // SLS 5.1.2: the classes among the base classes must form a single chain, so
    // the template has one superclass. That superclass is the first parent when
    // it is a class, and otherwise the most derived class the traits bring in
    // (dotty's ensureFirstIsClass); every other inherited class must be one of
    // its ancestors. `class C extends U with T` with `trait T extends A` is
    // legal and gets A as its superclass; `class C extends B with T` is not,
    // because B does not derive from A.
    if (!parents.empty()) {
        const ClassInfo* base =
            parents[0]->kind == ClassKind::Class ? parents[0] : superclassOf(c);
        for (const std::string& k : c.linearization) {
            if (k == c.key) continue;
            const ClassInfo* a = globals_.findTypeByKey(k);
            if (!a || a->kind != ClassKind::Class || a->builtin) continue;
            if (base && std::find(base->linearization.begin(), base->linearization.end(), k) !=
                            base->linearization.end())
                continue;
            // Name the parent that brings the unrelated class in.
            const ClassInfo* owner = nullptr;
            SourcePos where = t.pos;
            for (std::size_t j = 0; j < parents.size(); ++j)
                if (owner == nullptr &&
                    std::find(parents[j]->linearization.begin(), parents[j]->linearization.end(),
                              k) != parents[j]->linearization.end()) {
                    owner = parents[j];
                    where = t.parents[j].pos;
                }
            throw CompileError(
                std::string("illegal trait inheritance: superclass ") +
                    (base ? base->name : std::string("AnyRef")) + " does not derive from " +
                    (owner ? std::string(kindWord(owner->kind)) + " " + owner->name + "'s " : "") +
                    "superclass " + a->name,
                where);
        }
    }

    // Inherited public members, the more specific ancestors last; a member is
    // concrete when any ancestor defines it.
    for (auto it = c.linearization.rbegin(); it != c.linearization.rend(); ++it) {
        if (*it == c.key) continue;
        const ClassInfo* a = globals_.findTypeByKey(*it);
        if (!a) continue;
        c.mutableInstances = c.mutableInstances || a->mutableInstances;
        for (const auto& [name, m] : a->members) {
            if (m.key != name) continue;  // an ancestor's private member is not inherited
            MemberInfo merged = m;
            auto found = c.members.find(name);
            if (found != c.members.end() && found->second.concrete) merged.concrete = true;
            c.members[name] = merged;
        }
    }

    // Own members.
    std::unordered_set<std::string> own;
    // A plain constructor parameter is not a member: Scala makes it a
    // private[this] local, so it may shadow an inherited public member without
    // `override` and without weakening its access (scalac accepts
    // `class T(v: Int) extends Node(v)` over `class Node(val v: Int)`).
    // `declaredPrivate` is "written private"; `declaredMember` is "declares a
    // member of the template". Both checks below apply only to real members.
    auto addOwn = [&](const std::string& name, MemberKind kind, bool isPublic, bool concrete,
                      bool declaredPrivate, bool isOverride, bool declaredMember,
                      SourcePos pos) {
        if (!own.insert(name).second)
            throw CompileError(name + " is already defined in " + t.name, pos);
        auto inherited = c.members.find(name);
        const bool inheritedConcrete =
            inherited != c.members.end() && inherited->second.concrete && inherited->second.key == name;
        // scalac: "private X cannot override X in ...": a private member may not
        // take the place of an inherited public one (weaker access privileges).
        if (declaredPrivate && inherited != c.members.end() && inherited->second.key == name)
            throw CompileError("private " + name + " cannot override " + name +
                                   " inherited by " + t.name +
                                   ": it has weaker access privileges",
                               pos);
        const MemberKind inheritedKind =
            inherited != c.members.end() ? inherited->second.kind : MemberKind::Def;
        const bool redefines = inherited != c.members.end() && inherited->second.key == name;
        const bool inheritedStable = inheritedKind == MemberKind::Val ||
                                     inheritedKind == MemberKind::Var ||
                                     inheritedKind == MemberKind::LazyVal;
        const bool ownIsDef = kind == MemberKind::Def || kind == MemberKind::ParamlessDef;
        if (declaredMember && redefines) {
            // The rules below follow scalac 3.9.0, checked case by case:
            //  - only a stable member may take the place of a val, a var or a
            //    lazy val ("needs to be a stable, immutable value");
            //  - a var may implement an abstract var, but never override
            //    another var, and `override` is never written on one;
            //  - `override` is required for a member an ancestor implements and
            //    optional for an abstract one.
            if ((ownIsDef || kind == MemberKind::Var) && inheritedStable &&
                !(kind == MemberKind::Var && inheritedKind == MemberKind::Var))
                throw CompileError(std::string(ownIsDef ? "method " : "variable ") + name +
                                       " overriding " + name + " inherited by " + t.name +
                                       " needs to be a stable, immutable value",
                                   pos);
            if (kind == MemberKind::Var && (isOverride || inheritedConcrete))
                throw CompileError("variable " + name + " inherited by " + t.name +
                                       " cannot override a mutable variable",
                                   pos);
            if (!isOverride && inheritedConcrete)
                throw CompileError("error overriding " + name + " inherited by " + t.name + ": " +
                                       name + " needs an `override` modifier",
                                   pos);
        }
        c.members[name] =
            MemberInfo{kind, isPublic ? name : privateKey(c.key, name), concrete || inheritedConcrete, {}};
    };
    bool hasStatements = !t.ctorParams.empty();
    bool ownVar = false;
    for (const Param& p : t.ctorParams) {
        if (p.defaultValue && p.repeated)
            throw CompileError("a default parameter value is not supported on a repeated "
                               "constructor parameter",
                               p.pos);
        // scalac: "`val` parameters may not be call-by-name" — a member has to
        // hold a value, not a thunk (D47).
        if (p.byName && (p.isVal || p.isVar || c.isCase))
            throw CompileError("a val, var or case-class parameter may not be by-name", p.pos);
        // Plain parameters are private fields (reachable from the methods).
        const bool isPublic = (p.isVal || p.isVar || c.isCase) && !p.mods.isPrivate;
        const bool paramIsMember = p.isVal || p.isVar || c.isCase;
        addOwn(p.name, p.isVar ? MemberKind::Var : MemberKind::Val, isPublic, true,
               p.mods.isPrivate, p.mods.isOverride, paramIsMember, p.pos);
        if (p.isVar) {
            addOwn(setterName(p.name), MemberKind::Def, isPublic, /*concrete=*/true,
                   p.mods.isPrivate, p.mods.isOverride, /*declaredMember=*/false, p.pos);
            ownVar = true;
        }
        if (p.byName) c.members.at(p.name).byNameValue = true;
        c.ctorParams.push_back(p.name);
        if (c.isCase) c.fields.push_back(c.members.at(p.name).key);
    }
    c.primaryArity = t.ctorParams.size();
    // Trailing parameters with a default may be omitted; the callee prologue
    // fills them (Phase 4). A default in the middle is legal too, but then every
    // later parameter has to be given by name, which the prologue's
    // "is missing argument 'x'" reports.
    c.primaryMinArity = c.primaryArity;
    while (c.primaryMinArity > 0 && t.ctorParams[c.primaryMinArity - 1].defaultValue)
        --c.primaryMinArity;
    c.primaryVariadic = !t.ctorParams.empty() && t.ctorParams.back().repeated;
    // By-name primary-constructor parameters (D47): `C(e)` and `new C(e)` name
    // the class, so the call site can wrap `e` in a thunk.
    if (const std::uint32_t ctorMask = byNameMaskOfParams(t.ctorParams)) {
        c.primaryByNameMasks.assign(1, ctorMask);
        globals_.noteByNameSelector(t.name, c.primaryByNameMasks);
    }
    if (c.isCase && c.kind == ClassKind::Class)
        for (std::size_t k = 1; k <= c.fields.size(); ++k)
            c.members["_" + std::to_string(k)] =
                MemberInfo{MemberKind::ParamlessDef, "_" + std::to_string(k), true, {}};
    for (const NodePtr& s : t.body) {
        switch (s->kind) {
            case NodeKind::ValDef: {
                const auto& v = as<ValDef>(*s);
                addOwn(v.name,
                       v.isLazy  ? MemberKind::LazyVal
                       : v.isVar ? MemberKind::Var
                                 : MemberKind::Val,
                       !v.mods.isPrivate, v.rhs != nullptr, v.mods.isPrivate, v.mods.isOverride,
                       true, v.pos);
                if (v.isVar) {
                    // An abstract `var v: Int` declares an abstract setter too.
                    addOwn(setterName(v.name), MemberKind::Def, !v.mods.isPrivate,
                           /*concrete=*/v.rhs != nullptr, v.mods.isPrivate, v.mods.isOverride,
                           /*declaredMember=*/false, v.pos);
                    ownVar = true;
                }
                if (v.rhs) hasStatements = true;
                break;
            }
            case NodeKind::DefDef: {
                const auto& d = as<DefDef>(*s);
                if (d.isMain())
                    throw CompileError("@main methods must be top-level definitions", d.pos);
                if (d.name == "this") {
                    if (c.kind != ClassKind::Class)
                        throw CompileError("auxiliary constructors are only allowed in classes",
                                           d.pos);
                    const std::size_t arity = paramsOfDef(d).size();
                    if (arity == c.primaryArity ||
                        std::find(c.auxArities.begin(), c.auxArities.end(), arity) !=
                            c.auxArities.end())
                        throw CompileError("constructors of " + t.name +
                                               " must differ in their number of parameters (D31)",
                                           d.pos);
                    c.auxArities.push_back(arity);
                    break;
                }
                addOwn(d.name, d.paramLists.empty() ? MemberKind::ParamlessDef : MemberKind::Def,
                       !d.mods.isPrivate, d.body != nullptr, d.mods.isPrivate, d.mods.isOverride,
                       true, d.pos);
                // By-name parameters of a method (D47): honoured at a call site
                // that resolves to this declaration — `this.m(e)`, `m(e)` inside
                // the template, `O.m(e)` on an object — and evaluated at a
                // dynamic send (D53).
                if (std::vector<std::uint32_t> masks = byNameMasksOfDef(d); !masks.empty()) {
                    globals_.noteByNameSelector(d.name, masks);
                    c.members.at(d.name).byNameMasks = std::move(masks);
                }
                break;
            }
            case NodeKind::TemplateDef:
                // Desugar lifts a template nested in an `object` to the top
                // level, so one reaching here was nested in a class or a trait.
                throw CompileError("classes, traits and objects must be defined at the top level "
                                   "of a file or in an object",
                                   s->pos);
            case NodeKind::Import:
                break;
            default:
                hasStatements = true;  // an expression statement runs in the constructor
                break;
        }
    }
    c.mutableInstances = c.mutableInstances || ownVar;
    c.hasInit = c.kind != ClassKind::Trait || hasStatements;

    // Every member of a concrete class must be defined.
    if (!c.isAbstract) {
        std::vector<std::string> missing;
        for (const auto& [name, m] : c.members)
            if (!m.concrete) missing.push_back(name);
        if (!missing.empty()) {
            std::sort(missing.begin(), missing.end());
            const MemberInfo& m = c.members.at(missing.front());
            const char* what =
                m.kind == MemberKind::Def || m.kind == MemberKind::ParamlessDef ? "def" : "val";
            throw CompileError(std::string(kindWord(c.kind)) + " " + t.name +
                                   " needs to be abstract, since " + what + " " + missing.front() +
                                   " is not defined",
                               t.pos);
        }
    }
    return c;
}

// A class and an object of the same name in one unit are companions.
void Compiler::linkCompanions(const std::vector<const TemplateDef*>& ts) {
    for (const TemplateDef* o : ts) {
        if (o->kind != TemplateKind::Object) continue;
        for (const TemplateDef* k : ts) {
            if (k->kind == TemplateKind::Object || k->name != o->name) continue;
            ClassInfo* cls = globals_.mutableTypeByKey(globals_.findType(k->name)->key);
            ClassInfo* obj = globals_.mutableTypeByKey(globals_.findType(o->name + ".type")->key);
            cls->companionTermKey = globals_.binding(o->name)->key;
            cls->companionTypeKey = obj->key;
            obj->companionTypeKey = cls->key;
            for (const NodePtr& s : o->body) {
                if (s->kind != NodeKind::DefDef || as<DefDef>(*s).synthetic) continue;
                if (as<DefDef>(*s).name == "apply") cls->companionHasApply = true;
                if (as<DefDef>(*s).name == "unapply") cls->companionHasUnapply = true;
            }
        }
    }
}

// The prototype chain MAKE_CLASS installs: the linearization without the class
// itself. Product's natives stand in for the synthesised case-class members;
// they sit right before AnyRef, after every user parent, so a toString a case
// class inherits from a user class wins, as in Scala.
std::vector<std::string> Compiler::runtimeChain(const ClassInfo& info) const {
    std::vector<std::string> chain(info.linearization.begin() + 1, info.linearization.end());
    const bool product =
        std::any_of(info.linearization.begin(), info.linearization.end(),
                    [&](const std::string& k) {
                        const ClassInfo* c = globals_.findTypeByKey(k);
                        return k == info.key ? info.isCase : c && c->isCase;
                    });
    if (product && std::find(chain.begin(), chain.end(), kProductKey) == chain.end())
        chain.insert(std::find(chain.begin(), chain.end(), std::string(kAnyRefKey)), kProductKey);
    return chain;
}

std::string Compiler::ctorKeyFor(const ClassInfo& info, std::size_t argc, SourcePos pos) const {
    if (argc == info.primaryArity || (info.primaryVariadic && argc + 1 >= info.primaryArity) ||
        (argc >= info.primaryMinArity && argc < info.primaryArity))
        return kPrimaryCtorKey;
    if (std::find(info.auxArities.begin(), info.auxArities.end(), argc) != info.auxArities.end())
        return auxCtorKey(argc);
    throw CompileError("wrong number of arguments for the constructor of " + info.name + ": " +
                           std::to_string(argc),
                       pos);
}

void Compiler::compileTemplate(const TemplateDef& t, const ClassInfo& info) {
    const ClassInfo* companion =
        info.companionTypeKey.empty() ? nullptr : globals_.findTypeByKey(info.companionTypeKey);
    const TemplateScope scope{&info, companion, t.selfName};
    const TemplateScope* saved = tmpl_;
    tmpl_ = &scope;
    const std::vector<std::string> chain = runtimeChain(info);
    for (const std::string& k : chain) emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(k), t.pos, +1);
    std::vector<std::string> keys;
    for (const NodePtr& s : t.body) {  // methods
        if (s->kind != NodeKind::DefDef) continue;
        const auto& d = as<DefDef>(*s);
        if (d.name == "this" || !d.body) continue;
        keys.push_back(info.members.at(d.name).key);
        compileFunction(d.name, paramsOfDef(d), *d.body, FnShape::Method, d.pos,
                        /*paramless=*/d.paramLists.empty(), /*allowByName=*/true);
    }
    auto setterFor = [&](const std::string& name, SourcePos pos) {
        keys.push_back(info.members.at(setterName(name)).key);
        compileSetter(info.members.at(name).key, name, pos);
    };
    for (const Param& p : t.ctorParams)
        if (p.isVar) setterFor(p.name, p.pos);
    for (const NodePtr& s : t.body)
        if (s->kind == NodeKind::ValDef && as<ValDef>(*s).isVar) setterFor(as<ValDef>(*s).name, s->pos);
    if (info.hasInit) {
        keys.push_back(kPrimaryCtorKey);
        compileConstructor(t, info);
    }
    for (const NodePtr& s : t.body) {
        if (s->kind != NodeKind::DefDef || as<DefDef>(*s).name != "this") continue;
        keys.push_back(auxCtorKey(paramsOfDef(as<DefDef>(*s)).size()));
        compileAuxConstructor(as<DefDef>(*s), info);
    }
    BytecodeModule::ClassSpecData spec;
    // The SIMPLE name: `object O { case class C }` prints `C(1)`, not `O.C(1)`,
    // which is what scalac prints too.
    spec.displayName = simpleName(info.name);
    spec.key = info.key;
    spec.parentCount = static_cast<std::uint32_t>(chain.size());
    spec.memberKeys = keys;
    spec.fields = info.fields;
    spec.flags = (info.isCase ? BytecodeModule::kClassCase : 0u) |
                 (info.isCase && info.kind == ClassKind::Object ? BytecodeModule::kClassCaseObject
                                                                : 0u) |
                 (info.mutableInstances ? BytecodeModule::kClassMutableInstances : 0u) |
                 (info.kind == ClassKind::Trait ? BytecodeModule::kClassTrait : 0u) |
                 (info.kind == ClassKind::Object ? BytecodeModule::kClassObject : 0u);
    emit(Op::MAKE_CLASS, fn_->mod->addClassSpec(spec), t.pos,
         1 - static_cast<int>(chain.size() + keys.size()));
    emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(info.key), t.pos, -1);
    tmpl_ = saved;
}

// Emits `target.<init>(this, args)` and stores the new `this` (Design note 10).
void Compiler::compileInitCall(const ClassInfo& target, const std::vector<NodePtr>& args,
                               SourcePos pos) {
    emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(target.key), pos, +1);
    emit(Op::PUSH_LOCAL, 0, pos, +1);
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Splice || arg->kind == NodeKind::NamedArg)
            throw CompileError("constructor arguments must be plain expressions here", arg->pos);
        compileExpr(*arg);
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    const std::string key =
        target.kind == ClassKind::Trait ? kPrimaryCtorKey : ctorKeyFor(target, n, pos);
    emit(Op::INVOKE_INIT, fn_->mod->addSendSite(key, n), pos, -static_cast<int>(n) - 1);
    emit(Op::STORE_LOCAL, 0, pos, -1);
}

// <init>(this, params): superclass initialiser, trait initialisers in Scala's
// order, parameter fields, lazy holders, then the template statements; returns
// the final `this`.
void Compiler::compileConstructor(const TemplateDef& t, const ClassInfo& info) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(info.name + ".<init>");
    mod->setMethod(true);
    FunctionState fs;
    fs.mod = mod.get();
    fs.allowsReturn = false;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    const LocalInfo self{newSlot(), BindingKind::Param, false, false, {}};
    fs.scopes.back()["this"] = self;
    if (!t.selfName.empty()) fs.scopes.back()[t.selfName] = self;
    for (const Param& p : t.ctorParams)
        fs.scopes.back()[p.name] =
            LocalInfo{newSlot(), p.byName ? BindingKind::ByNameParam : BindingKind::Param, false,
                      false, {}};
    const int arity = 1 + static_cast<int>(t.ctorParams.size());
    mod->setArity(arity);
    mod->setVariadic(info.primaryVariadic);
    static const std::vector<NodePtr> noArgs;
    for (const ParentRef& p : t.parents)
        for (const auto& arg : p.args) analyseCaptures(t.ctorParams, *arg);
    for (const NodePtr& s : t.body) {
        if (s->kind == NodeKind::ValDef && as<ValDef>(*s).rhs && !as<ValDef>(*s).isLazy)
            analyseCaptures(t.ctorParams, *as<ValDef>(*s).rhs);
        else if (s->kind != NodeKind::ValDef && s->kind != NodeKind::DefDef &&
                 s->kind != NodeKind::Import)
            analyseCaptures(t.ctorParams, *s);
    }
    // Parameter fields first, as scalac assigns them: a superclass constructor
    // that calls an overridden method must already see them. compileInitCall
    // stores the `this` the callee returns back into slot 0, so the fields
    // survive the initialiser chain.
    //
    // STORE_FIELD_IF_NEW, not STORE_FIELD: a public member's attribute key is
    // its plain name, so an ancestor's `val y` and a subclass's `override val y`
    // are one slot. Since the subclass stores first, the guard is what makes the
    // override win for the ancestor's own initialiser body -- scalac gives the
    // ancestor a second field and an overridden accessor instead.
    for (std::size_t k = 0; k < t.ctorParams.size(); ++k) {  // parameter fields
        emit(Op::PUSH_LOCAL, 1 + k, t.pos, +1);
        emit(Op::STORE_FIELD_IF_NEW,
             fn_->mod->addSymbol(info.members.at(t.ctorParams[k].name).key), t.pos, -1);
    }
    if (info.kind != ClassKind::Trait) {  // a trait's initialiser runs only its own body
        const ClassInfo* super = superclassOf(info);
        if (super) {
            const bool direct = !t.parents.empty() && &resolveType(*t.parents[0].type, t.pos) == super;
            compileInitCall(*super, direct ? t.parents[0].args : noArgs, t.pos);
        }
        std::unordered_set<std::string> bySuper;
        if (super) bySuper.insert(super->linearization.begin(), super->linearization.end());
        for (auto it = info.linearization.rbegin(); it != info.linearization.rend(); ++it) {
            if (*it == info.key || bySuper.count(*it)) continue;
            const ClassInfo* tr = globals_.findTypeByKey(*it);
            if (!tr || tr->kind != ClassKind::Trait || tr->builtin || !tr->hasInit) continue;
            const ParentRef* ref = nullptr;
            for (const ParentRef& p : t.parents)
                if (&resolveType(*p.type, p.pos) == tr) ref = &p;
            const std::size_t n = ref && ref->hasArgs ? ref->args.size() : 0;
            if (n != tr->primaryArity)
                throw CompileError(ref ? "wrong number of arguments for trait " + tr->name
                                       : "parameterized trait " + tr->name +
                                             " is indirectly implemented; it must be extended "
                                             "directly so that arguments can be passed",
                                   t.pos);
            compileInitCall(*tr, ref && ref->hasArgs ? ref->args : noArgs, t.pos);
        }
    }
    // A parameter field that overrides an inherited member is stored twice: once
    // before the chain (so an inherited initialiser sees it, as scalac does) and
    // once after it. The second store is still needed when the ancestor declares
    // the member in its *body* rather than as a parameter: that store runs after
    // the subclass's and is unconditional, because a subclass's own body `val`
    // must in turn be able to overwrite it (D108).
    for (std::size_t k = 0; k < t.ctorParams.size(); ++k) {
        const std::string& name = t.ctorParams[k].name;
        if (info.members.at(name).key != name) continue;  // a private field: no clash
        bool inherited = false;
        for (std::size_t j = 1; j < info.linearization.size() && !inherited; ++j) {
            const ClassInfo* a = globals_.findTypeByKey(info.linearization[j]);
            if (!a) continue;
            auto it = a->members.find(name);
            inherited = it != a->members.end() && it->second.key == name && it->second.concrete;
        }
        if (!inherited) continue;
        emit(Op::PUSH_LOCAL, 1 + k, t.pos, +1);
        emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(name).key), t.pos, -1);
    }
    for (const NodePtr& s : t.body) {  // lazy holders: a thunk method per member (Q12)
        if (s->kind != NodeKind::ValDef || !as<ValDef>(*s).isLazy) continue;
        const auto& v = as<ValDef>(*s);
        if (!v.rhs) throw CompileError("a lazy value needs an initialiser", v.pos);
        compileFunction("<lazy " + v.name + ">", {}, *v.rhs, FnShape::Method, v.pos);
        emit(Op::MAKE_LAZY, 0, v.pos, 0);
        emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(v.name).key), v.pos, -1);
    }
    for (const NodePtr& s : t.body) {  // template statements, in order
        if (s->kind == NodeKind::Import) {
            // Hoisted out of the template to the unit (D96): the binding is
            // installed in the unit's tables and outlives the class body. Left
            // skipped it would be silently ignored, which is exactly the trap
            // "parsed and ignored" was in Phase 1.
            compileImport(as<Import>(*s));
            continue;
        }
        if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            if (v.isLazy || !v.rhs) continue;
            compileExpr(*v.rhs);
            emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(v.name).key), v.pos, -1);
        } else if (s->kind != NodeKind::DefDef && s->kind != NodeKind::Import) {
            compileExpr(*s);
            emit(Op::POP, 0, s->pos, -1);
        }
    }
    emit(Op::PUSH_LOCAL, 0, t.pos, +1);
    emit(Op::RETURN, 0, t.pos, -1);
    // A constructor binds named arguments and fills its defaults in its own
    // prologue, exactly as a method does (Phase 4).
    recordParamsAndDefaults(*mod, t.ctorParams, /*method=*/true, info.name + ".<init>",
                            fs.scopes.front());
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), t.pos, +1);
}

// def this(ps) = { this(args); stats }: `<initN>(this, ps)`.
void Compiler::compileAuxConstructor(const DefDef& d, const ClassInfo& info) {
    const auto& params = paramsOfDef(d);
    if (!d.body) throw CompileError("an auxiliary constructor needs a body", d.pos);
    const Node& body = *d.body;
    const bool block = body.kind == NodeKind::Block;
    const Node* first =
        block ? (as<Block>(body).stats.empty() ? nullptr : as<Block>(body).stats[0].get()) : &body;
    const bool selfCall = first && first->kind == NodeKind::Apply &&
                          as<Apply>(*first).fn->kind == NodeKind::Ident &&
                          as<Ident>(*as<Apply>(*first).fn).name == "this";
    if (!selfCall)
        throw CompileError("an auxiliary constructor must begin with a call to another "
                           "constructor: this(...)",
                           d.pos);
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(info.name + ".<init" + std::to_string(params.size()) + ">");
    mod->setMethod(true);
    FunctionState fs;
    fs.mod = mod.get();
    fs.allowsReturn = false;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    fs.scopes.back()["this"] = LocalInfo{newSlot(), BindingKind::Param, false, false, {}};
    for (const Param& p : params)
        fs.scopes.back()[p.name] = LocalInfo{newSlot(), BindingKind::Param, false, false, {}};
    const int arity = 1 + static_cast<int>(params.size());
    mod->setArity(arity);
    analyseCaptures(params, body);
    if (as<Apply>(*first).args.size() == params.size())
        throw CompileError("an auxiliary constructor of " + info.name +
                               " must call a preceding constructor, not itself",
                           d.pos);
    compileInitCall(info, as<Apply>(*first).args, d.pos);
    if (block) {
        compileStats(as<Block>(body).stats, 1, body.pos);
        emit(Op::POP, 0, body.pos, -1);
    }
    emit(Op::PUSH_LOCAL, 0, d.pos, +1);
    emit(Op::RETURN, 0, d.pos, -1);
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), d.pos, +1);
}

// x_=(this, v): the instance of a class with var fields is mutable.
void Compiler::compileSetter(const std::string& fieldKey, const std::string& name, SourcePos pos) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(setterName(name));
    mod->setMethod(true);
    mod->setArity(2);
    mod->setMaxStack(2);
    mod->emit(Op::PUSH_LOCAL, 0, pos.line);
    mod->emit(Op::PUSH_LOCAL, 1, pos.line);
    mod->emit(Op::SET_FIELD, mod->addSymbol(fieldKey), pos.line);
    mod->emit(Op::PUSH_UNIT, 0, pos.line);
    mod->emit(Op::RETURN, 0, pos.line);
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), pos, +1);
}

// object O: a lazily initialised singleton (DESIGN §4.2): a lazy holder whose
// thunk instantiates O's class once, on first access.
void Compiler::compileObjectHolder(const ClassInfo& info, const std::string& termKey,
                                   SourcePos pos) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<object " + info.name + ">");
    mod->setMaxStack(1);
    mod->emit(Op::PUSH_GLOBAL, mod->addSymbol(info.key), pos.line);
    mod->emit(Op::NEW, mod->addSendSite(kPrimaryCtorKey, 0), pos.line);
    mod->emit(Op::RETURN, 0, pos.line);
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), pos, +1);
    emit(Op::MAKE_LAZY, 0, pos, 0);
    emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(termKey), pos, -1);
}

void Compiler::compileNew(const New& n) { compileNewOf(resolveType(*n.type, n.pos), n.args, n.pos); }

void Compiler::compileNewOf(const ClassInfo& info, const std::vector<NodePtr>& args,
                            SourcePos pos) {
    if (info.kind == ClassKind::Trait)
        throw CompileError(info.name + " is a trait; it cannot be instantiated", pos);
    if (info.kind == ClassKind::Object) throw CompileError("Not found: type " + info.name, pos);
    if (info.isAbstract)
        throw CompileError(info.name + " is abstract; it cannot be instantiated", pos);
    if (info.builtin && info.key.rfind("@Tuple", 0) == 0) {  // new TupleN(...): the tuple itself
        if (args.size() != info.primaryArity)
            throw CompileError("wrong number of arguments for the constructor of " + info.name, pos);
        for (const auto& arg : args) compileExpr(*arg);
        emit(Op::MAKE_TUPLE, args.size(), pos, 1 - static_cast<int>(args.size()));
        return;
    }
    emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(info.key), pos, +1);
    // `new C(x = 1)`: the arguments travel in protoCore's keywordParameters and
    // the constructor's own prologue binds them, exactly as a method's does. NEW
    // takes a KwSendSite instead of a SendSite; no extra opcode is needed.
    bool anyNamed = false;
    for (const auto& arg : args) anyNamed = anyNamed || arg->kind == NodeKind::NamedArg;
    if (anyNamed) {
        std::size_t positional = 0;
        const std::vector<std::string> keywords = splitNamedArgs(args, &positional);
        const std::uint32_t byNameCtor =
            info.primaryByNameMasks.empty() ? 0u : info.primaryByNameMasks[0];
        for (std::size_t k = 0; k < args.size(); ++k) {
            const Node& arg = args[k]->kind == NodeKind::NamedArg ? *as<NamedArg>(*args[k]).value
                                                                 : *args[k];
            // A by-name constructor parameter is only honoured positionally: a
            // named argument's parameter is not known until the callee binds it.
            if (args[k]->kind != NodeKind::NamedArg && k < kMaxByNameParams &&
                (byNameCtor >> k) & 1u)
                compileByNameArgument(arg);
            else
                compileExpr(arg);
        }
        emit(Op::NEW,
             fn_->mod->addKwSendSite(ctorKeyFor(info, args.size(), pos),
                                     static_cast<std::uint32_t>(positional), keywords),
             pos, -static_cast<int>(args.size()));
        return;
    }
    const bool spread = !args.empty() && args.back()->kind == NodeKind::Splice;
    // The class is named here, so its by-name constructor parameters are
    // honoured (D47).
    const std::uint32_t byName =
        info.primaryByNameMasks.empty() ? 0u : info.primaryByNameMasks[0];
    for (std::size_t k = 0; k < args.size(); ++k) {
        const Node& arg = *args[k];
        if (arg.kind == NodeKind::Splice) {
            if (k + 1 != args.size()) throw CompileError("a splice must be the last argument", arg.pos);
            if (!info.primaryVariadic)
                throw CompileError("the constructor of " + info.name +
                                       " takes no repeated parameter",
                                   arg.pos);
            compileExpr(*as<Splice>(arg).expr);
        } else if (k < kMaxByNameParams && (byName >> k) & 1u) {
            compileByNameArgument(arg);
        } else {
            compileExpr(arg);
        }
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    if (spread) {
        // The splice supplies the repeated parameter; the fixed ones must match.
        if (n - 1 != info.primaryArity - 1)
            throw CompileError("wrong number of arguments for the constructor of " + info.name +
                                   ": " + std::to_string(n - 1) + " before the splice, expected " +
                                   std::to_string(info.primaryArity - 1),
                               pos);
        emit(Op::NEW_SPREAD,
             fn_->mod->addSendSite(ctorKeyFor(info, info.primaryArity, pos), n - 1), pos,
             -static_cast<int>(n));
    } else
        emit(Op::NEW, fn_->mod->addSendSite(ctorKeyFor(info, n, pos), n), pos, -static_cast<int>(n));
}

// (a, b, ...): the case class TupleN of its arity (DESIGN §4.6); protoCore
// tuples are never used for it (they are interned and perennial).
void Compiler::compileTuple(const Tuple& t) {
    const std::size_t n = t.elems.size();
    if (n > kMaxTupleArity)
        throw CompileError("tuples of more than 22 elements are not supported (D32)", t.pos);
    for (const auto& e : t.elems) compileExpr(*e);
    emit(Op::MAKE_TUPLE, n, t.pos, 1 - static_cast<int>(n));
}

// super.m resolves relative to the method's defining class; super[T].m names T
// explicitly and the runtime probes T's OWN definition first, so `super[A].m`
// finds A's m when A defines it and the m A inherits when it does not — which is
// what "the member m as seen from A" means in Scala (DESIGN §4.4).
void Compiler::compileSuperSend(const std::string& name, const std::vector<NodePtr>& args,
                                const std::string& qualifier, SourcePos pos) {
    if (!tmpl_) throw CompileError("super can be used only inside a class, trait or object", pos);
    std::string ownerKey = tmpl_->info->key;
    bool exact = false;
    if (!qualifier.empty()) {
        const ClassInfo* t = globals_.findType(qualifier);
        if (!t)
            throw CompileError("super[" + qualifier + "]: " + qualifier +
                                   " is not a protoScala type",
                               pos);
        if (t->key == tmpl_->info->key)
            throw CompileError("super[" + qualifier + "]." + name + " inside " + qualifier +
                                   " itself would call itself",
                               pos);
        ownerKey = t->key;
        exact = true;
    }
    loadThis(pos);
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Splice || arg->kind == NodeKind::NamedArg)
            throw CompileError("super calls take plain arguments", arg->pos);
        compileExpr(*arg);
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    emit(Op::SEND_SUPER, fn_->mod->addSuperSite(name, n, ownerKey, exact), pos,
         -static_cast<int>(n));
}

// recv.m(a, k = v): named arguments reach native methods (Product.copy)
// through SEND_KW (Open question Q9).
void Compiler::compileNamedSend(const Select& sel, const std::vector<NodePtr>& args,
                                SourcePos pos) {
    compileExpr(*sel.qualifier);
    std::vector<std::string> keywords;
    std::size_t positional = 0;
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::NamedArg) {
            const std::string& name = as<NamedArg>(*arg).name;
            if (std::find(keywords.begin(), keywords.end(), name) != keywords.end())
                throw CompileError("parameter " + name + " is specified twice", arg->pos);
            keywords.push_back(name);
        } else {
            if (!keywords.empty())
                throw CompileError("positional after named argument", arg->pos);
            if (arg->kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg->pos);
            ++positional;
        }
    }
    for (const auto& arg : args)
        compileExpr(arg->kind == NodeKind::NamedArg ? *as<NamedArg>(*arg).value : *arg);
    emit(Op::SEND_KW, kwSendSite(sel.name, static_cast<std::uint32_t>(positional), keywords), pos,
         -static_cast<int>(args.size()));
}

} // namespace protoScala
