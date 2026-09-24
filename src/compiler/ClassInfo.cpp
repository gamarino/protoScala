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
    // Phase 3: the collections that are objects rather than raw protoCore
    // values (DESIGN §6). `List` is a raw ProtoList and is tested by TypeCode,
    // so it needs no ClassInfo; these four are tested by their marker key.
    ClassInfo range = anyRef;
    range.name = "Range";
    range.key = kRangeKey;
    range.isFinal = true;
    range.linearization = {kRangeKey, kAnyRefKey, kAnyKey};
    for (const char* m : {"apply", "by", "contains", "map", "flatMap", "filter", "withFilter",
                          "foreach", "exists", "forall", "count", "find", "mkString"})
        member(range, m, MemberKind::Def);
    for (const char* m : {"length", "size", "isEmpty", "nonEmpty", "head", "last", "sum",
                          "reverse", "toList", "toSeq", "toVector", "toSet"})
        member(range, m, MemberKind::ParamlessDef);
    ClassInfo vector = anyRef;
    vector.name = "Vector";
    vector.key = kVectorKey;
    vector.isFinal = true;
    vector.linearization = {kVectorKey, kAnyRefKey, kAnyKey};
    ClassInfo map = anyRef;
    map.name = "Map";
    map.key = kMapKey;
    map.isFinal = true;
    map.linearization = {kMapKey, kAnyRefKey, kAnyKey};
    ClassInfo set = anyRef;
    set.name = "Set";
    set.key = kSetKey;
    set.isFinal = true;
    set.linearization = {kSetKey, kAnyRefKey, kAnyKey};
    // Phase 4: the marker trait every `enum` class extends (DESIGN §4.5). The
    // cases are ordinary case classes and case objects, so pattern matching,
    // toString, equals, hashCode and unapply come from Phase 2 for free, and
    // `enum` needs NO native method of its own.
    ClassInfo enumTrait = anyRef;
    enumTrait.name = "Enum";
    enumTrait.key = kEnumKey;
    enumTrait.kind = ClassKind::Trait;
    enumTrait.isAbstract = true;
    enumTrait.hasInit = false;
    enumTrait.linearization = {kEnumKey, kAnyRefKey, kAnyKey};
    // `ordinal` is declared ABSTRACT here: the desugarer gives every enum case a
    // `val ordinal`, and a case implementing an abstract member needs no
    // `override`. Nothing native is installed for it.
    enumTrait.members["ordinal"] = MemberInfo{MemberKind::ParamlessDef, "ordinal", false, {}};
    out.push_back(std::move(enumTrait));
    out.push_back(std::move(range));
    out.push_back(std::move(vector));
    out.push_back(std::move(map));
    out.push_back(std::move(set));
    out.push_back(std::move(actor));
    out.push_back(std::move(future));
    out.push_back(std::move(any));
    out.push_back(std::move(anyRef));
    out.push_back(std::move(product));
    out.push_back(std::move(serializable));
    return out;
}

} // namespace protoScala
