#include "compiler/ClassInfo.h"

namespace protoScala {

std::vector<ClassInfo> builtinTypes() {
    std::vector<ClassInfo> out;
    auto member = [](ClassInfo& c, const std::string& name, MemberKind kind) {
        c.members[name] = MemberInfo{kind, name, true, {}};
    };
    ClassInfo any;
    any.name = "Any";
    any.key = kAnyKey;
    any.isAbstract = true;
    any.builtin = true;
    any.linearization = {kAnyKey};
    for (const char* m : {"equals", "==", "!=", "eq", "ne"}) member(any, m, MemberKind::Def);
    for (const char* m : {"toString", "hashCode", "##"}) member(any, m, MemberKind::ParamlessDef);
    ClassInfo anyRef = any;
    anyRef.name = "AnyRef";
    anyRef.key = kAnyRefKey;
    anyRef.linearization = {kAnyRefKey, kAnyKey};
    ClassInfo product = anyRef;
    product.name = "Product";
    product.key = kProductKey;
    product.kind = ClassKind::Trait;
    product.hasInit = false;
    product.linearization = {kProductKey, kAnyRefKey, kAnyKey};
    member(product, "productArity", MemberKind::ParamlessDef);
    member(product, "productPrefix", MemberKind::ParamlessDef);
    member(product, "productElement", MemberKind::Def);
    member(product, "canEqual", MemberKind::Def);
    ClassInfo serializable = anyRef;
    serializable.name = "Serializable";
    serializable.key = kSerializableKey;
    serializable.kind = ClassKind::Trait;
    serializable.hasInit = false;
    serializable.linearization = {kSerializableKey, kAnyRefKey, kAnyKey};
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        ClassInfo t = product;
        t.name = "Tuple" + std::to_string(n);
        t.key = tupleTypeKey(n);
        t.kind = ClassKind::Class;
        t.isAbstract = false;
        t.isCase = true;
        t.isFinal = true;
        t.hasInit = true;
        t.linearization = {t.key, kProductKey, kAnyRefKey, kAnyKey};
        t.primaryArity = n;
        for (unsigned k = 1; k <= n; ++k) {
            const std::string f = "_" + std::to_string(k);
            t.fields.push_back(f);
            t.ctorParams.push_back(f);
            member(t, f, MemberKind::Val);
        }
        member(t, "copy", MemberKind::Def);
        out.push_back(std::move(t));
    }
    // Phase 5: `x.isInstanceOf[Actor]` and `case f: Future` (DESIGN §8).
    ClassInfo actor = anyRef;
    actor.name = "Actor";
    actor.key = kActorKey;
    actor.isFinal = true;
    actor.linearization = {kActorKey, kAnyRefKey, kAnyKey};
    member(actor, "!", MemberKind::Def);
    member(actor, "?", MemberKind::Def);
    member(actor, "send", MemberKind::Def);
    member(actor, "ask", MemberKind::Def);
    member(actor, "value", MemberKind::ParamlessDef);
    ClassInfo future = anyRef;
    future.name = "Future";
    future.key = kFutureKey;
    future.isFinal = true;
    future.linearization = {kFutureKey, kAnyRefKey, kAnyKey};
    member(future, "await", MemberKind::ParamlessDef);
    member(future, "isCompleted", MemberKind::ParamlessDef);
    member(future, "value", MemberKind::ParamlessDef);
    for (const char* m : {"map", "flatMap", "recover", "onComplete"})
        member(future, m, MemberKind::Def);
    out.push_back(std::move(actor));
    out.push_back(std::move(future));
    out.push_back(std::move(any));
    out.push_back(std::move(anyRef));
    out.push_back(std::move(product));
    out.push_back(std::move(serializable));
    return out;
}

} // namespace protoScala
