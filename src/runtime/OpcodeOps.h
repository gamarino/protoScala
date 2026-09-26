/*
 * OpcodeOps — one semantics, two consumers (Phase 7 §D1).
 *
 * Every opcode body lives here exactly once. `ExecutionEngine::runLoop` calls
 * these functions from its `switch` arms, and the C++ the transpiler emits calls
 * them through the thin `protoScala::gen::` forwards in
 * `src/runtime/GeneratedSupport.cpp`. There is no second implementation of ADD,
 * of SEND or of UNCONS, so the transpiler cannot drift from the interpreter:
 * there is nothing to drift from.
 *
 * Three classes of opcode, and only two of them appear here (the table is the
 * review artefact — an opcode in none of the three, or in two, is a bug):
 *
 *  V — vanishes in generated code, so there is no body to share. No entry here.
 *      NOP, EXTEND, PUSH_UNIT, PUSH_NULL, PUSH_TRUE, PUSH_FALSE, POP, DUP,
 *      PUSH_LOCAL, STORE_LOCAL, JUMP, JUMP_IF_FALSE's and JUMP_IF_TRUE's jump
 *      halves, JUMP_BACK's jump half, RETURN.
 *
 *  P — already reachable through a PUBLIC ExecutionEngine member; the entry is a
 *      one-line forward. CALL/CALL_SPREAD (invoke), SEND (send), NEW (construct),
 *      FORCE (force).
 *
 *  B — a real body, inside `runLoop`'s switch today, or a PRIVATE member.
 *      PUSH_CONST, PUSH_GLOBAL, STORE_GLOBAL, MAKE_CELL, PUSH_CELL, STORE_CELL,
 *      MAKE_FN, SEND/SEND_APPLY's dispatch, MAKE_LAZY, FORCE_THUNK,
 *      JUMP_IF_FALSE/TRUE's Boolean check, JUMP_BACK's safepoint, ADD..NE, NEG,
 *      NOT, CONCAT, MAKE_CLASS, NEW's instantiate, NEW_SPREAD, INVOKE_INIT,
 *      STORE_FIELD, STORE_FIELD_IF_NEW, SET_FIELD, SEND_SUPER, TEST_TYPE,
 *      TEST_PROTO, UNAPPLY_FIELDS, UNCONS, MATCH_ERROR, CAST_FAIL, MAKE_TUPLE,
 *      SEND_KW, CALL_KW, THROW, RETHROW.
 *
 * How class B reaches a private member, and why that is not "promoting eighteen
 * members to public". `dispatch`, `callMember`, `callWithReceiver`, `instantiate`,
 * `makeClass`, `makeTuple`, `superSend`, `sendKeywords`, `callKeywords`,
 * `testType`, `slowBinary`, `siteName` and `materialiseError` are private, and
 * the plan's first shape for this header moved their bodies out. They are not
 * moved. They are reached through `ops::Engine`, a single `friend` of
 * `ExecutionEngine` whose members are one-line forwards. That keeps the property
 * the phase needs — one implementation, two callers — while
 *
 *   - moving no line of a hot function, so the dispatch loop's code layout is
 *     unchanged by construction rather than by measurement (this project has
 *     measured a change that looked free costing 8 % of `sum_loop` cycles
 *     through layout alone, and backed it out under its own 3 % rule), and
 *   - leaving `ExecutionEngine`'s public contract exactly as it was: one
 *     `friend` declaration, not eighteen new public members.
 *
 * P1 applies to every caller of this header. A function here may allocate, so a
 * caller may not hold a `const proto::ProtoObject*` in a C++ local across a
 * call: the interpreter's operand stack is `ProtoContext` automatic locals and
 * so is the generated code's (Phase 7 Task 6 Step 2).
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/Opcodes.h"
#include "compiler/GlobalTable.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"

#include "protoCore.h"

#include <string>
#include <string_view>
#include <vector>

namespace protoScala::ops {

// ---------------------------------------------------------------------------
// The one bridge to ExecutionEngine's private members (see the header comment).
// ---------------------------------------------------------------------------
struct Engine {
    static const RuntimeLayout& layout(const ExecutionEngine& e) { return e.layout_; }

    static const proto::ProtoObject* dispatch(ExecutionEngine& e, proto::ProtoContext* ctx,
                                              const proto::ProtoObject** base,
                                              const proto::ProtoString* name, unsigned argc,
                                              bool applied) {
        return e.dispatch(ctx, base, name, argc, applied);
    }
    static const proto::ProtoString* siteName(const ExecutionEngine& e, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* receiver,
                                              const BytecodeModule::Const& site) {
        return e.siteName(ctx, receiver, site);
    }
    static const proto::ProtoObject* callWithReceiver(ExecutionEngine& e, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* method,
                                                      const proto::ProtoObject** base,
                                                      unsigned argc) {
        return e.callWithReceiver(ctx, method, base, argc);
    }
    static const proto::ProtoObject* instantiate(ExecutionEngine& e, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject** base,
                                                 const proto::ProtoString* ctorKey, unsigned argc,
                                                 const proto::ProtoSparseList* keywords) {
        return e.instantiate(ctx, base, ctorKey, argc, keywords);
    }
    static const proto::ProtoObject* makeClass(ExecutionEngine& e, proto::ProtoContext* ctx,
                                               const BytecodeModule::Const& spec,
                                               const proto::ProtoObject* const* base) {
        return e.makeClass(ctx, spec, base);
    }
    static const proto::ProtoObject* makeTuple(ExecutionEngine& e, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* const* elems, unsigned n) {
        return e.makeTuple(ctx, elems, n);
    }
    static const proto::ProtoObject* superSend(ExecutionEngine& e, proto::ProtoContext* ctx,
                                               const proto::ProtoObject** base,
                                               const BytecodeModule::Const& site) {
        return e.superSend(ctx, base, site);
    }
    static const proto::ProtoObject* sendKeywords(ExecutionEngine& e, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject** base,
                                                  const BytecodeModule::Const& site) {
        return e.sendKeywords(ctx, base, site);
    }
    static const proto::ProtoObject* callKeywords(ExecutionEngine& e, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject** base,
                                                  const BytecodeModule::Const& site) {
        return e.callKeywords(ctx, base, site);
    }
    static bool testType(const ExecutionEngine& e, proto::ProtoContext* ctx, TypeCode code,
                         const proto::ProtoObject* v) {
        return e.testType(ctx, code, v);
    }
    static const proto::ProtoObject* slowBinary(ExecutionEngine& e, proto::ProtoContext* ctx, Op op,
                                                const proto::ProtoObject* a,
                                                const proto::ProtoObject* b) {
        return e.slowBinary(ctx, op, a, b);
    }
    static const proto::ProtoObject* materialiseError(ExecutionEngine& e, proto::ProtoContext* ctx,
                                                      const ScalaError& err) {
        return e.materialiseError(ctx, err);
    }
};

// ---------------------------------------------------------------------------
// Class B — constants, globals, cells
// ---------------------------------------------------------------------------

/**
 * PUSH_CONST. Parameterised on the fields of one constant-pool entry rather than
 * on `BytecodeModule::Const`, because a generated module's pool is a C array of
 * PODs and C strings and holds no `std::string`.
 */
inline const proto::ProtoObject* constantOf(proto::ProtoContext* ctx,
                                            BytecodeModule::ConstKind kind, long long ival,
                                            double dval, int base, std::string_view sval,
                                            std::string_view where) {
    using K = BytecodeModule::ConstKind;
    switch (kind) {
        case K::Int:    return ctx->fromInteger(ival);
        case K::BigInt: return ctx->fromString(std::string(sval).c_str(), base);
        case K::Double: return ctx->fromDouble(dval);
        case K::String: return makeString(ctx, sval);   // may hold NUL
        case K::Char:   return ctx->fromUnicodeChar(static_cast<unsigned>(ival));
        case K::Symbol: case K::SendSite: case K::Names:
        case K::ClassSpec: case K::SuperSite: case K::KwSendSite:
            break;
    }
    throw std::logic_error("PUSH_CONST of a name constant in " + std::string(where));
}

/** PUSH_GLOBAL. `name` is only read on the error path. */
inline const proto::ProtoObject* pushGlobal(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                            const proto::ProtoString* key, std::string_view name) {
    const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(ctx, key);
    if ((!v || v == PROTO_NONE) &&
        L.globals->hasOwnAttribute(ctx, key) != PROTO_TRUE) [[unlikely]]
        throw ScalaError("UninitializedFieldError",
                         GlobalTable::nameOfKey(std::string(name)) +
                             " is used before it is initialised");
    return v ? v : PROTO_NONE;
}

inline void storeGlobal(proto::ProtoContext* ctx, const RuntimeLayout& L,
                        const proto::ProtoString* key, const proto::ProtoObject* v) {
    L.globals->setAttribute(ctx, key, v);
}

inline const proto::ProtoObject* makeCell(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    return L.cellProto->newChild(ctx, /*isMutable=*/true);
}

inline const proto::ProtoObject* cellGet(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                         const proto::ProtoObject* cell) {
    const proto::ProtoObject* v = cell->getOwnAttributeDirect(ctx, L.valueKey);
    return v ? v : PROTO_NONE;
}

/**
 * STORE_CELL. Returns the cell: a Cell is created mutable and `setAttribute`
 * updates it in place, but the caller writes the result back so that a holder
 * which is not mutable still behaves (the interpreter relies on the same
 * property for a lazy holder).
 */
inline const proto::ProtoObject* cellSet(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                         const proto::ProtoObject* cell,
                                         const proto::ProtoObject* v) {
    return cell->setAttribute(ctx, L.valueKey, v);
}

inline const proto::ProtoObject* makeLazy(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                          const proto::ProtoObject* thunk) {
    const proto::ProtoObject* holder = L.lazyProto->newChild(ctx, /*isMutable=*/true);
    holder->setAttribute(ctx, L.thunkKey, thunk);  // mutable: in place
    return holder;
}

/**
 * The shared half of MAKE_FN: the prototype choice and the captures list. The
 * two consumers differ only in what the code attribute holds — a `SmallInteger`
 * carrying a `BytecodeModule` address for the interpreter, a `ProtoMethodCell`
 * for a transpiled block — so the key and the value are parameters.
 */
inline const proto::ProtoObject* makeFunctionObject(proto::ProtoContext* ctx,
                                                    const RuntimeLayout& L, unsigned arity,
                                                    const proto::ProtoString* codeKey,
                                                    const proto::ProtoObject* codeValue,
                                                    const proto::ProtoObject** captures,
                                                    unsigned captureCount) {
    const proto::ProtoObject* fn =
        L.functionProtoFor(arity)->newChild(ctx)->setAttribute(ctx, codeKey, codeValue);
    if (captureCount > 0)
        fn = fn->setAttribute(ctx, L.capturesKey,
                              ctx->newList(captureCount, captures)->asObject(ctx));
    return fn;
}

// ---------------------------------------------------------------------------
// Class B — control flow
// ---------------------------------------------------------------------------

/** The Boolean check of JUMP_IF_FALSE / JUMP_IF_TRUE. Raises as the VM does. */
inline bool truthy(proto::ProtoContext* ctx, const RuntimeLayout& L,
                   const proto::ProtoObject* v) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw ScalaError("ClassCastException", typeName(ctx, L, v) + " cannot be cast to Boolean");
}

/**
 * JUMP_BACK's GC obligation (P4 rule 1). `ProtoContext::safepoint()` is the only
 * place a context's young chain reaches the collector; an unsubmitted chain is
 * live by construction and reclaims nothing while looking healthy. A generated
 * loop without this is that bug in machine-written code.
 */
inline void safepoint(proto::ProtoContext* ctx) { ctx->safepoint(); }

// ---------------------------------------------------------------------------
// Class B — arithmetic, comparison, equality, CONCAT
// ---------------------------------------------------------------------------

/**
 * ADD, SUB and MUL, with the SmallInteger fast path that is measured to matter.
 * `|x|, |y| < 2^53`, so `x + y` and `x - y` cannot overflow a `long long`.
 */
inline const proto::ProtoObject* arith(proto::ProtoContext* ctx, ExecutionEngine& eng, Op op,
                                       const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
        long long r;
        bool fits;
        if (op == Op::ADD)      { r = x + y; fits = proto::smallIntInRange(r); }
        else if (op == Op::SUB) { r = x - y; fits = proto::smallIntInRange(r); }
        else fits = !__builtin_mul_overflow(x, y, &r) && proto::smallIntInRange(r);
        return fits ? proto::makeSmallInt(r)
             : op == Op::ADD ? a->add(ctx, b)          // promotes to LargeInteger
             : op == Op::SUB ? a->subtract(ctx, b)
                             : a->multiply(ctx, b);
    }
    return Engine::slowBinary(eng, ctx, op, a, b);
}

/** LT, LE, GT, GE. */
inline const proto::ProtoObject* compare(proto::ProtoContext* ctx, ExecutionEngine& eng, Op op,
                                         const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
        const bool r = op == Op::LT ? x < y : op == Op::LE ? x <= y
                     : op == Op::GT ? x > y : x >= y;
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    return Engine::slowBinary(eng, ctx, op, a, b);
}

/**
 * EQ and NE. No SmallInteger fast path here, and deliberately: the first branch
 * of `valuesEqual` already is one, so duplicating it buys nothing and Phase 3
 * measured the duplicate costing `sum_loop` 8 % through code layout alone.
 */
inline const proto::ProtoObject* equality(proto::ProtoContext* ctx, const RuntimeLayout& L, Op op,
                                          const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const bool eq = valuesEqual(ctx, L, a, b);
    return (eq == (op == Op::EQ)) ? PROTO_TRUE : PROTO_FALSE;
}

inline const proto::ProtoObject* neg(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                     const proto::ProtoObject* a) {
    if (proto::isSmallInt(a) && proto::asSmallInt(a) != proto::PROTO_SMALL_INT_MIN)
        return proto::makeSmallInt(-proto::asSmallInt(a));
    if (isNumberFast(a)) return a->negate(ctx);
    return eng.send(ctx, a, Engine::layout(eng).unaryMinusName, nullptr, 0);
}

inline const proto::ProtoObject* notOp(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                       const proto::ProtoObject* a) {
    if (a == PROTO_TRUE) return PROTO_FALSE;
    if (a == PROTO_FALSE) return PROTO_TRUE;
    return eng.send(ctx, a, Engine::layout(eng).unaryNotName, nullptr, 0);
}

/**
 * CONCAT. `base[0..n)` is a run of traced slots, and each converted piece is
 * written back into its own slot before the next allocation: an accumulator held
 * only in a C++ local is the Phase 5 `Mailbox::push` bug. The result is left in
 * `base[0]` and returned.
 */
inline const proto::ProtoObject* concat(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject** base, unsigned n,
                                        std::string_view where) {
    if (n < 2)
        throw std::logic_error("CONCAT of arity " + std::to_string(n) + " in " + std::string(where));
    base[0] = toScalaString(ctx, L, base[0]);
    for (unsigned k = 1; k < n; ++k) {
        base[k] = toScalaString(ctx, L, base[k]);
        base[0] = reinterpret_cast<const proto::ProtoString*>(base[0])
                      ->appendLast(ctx, reinterpret_cast<const proto::ProtoString*>(base[k]))
                      ->asObject(ctx);
    }
    return base[0];
}

// ---------------------------------------------------------------------------
// Class B — fields
// ---------------------------------------------------------------------------

/** STORE_FIELD: in a constructor `this` is slot 0, so the store yields a new `this`. */
inline const proto::ProtoObject* storeField(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                            const proto::ProtoString* key,
                                            const proto::ProtoObject* v) {
    return self->setAttribute(ctx, key, v);
}

/**
 * STORE_FIELD_IF_NEW. A more-derived constructor stored its own `override val`
 * before calling this one, and that value must survive the whole initialiser
 * chain. The probe is `hasOwnAttribute`, not a read, because PROTO_NONE is both a
 * value and the missing-attribute answer.
 */
inline const proto::ProtoObject* storeFieldIfNew(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ProtoString* key,
                                                 const proto::ProtoObject* v) {
    if (self->hasOwnAttribute(ctx, key) != PROTO_TRUE) return self->setAttribute(ctx, key, v);
    return self;
}

/** SET_FIELD: the setter of a `var` field; the instance must be mutable. */
inline void setField(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                     const proto::ProtoString* key, const proto::ProtoObject* v) {
    if (obj->setAttribute(ctx, key, v) != obj)
        throw ScalaError("UnsupportedOperationException",
                         "cannot assign a field of an immutable object");
}

// ---------------------------------------------------------------------------
// Class B — pattern matching
// ---------------------------------------------------------------------------

/** TEST_PROTO: class membership by marker attribute (Design note 5). */
inline bool testProtoKey(proto::ProtoContext* ctx, const proto::ProtoString* key,
                         const proto::ProtoObject* v) {
    return v != PROTO_NONE && v->getAttribute(ctx, key) == PROTO_TRUE;
}

/**
 * UNAPPLY_FIELDS. Writes one value per key into `out`, which must be a run of
 * `keys.size()` traced slots. Returns the number written.
 */
inline unsigned unapplyFieldsWith(proto::ProtoContext* ctx,
                                  const proto::ProtoString* const* keys, unsigned keyCount,
                                  const proto::ProtoObject* v, const proto::ProtoObject** out) {
    for (unsigned k = 0; k < keyCount; ++k) {
        const proto::ProtoObject* f = v->getOwnAttributeDirect(ctx, keys[k]);
        out[k] = f ? f : PROTO_NONE;
    }
    return keyCount;
}

/** UNCONS: head into `*outHead`, tail into `*outTail`; both must be traced slots. */
inline void uncons(proto::ProtoContext* ctx, const proto::ProtoObject* listObj,
                   const proto::ProtoObject** outHead, const proto::ProtoObject** outTail) {
    const proto::ProtoList* list = listObj->asList(ctx);
    *outHead = list->getAt(ctx, 0);
    *outTail = list->removeFirst(ctx)->asObject(ctx);
}

[[noreturn]] inline void matchError(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                    const proto::ProtoObject* v) {
    throw ScalaError("MatchError",
                     show(ctx, L, v) + " (of class " + typeName(ctx, L, v) + ")");
}

[[noreturn]] inline void castFail(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                  const proto::ProtoObject* v, std::string_view typeName_) {
    throw ScalaError("ClassCastException",
                     typeName(ctx, L, v) + " cannot be cast to " + std::string(typeName_));
}

// ---------------------------------------------------------------------------
// Class B — exceptions
// ---------------------------------------------------------------------------

/**
 * THROW. Only a `Throwable` may be thrown (Scala's rule), tested with the Phase 2
 * per-class marker attribute and not with protoCore's `isInstanceOf` (R3).
 */
[[noreturn]] inline void throwValue(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                    const proto::ProtoObject* v) {
    if (v == PROTO_NONE) throw ScalaError("NullPointerException", "throw null");
    if (v->getAttribute(ctx, L.throwableKey) != PROTO_TRUE)
        throw ScalaError("IllegalArgumentException",
                         "throw expects a Throwable, got " + typeName(ctx, L, v));
    throw ScalaThrow(v);
}

/** RETHROW: re-raise the value a handler saved in a slot (plan A0-4). */
[[noreturn]] inline void rethrow(const proto::ProtoObject* v, std::string_view where) {
    if (!v || v == PROTO_NONE)
        throw std::logic_error("RETHROW with no saved exception in " + std::string(where));
    throw ScalaThrow(v);
}

// ---------------------------------------------------------------------------
// Class B — sends, and the D5 fallback the emitter must never re-implement
// ---------------------------------------------------------------------------

/**
 * SEND / SEND_APPLY, keyed by already-interned symbols rather than by a pool
 * index, so both consumers can reach it. `fallback` is the D5 plain name a send
 * retries when the receiver does not carry a private member's class-qualified
 * key; passing it is how the emitter avoids re-implementing the retry.
 */
inline const proto::ProtoObject* sendNamed(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                           const proto::ProtoObject** base,
                                           const proto::ProtoString* name, unsigned argc,
                                           const proto::ProtoString* fallback, bool applied) {
    const proto::ProtoObject* receiver = base[0];
    const proto::ProtoString* resolved =
        (!fallback || receiver == PROTO_NONE)                     ? name
        : receiver->hasAttribute(ctx, name) == PROTO_TRUE         ? name
                                                                  : fallback;
    return Engine::dispatch(eng, ctx, base, resolved, argc, applied);
}

// ---------------------------------------------------------------------------
// Class P — one-line forwards to public members
// ---------------------------------------------------------------------------

inline const proto::ProtoObject* call(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                      const proto::ProtoObject* callee,
                                      const proto::ProtoObject* const* args, unsigned argc) {
    return eng.invoke(ctx, callee, args, argc);
}

inline const proto::ProtoObject* force(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                       const proto::ProtoObject* v) {
    return eng.force(ctx, v);
}

inline const proto::ProtoObject* construct(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                           const proto::ProtoObject* cls,
                                           const proto::ProtoObject* const* args, unsigned argc) {
    return eng.construct(ctx, cls, args, argc);
}

/**
 * FORCE_THUNK — a read of a by-name parameter (D47). The thunk the call site
 * built is a zero-argument protoScala function; any other value is the argument
 * itself, already evaluated at a call site the compiler could not resolve (D53),
 * and passes through untouched. A transpiled thunk is a native method object, so
 * the interpreter's `compiledModuleOf` probe is joined by an arity probe on the
 * function prototype, which both shapes carry.
 */
inline bool isZeroArgThunk(proto::ProtoContext* ctx, const RuntimeLayout& L,
                           const proto::ProtoObject* v) {
    if (const BytecodeModule* m = compiledModuleOf(ctx, L, v))
        return !m->isMethod() && m->arity() == 0;
    return false;
}

}  // namespace protoScala::ops
