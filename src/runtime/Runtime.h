/*
 * Runtime — the protoCore objects one protoScala session needs, built once
 * per ProtoSpace and pinned in the root context's automatic-local slots for
 * the whole session (P1). Drivers never use root-context slots themselves;
 * they create child contexts.
 *
 * Primitive prototypes: protoCore aliases the integer, double, none and
 * method prototypes to objectPrototype and creates the string, boolean, char
 * and list prototypes immutable (core/ProtoSpace.cpp:1185-1216). Like
 * protoST (src/runtime/Bootstrap.cpp:88-103), the Runtime creates mutable
 * protoScala prototypes and rebinds the ProtoSpace fields to them, so a raw
 * SmallInteger, ProtoString or ProtoList answers Scala methods (DESIGN §4.1,
 * §6). One runtime per process (R5).
 */
#pragma once

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoSpace;
class ProtoString;
}

namespace protoScala {

inline constexpr unsigned kMaxFunctionArity = 22;

struct RuntimeLayout {
    // Every pointer below is pinned in a root-context slot (Runtime.cpp).
    proto::ProtoObject* globals = nullptr;        // mutable: module and session globals
    proto::ProtoObject* anyProto = nullptr;       // Any: parent of every Scala prototype
    proto::ProtoObject* intProto = nullptr;       // Int = Long = BigInt (D1)
    proto::ProtoObject* doubleProto = nullptr;    // Double = Float (D2)
    proto::ProtoObject* booleanProto = nullptr;
    proto::ProtoObject* charProto = nullptr;
    proto::ProtoObject* stringProto = nullptr;
    proto::ProtoObject* listProto = nullptr;      // List (varargs arrive as List)
    proto::ProtoObject* unitProto = nullptr;
    proto::ProtoObject* functionProto = nullptr;  // parent of Function0..22, FunctionXXL
    proto::ProtoObject* cellProto = nullptr;      // boxed captured variables
    proto::ProtoObject* lazyProto = nullptr;      // lazy val holders
    proto::ProtoObject* functionArity[kMaxFunctionArity + 2] = {};  // [23] = FunctionXXL
    const proto::ProtoObject* unit = nullptr;     // the () singleton (DESIGN §4.1)
    // Interned attribute keys (strong symbols of this space).
    const proto::ProtoString* codeKey = nullptr;      // "__code__": BytecodeModule address
    const proto::ProtoString* capturesKey = nullptr;  // "__captures__": ProtoList
    const proto::ProtoString* valueKey = nullptr;     // "__value__": Cell / Lazy value
    const proto::ProtoString* thunkKey = nullptr;     // "__thunk__": Lazy initialiser
    const proto::ProtoString* applyName = nullptr;    // "apply"

    const proto::ProtoObject* functionProtoFor(unsigned arity) const {
        return functionArity[arity <= kMaxFunctionArity ? arity : kMaxFunctionArity + 1];
    }
};

class Runtime {
public:
    explicit Runtime(proto::ProtoSpace& space);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    proto::ProtoContext* rootContext() const;
    const RuntimeLayout& layout() const { return layout_; }

private:
    proto::ProtoSpace& space_;
    RuntimeLayout layout_;
};

} // namespace protoScala
