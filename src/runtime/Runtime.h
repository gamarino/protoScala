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
#include "compiler/ClassInfo.h"  // the type-key constants and kMaxTupleArity

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
    // Phase 3: the operator method names the dispatch loop sends when an operand
    // is not a number. Interning them per execution was measured at 51 % of CPU
    // in protoST and leaks for names longer than six bytes (R4), so every one
    // this engine can send lives here and is read, never re-interned.
    const proto::ProtoString* unaryMinusName = nullptr;  // "unary_-"
    const proto::ProtoString* unaryNotName = nullptr;    // "unary_!"
    // Indexed by Op: ADD, SUB, MUL, LT, LE, GT, GE (see binaryOpName()).
    const proto::ProtoString* binaryOpName[7] = {};

    // Phase 2 (pinned in root-context slots like the rest):
    proto::ProtoObject* anyRefProto = nullptr;        // AnyRef: the root of every class chain
    proto::ProtoObject* productProto = nullptr;       // Product: case-class members (natives)
    proto::ProtoObject* serializableProto = nullptr;  // Serializable: a marker trait
    proto::ProtoObject* withFilterProto = nullptr;    // the lazy result of List.withFilter
    proto::ProtoObject* listCompanion = nullptr;      // the value of the global `List`
    proto::ProtoObject* tupleProto[kMaxTupleArity + 1] = {};  // [2..22]: Tuple2..Tuple22
    proto::ProtoObject* tupleCompanion[kMaxTupleArity + 1] = {};  // [2..22]: the TupleN companions
    const proto::ProtoString* nameKey = nullptr;      // "__name__": a class's display name
    const proto::ProtoString* prefixKey = nullptr;    // "__prefix__": a case class's productPrefix
    const proto::ProtoString* fieldsKey = nullptr;    // "__fields__": ProtoList of element keys
    const proto::ProtoString* tupleKey = nullptr;     // "__tuple__": PROTO_TRUE on Tuple2..22
    const proto::ProtoString* mutableKey = nullptr;   // "__mutable__": instances are mutable
    const proto::ProtoString* selfKey = nullptr;      // "__self__": receiver of a bound method
    const proto::ProtoString* listKey = nullptr;      // "__list__": the source of a WithFilter
    const proto::ProtoString* predsKey = nullptr;     // "__preds__": its predicates (ProtoList)
    const proto::ProtoString* initKey = nullptr;      // "<init>"
    const proto::ProtoString* toStringName = nullptr; // "toString"
    const proto::ProtoString* equalsName = nullptr;   // "equals"
    const proto::ProtoString* hashCodeName = nullptr; // "hashCode"
    const proto::ProtoString* canEqualName = nullptr; // "canEqual"
    const proto::ProtoString* tupleFieldKey[kMaxTupleArity + 1] = {};  // [1..22]: "_1".."_22"

    // Phase 5: the concurrency prototypes and keys (DESIGN §8). Every prototype
    // and the registry are pinned in root-context slots (Runtime.cpp).
    proto::ProtoObject* actorProto = nullptr;         // the prototype of every actor
    proto::ProtoObject* futureProto = nullptr;        // the prototype of every Future
    proto::ProtoObject* envelopeProto = nullptr;      // one queued message
    proto::ProtoObject* threadProto = nullptr;        // a Thread handle (D49)
    proto::ProtoObject* frameProto = nullptr;         // one record of a suspended call chain
    proto::ProtoObject* spawnPartialProto = nullptr;  // the result of Actor.spawn(state)
    proto::ProtoObject* actorRegistry = nullptr;      // mutable: anchors every actor (D46)
    proto::ProtoObject* actorCompanion = nullptr;     // the value of the global `Actor`
    proto::ProtoObject* priorityCompanion = nullptr;  // the value of the global `Priority`
    proto::ProtoObject* futureCompanion = nullptr;    // the value of the global `Future`
    proto::ProtoObject* threadCompanion = nullptr;    // the value of the global `Thread`
    proto::ProtoObject* systemCompanion = nullptr;    // the value of the global `System`
    const proto::ProtoString* handlerKey = nullptr;    // "__handler__"
    const proto::ProtoString* actorStateKey = nullptr; // "__astate__": the user state
    const proto::ProtoString* stateRefKey = nullptr;   // "__sched_ref__": ActorState address
    const proto::ProtoString* mailboxKey[3] = {};      // "__mbox0__".."__mbox2__"
    const proto::ProtoString* pendingKey[3] = {};      // "__pend0__".."__pend2__"
    const proto::ProtoString* actorsKey = nullptr;     // "__actors__" on the registry
    const proto::ProtoString* threadsKey = nullptr;    // "__threads__" on the registry
    const proto::ProtoString* msgKey = nullptr;        // "__msg__" on an envelope
    const proto::ProtoString* futureKey = nullptr;     // "__future__" on an envelope
    const proto::ProtoString* snapshotKey = nullptr;   // "__snapshot__": suspended frames
    const proto::ProtoString* waitingOnKey = nullptr;  // "__waiting_on__": the awaited future
    const proto::ProtoString* turnFutureKey = nullptr; // "__turn_future__": the suspended ask
    const proto::ProtoString* fstateKey = nullptr;     // "__fstate__": 0 pending, 1 ok, 2 failed
    const proto::ProtoString* fvalueKey = nullptr;     // "__fvalue__"
    const proto::ProtoString* ferrorKey = nullptr;     // "__ferror__": a RuntimeError instance
    const proto::ProtoString* waitersKey = nullptr;    // "__waiters__": suspended actors
    const proto::ProtoString* contsKey = nullptr;      // "__conts__": continuations (D48)
    const proto::ProtoString* modKey = nullptr;        // "__mod__" on a frame record
    const proto::ProtoString* ipKey = nullptr;         // "__ip__"
    const proto::ProtoString* fbaseKey = nullptr;      // "__fbase__"
    const proto::ProtoString* fslotsKey = nullptr;     // "__fslots__"
    const proto::ProtoString* threadRefKey = nullptr;  // "__thread__": ProtoThread address
    const proto::ProtoString* bodyKey = nullptr;       // "__body__": a Thread's function
    const proto::ProtoString* tuple2Key = nullptr;     // "@Tuple2": the D45 handler-result test
    const proto::ProtoString* classNameField = nullptr;  // RuntimeError.className
    const proto::ProtoString* messageField = nullptr;    // RuntimeError.message

    // Phase 3: the collection prototypes (DESIGN §6, plan A0-4). Each is an
    // ordinary protoCore object whose payload is one attribute, and each is
    // pinned in a root-context slot like the rest.
    proto::ProtoObject* rangeProto = nullptr;          // Range
    proto::ProtoObject* vectorProto = nullptr;         // Vector
    proto::ProtoObject* mapProto = nullptr;            // Map (DESIGN §6.1)
    proto::ProtoObject* setProto = nullptr;            // Set
    proto::ProtoObject* vectorCompanion = nullptr;     // the value of the global `Vector`
    proto::ProtoObject* mapCompanion = nullptr;        // the value of the global `Map`
    proto::ProtoObject* setCompanion = nullptr;        // the value of the global `Set`
    proto::ProtoObject* foldPartialProto = nullptr;    // the result of xs.foldLeft(z)
    const proto::ProtoString* rangeStartKey = nullptr;     // "__start__"
    const proto::ProtoString* rangeEndKey = nullptr;       // "__end__"
    const proto::ProtoString* rangeStepKey = nullptr;      // "__step__"
    const proto::ProtoString* rangeInclusiveKey = nullptr; // "__inclusive__"
    const proto::ProtoString* vecDataKey = nullptr;        // "__vec__": a ProtoList
    const proto::ProtoString* mapDataKey = nullptr;        // "__map__": a ProtoMap
    const proto::ProtoString* foldSrcKey = nullptr;        // "__fold_src__"
    const proto::ProtoString* foldSeedKey = nullptr;       // "__fold_seed__"
    const proto::ProtoString* foldLeftKey = nullptr;       // "__fold_left__"
    // The one `equals` installed on anyProto (Primitives.cpp, any_equals). A
    // class that does not override `equals` resolves to exactly this object,
    // which is how scalaIsIdentityKey decides DESIGN §6.1's identity/value
    // classification without a heuristic. Filled by installPrimitives, not by
    // Runtime::Runtime, because the method object does not exist until then.
    const proto::ProtoObject* defaultEqualsMethod = nullptr;

    // Prelude values the natives construct, resolved after the prelude is
    // compiled (a REPL redefinition gives `Some#1`, so a name cannot be
    // hard-coded). Filled by bindPreludeHooks.
    struct PreludeHooks {
        const proto::ProtoObject* actorStats = nullptr;
        const proto::ProtoObject* someCompanion = nullptr;
        const proto::ProtoObject* noneValue = nullptr;
        const proto::ProtoObject* success = nullptr;
        const proto::ProtoObject* failure = nullptr;
        const proto::ProtoObject* runtimeError = nullptr;
        const proto::ProtoObject* left = nullptr;     // Phase 3: __mkLeft
        const proto::ProtoObject* right = nullptr;    // Phase 3: __mkRight
        bool bound = false;
    } hooks;

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
    // Non-const access, used only by bindPreludeHooks (Session, EvalHarness).
    RuntimeLayout& mutableLayout() { return layout_; }

private:
    proto::ProtoSpace& space_;
    RuntimeLayout layout_;
};

} // namespace protoScala
