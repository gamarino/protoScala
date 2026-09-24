/*
 * ClassInfo — the compile-time description of a class, trait or object
 * (the class of an object), kept in GlobalTable's type namespace.
 *
 * Keys: a type key starts with '@' (`@Point`, `@Point#1` for a REPL
 * redefinition, `@O.type` for the class of `object O`); the class prototype is
 * stored in the globals object under it, and it is also the marker attribute
 * every instance's chain answers (Design note 5 of the Phase 2 plan). A member
 * key is the member's name, or `<TypeKey-without-@>::<name>` for a private
 * member (D5). None of these can be a Scala identifier.
 */
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoScala {

enum class ClassKind : uint8_t { Class, Trait, Object };
enum class MemberKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef };

struct MemberInfo {
    MemberKind kind = MemberKind::Def;
    std::string key;        // attribute key on the instance or the prototype
    bool concrete = true;   // false: declared abstract (no body / no initialiser)
    // One mask per parameter list: bit k marks a by-name parameter (D47). A call
    // site honours them when it resolves to this declaration; a dynamic send
    // cannot, and evaluates the argument (D53).
    std::vector<std::uint32_t> byNameMasks;
    // A by-name constructor parameter: the field holds the thunk the `new` site
    // built, so every read of the name forces it (D47).
    bool byNameValue = false;
};

inline constexpr const char* kAnyKey = "@Any";
inline constexpr const char* kAnyRefKey = "@AnyRef";
inline constexpr const char* kProductKey = "@Product";
inline constexpr const char* kSerializableKey = "@Serializable";
inline constexpr const char* kActorKey = "@Actor";        // Phase 5 (DESIGN §8)
inline constexpr const char* kFutureKey = "@Future";
inline constexpr const char* kRangeKey = "@Range";        // Phase 3 (DESIGN §6)
inline constexpr const char* kVectorKey = "@Vector";
inline constexpr const char* kMapKey = "@Map";            // DESIGN §6.1
inline constexpr const char* kSetKey = "@Set";
inline constexpr const char* kThrowableKey = "@Throwable"; // Phase 4 (DESIGN §7)
inline constexpr const char* kEnumKey = "@Enum";           // Phase 4 (DESIGN §4.5)
inline constexpr const char* kPrimaryCtorKey = "<init>";
inline constexpr unsigned kMaxTupleArity = 22;

inline std::string tupleTypeKey(unsigned n) { return "@Tuple" + std::to_string(n); }
inline std::string auxCtorKey(std::size_t arity) { return "<init>" + std::to_string(arity); }
inline std::string setterName(const std::string& name) { return name + "_="; }
// The last segment of a (possibly dotted) template name: a template lifted out
// of an `object` keeps the qualified name for resolution and shows the simple
// one, exactly as Scala does (`O.C`'s toString says `C`).
inline std::string simpleName(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}
inline std::string privateKey(const std::string& typeKey, const std::string& name) {
    return typeKey.substr(1) + "::" + name;
}

struct ClassInfo {
    std::string name;                 // source name ("Point"; for an object, the object's name)
    std::string key;                  // type key
    ClassKind kind = ClassKind::Class;
    bool isCase = false;
    bool isAbstract = false;
    bool isFinal = false;
    bool isSealed = false;
    bool builtin = false;             // provided by the runtime (Any, AnyRef, Product, TupleN, ...)
    bool mutableInstances = false;    // it or an ancestor declares a var field (DESIGN §4.2)
    bool hasInit = true;              // traits: false when the trait has no fields, parameters or statements
    std::vector<std::string> linearization;  // type keys, the class first, ending with @AnyRef, @Any
    std::vector<std::string> fields;         // case classes: the product elements' attribute keys
    std::vector<std::string> ctorParams;     // primary constructor parameter names
    std::size_t primaryArity = 0;
    bool primaryVariadic = false;
    // By-name primary-constructor parameters, as one mask in a list (D47).
    std::vector<std::uint32_t> primaryByNameMasks;
    std::vector<std::size_t> auxArities;     // auxiliary constructors, by arity (D31)
    std::unordered_map<std::string, MemberInfo> members;  // public (own + inherited) and own private
    std::string companionTermKey;     // classes: the term key of the companion object, if any
    std::string companionTypeKey;     // classes: the companion object's type key; objects: their companion class's
    bool companionHasApply = false;   // the companion object defines apply / unapply itself
    bool companionHasUnapply = false;
};

// Any, AnyRef, Product, Serializable and Tuple2..Tuple22, with the keys the
// runtime binds (Runtime.cpp).
std::vector<ClassInfo> builtinTypes();

} // namespace protoScala
