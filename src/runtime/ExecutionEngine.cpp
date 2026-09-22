#include "runtime/ExecutionEngine.h"
#include "compiler/GlobalTable.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/StackGuard.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace protoScala {

namespace {

thread_local ActiveCallContext tl_active{};
thread_local bool tl_activeSet = false;

struct ActiveGuard {
    ActiveCallContext saved;
    bool wasSet;
    ActiveGuard(ExecutionEngine* e, const RuntimeLayout* l) : saved(tl_active), wasSet(tl_activeSet) {
        tl_active = ActiveCallContext{e, l};
        tl_activeSet = true;
    }
    ~ActiveGuard() { tl_active = saved; tl_activeSet = wasSet; }
};

const char* opSymbol(Op op) {
    switch (op) {
        case Op::ADD: return "+";  case Op::SUB: return "-";  case Op::MUL: return "*";
        case Op::LT:  return "<";  case Op::LE:  return "<="; case Op::GT:  return ">";
        case Op::GE:  return ">="; default:      return "?";
    }
}

[[noreturn, gnu::cold]] void throwNotBoolean(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                             const proto::ProtoObject* v) {
    throw ScalaError("ClassCastException", typeName(ctx, L, v) + " cannot be cast to Boolean");
}

} // namespace

const ActiveCallContext* activeCallContext() { return tl_activeSet ? &tl_active : nullptr; }

const proto::ProtoObject* ExecutionEngine::run(proto::ProtoContext* parent, const BytecodeModule& mod) {
    ActiveGuard guard(this, &layout_);
    return execute(parent, mod, nullptr, 0, nullptr);
}

const proto::ProtoObject* ExecutionEngine::callTopLevel(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* callable,
                                                        const proto::ProtoObject* const* args,
                                                        unsigned argc) {
    ActiveGuard guard(this, &layout_);
    return invoke(ctx, callable, args, argc);
}

const proto::ProtoObject* ExecutionEngine::callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                                      const proto::ProtoObject* self,
                                                      const proto::ProtoObject* const* args,
                                                      unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* list = scope.newList(argc, args);
    const proto::ProtoObject* r = fn(&scope, self, nullptr, list, nullptr);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* ExecutionEngine::invoke(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* callee,
                                                  const proto::ProtoObject* const* args,
                                                  unsigned argc) {
    if (callee == PROTO_NONE) throw ScalaError("NullPointerException", "cannot call null");
    if (const BytecodeModule* m = compiledModuleOf(ctx, layout_, callee)) {
        const proto::ProtoObject* caps =
            m->captureCount() ? callee->getOwnAttributeDirect(ctx, layout_.capturesKey) : nullptr;
        return execute(ctx, *m, args, argc, caps);
    }
    if (callee->isMethod(ctx))
        return callNative(ctx, callee->asMethod(ctx), callee->asMethodSelf(ctx), args, argc);
    return send(ctx, callee, layout_.applyName, args, argc);  // universal apply (DESIGN §5.1)
}

const proto::ProtoObject* ExecutionEngine::send(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* receiver,
                                                const proto::ProtoString* name,
                                                const proto::ProtoObject* const* args,
                                                unsigned argc) {
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + name->toStdString(ctx) + "' on null");
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE) {
        // PROTO_NONE is also a stored null: probe presence (DESIGN §4.1).
        if (receiver->hasAttribute(ctx, name) != PROTO_TRUE)
            throw ScalaError("NoSuchMethodError", "value " + name->toStdString(ctx) +
                             " is not a member of " + typeName(ctx, layout_, receiver));
        if (argc == 0) return PROTO_NONE;
        throw ScalaError("NullPointerException", "cannot call null");
    }
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), receiver, args, argc);
    if (argc == 0 && !compiledModuleOf(ctx, layout_, m)) return m;  // a plain attribute (field read)
    throw ScalaError("NoSuchMethodError",
                     "methods written in Scala on objects are not implemented yet");
}

const proto::ProtoObject* ExecutionEngine::force(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* v) {
    if (v == PROTO_NONE || v->getPrototype(ctx) != layout_.lazyProto) return v;
    if (v->hasOwnAttribute(ctx, layout_.valueKey) == PROTO_TRUE)
        return v->getOwnAttributeDirect(ctx, layout_.valueKey);
    const proto::ProtoObject* thunk = v->getOwnAttributeDirect(ctx, layout_.thunkKey);
    const proto::ProtoObject* r = invoke(ctx, thunk, nullptr, 0);
    v->setAttribute(ctx, layout_.valueKey, r);  // mutable holder: in place
    return r;
}

const proto::ProtoObject* ExecutionEngine::execute(proto::ProtoContext* parent,
                                                   const BytecodeModule& mod,
                                                   const proto::ProtoObject* const* args,
                                                   unsigned argc,
                                                   const proto::ProtoObject* captures) {
    checkNativeStack();
    const unsigned arity = static_cast<unsigned>(mod.arity());
    const unsigned fixed = mod.isVariadic() ? arity - 1 : arity;
    if (mod.isVariadic() ? argc < fixed : argc != arity)
        throw ScalaError("IllegalArgumentException",
                         "wrong number of arguments for " + mod.name() + ": expected " +
                         std::to_string(fixed) + (mod.isVariadic() ? " or more" : "") +
                         ", got " + std::to_string(argc));
    const unsigned stackBase = arity + static_cast<unsigned>(mod.localCount());

    proto::ProtoContext frame(parent->space, parent);
    frame.resizeAutomaticLocals(stackBase + static_cast<unsigned>(mod.maxStack()));
    const proto::ProtoObject** slots = frame.getAutomaticLocals();  // never resized again
    for (unsigned k = 0; k < fixed; ++k) slots[k] = args[k];
    if (mod.isVariadic())
        slots[fixed] = frame.newList(argc - fixed, args + fixed)->asObject(&frame);
    if (mod.captureCount() > 0) {
        const proto::ProtoList* caps = captures->asList(&frame);
        const auto& specs = mod.captureSpecs();
        for (std::size_t k = 0; k < specs.size(); ++k)
            slots[specs[k].localSlot] = caps->getAt(&frame, static_cast<int>(k));
    }

    const proto::ProtoObject** sp = slots + stackBase;  // next free operand slot
    const Instr* const code = mod.code().data();
    const Instr* ip = code;
    const RuntimeLayout& L = layout_;
    try {
        for (;;) {
            Instr word = *ip++;
            Op op = static_cast<Op>(word & 0xFF);
            std::uint64_t operand = word >> kOperandShift;
            if (op == Op::EXTEND) {
                const Instr next = *ip++;
                operand = (operand << 24) | (next >> kOperandShift);
                op = static_cast<Op>(next & 0xFF);
            }
            switch (op) {
                case Op::NOP: case Op::EXTEND: continue;
                case Op::PUSH_CONST: {
                    const auto& c = mod.constAt(operand);
                    using K = BytecodeModule::ConstKind;
                    switch (c.kind) {
                        case K::Int:    *sp++ = frame.fromInteger(c.ival); break;
                        case K::BigInt: *sp++ = frame.fromString(c.sval.c_str(), c.base); break;
                        case K::Double: *sp++ = frame.fromDouble(c.dval); break;
                        case K::String: *sp++ = frame.fromUTF8String(c.sval.c_str()); break;
                        case K::Char:   *sp++ = frame.fromUnicodeChar(static_cast<unsigned>(c.ival)); break;
                        case K::Symbol: case K::SendSite:
                            throw std::logic_error("PUSH_CONST of a name constant");
                    }
                    continue;
                }
                case Op::PUSH_UNIT:  *sp++ = L.unit; continue;
                case Op::PUSH_NULL:  *sp++ = PROTO_NONE; continue;
                case Op::PUSH_TRUE:  *sp++ = PROTO_TRUE; continue;
                case Op::PUSH_FALSE: *sp++ = PROTO_FALSE; continue;
                case Op::POP: --sp; continue;
                case Op::DUP: *sp = sp[-1]; ++sp; continue;
                case Op::PUSH_LOCAL:  *sp++ = slots[operand]; continue;
                case Op::STORE_LOCAL: slots[operand] = *--sp; continue;
                case Op::MAKE_CELL:
                    slots[operand] = L.cellProto->newChild(&frame, /*isMutable=*/true);
                    continue;
                case Op::PUSH_CELL: {
                    const proto::ProtoObject* v = slots[operand]->getOwnAttributeDirect(&frame, L.valueKey);
                    *sp++ = v ? v : PROTO_NONE;
                    continue;
                }
                case Op::STORE_CELL:
                    slots[operand]->setAttribute(&frame, L.valueKey, sp[-1]);  // mutable: in place
                    --sp;
                    continue;
                case Op::PUSH_GLOBAL: {
                    const auto* key = mod.constAt(operand).symbol;
                    const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(&frame, key);
                    if ((!v || v == PROTO_NONE) &&
                        L.globals->hasOwnAttribute(&frame, key) != PROTO_TRUE) [[unlikely]]
                        throw ScalaError("UninitializedFieldError",
                                         GlobalTable::nameOfKey(mod.constAt(operand).sval) +
                                             " is used before it is initialised");
                    *sp++ = v ? v : PROTO_NONE;
                    continue;
                }
                case Op::STORE_GLOBAL:
                    L.globals->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::MAKE_FN: {
                    const BytecodeModule& sub = mod.block(operand);
                    const unsigned nc = static_cast<unsigned>(sub.captureCount());
                    const auto address = reinterpret_cast<std::intptr_t>(&sub);
                    // User-space addresses are below 2^47, inside the SmallInteger range.
                    const proto::ProtoObject* fn =
                        L.functionProtoFor(static_cast<unsigned>(sub.arity()))->newChild(&frame)
                            ->setAttribute(&frame, L.codeKey, proto::makeSmallInt(address));
                    if (nc > 0)
                        fn = fn->setAttribute(&frame, L.capturesKey,
                                              frame.newList(nc, sp - nc)->asObject(&frame));
                    sp -= nc;
                    *sp++ = fn;
                    continue;
                }
                case Op::CALL: {
                    const proto::ProtoObject** base = sp - operand - 1;
                    const proto::ProtoObject* r =
                        invoke(&frame, base[0], base + 1, static_cast<unsigned>(operand));
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
                case Op::CALL_SPREAD: {
                    const unsigned n = static_cast<unsigned>(operand);
                    const proto::ProtoObject** base = sp - n - 2;  // callee
                    const proto::ProtoObject* listObj = sp[-1];
                    if (!isListFast(listObj))
                        throw ScalaError("ClassCastException",
                                         typeName(&frame, L, listObj) + " cannot be spliced as arguments");
                    const proto::ProtoList* list = listObj->asList(&frame);
                    const unsigned extra = static_cast<unsigned>(list->getSize(&frame));
                    const proto::ProtoObject* r;
                    {
                        proto::ProtoContext argScope(frame.space, &frame);
                        argScope.resizeAutomaticLocals(n + extra);
                        const proto::ProtoObject** a = argScope.getAutomaticLocals();
                        for (unsigned k = 0; k < n; ++k) a[k] = base[1 + k];
                        for (unsigned k = 0; k < extra; ++k) a[n + k] = list->getAt(&argScope, static_cast<int>(k));
                        r = invoke(&argScope, base[0], a, n + extra);
                        argScope.returnValue = r;
                    }
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
                case Op::SEND: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // receiver
                    const proto::ProtoObject* r = send(&frame, base[0], site.symbol, base + 1, site.argc);
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
                case Op::RETURN: {
                    const proto::ProtoObject* r = sp[-1];
                    frame.returnValue = r;
                    return r;
                }
                case Op::MAKE_LAZY: {
                    const proto::ProtoObject* holder = L.lazyProto->newChild(&frame, true);
                    holder->setAttribute(&frame, L.thunkKey, sp[-1]);  // mutable: in place
                    sp[-1] = holder;
                    continue;
                }
                case Op::FORCE: sp[-1] = force(&frame, sp[-1]); continue;
                case Op::JUMP: ip += operand; continue;
                case Op::JUMP_IF_FALSE: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_FALSE) ip += operand;
                    else if (v != PROTO_TRUE) throwNotBoolean(&frame, L, v);
                    continue;
                }
                case Op::JUMP_IF_TRUE: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_TRUE) ip += operand;
                    else if (v != PROTO_FALSE) throwNotBoolean(&frame, L, v);
                    continue;
                }
                case Op::JUMP_BACK:
                    ip -= operand;
                    frame.safepoint();  // Open question Q21; every live value is in a slot
                    continue;
                case Op::ADD: case Op::SUB: case Op::MUL: {
                    const proto::ProtoObject* a = sp[-2];
                    const proto::ProtoObject* b = sp[-1];
                    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
                        long long r;
                        bool fits;
                        if (op == Op::ADD)      { r = x + y; fits = proto::smallIntInRange(r); }
                        else if (op == Op::SUB) { r = x - y; fits = proto::smallIntInRange(r); }
                        else fits = !__builtin_mul_overflow(x, y, &r) && proto::smallIntInRange(r);
                        // |x|, |y| < 2^53, so x + y and x - y cannot overflow a long long.
                        sp[-2] = fits ? proto::makeSmallInt(r)
                               : op == Op::ADD ? a->add(&frame, b)        // promotes to LargeInteger
                               : op == Op::SUB ? a->subtract(&frame, b)
                                               : a->multiply(&frame, b);
                    } else {
                        sp[-2] = slowBinary(&frame, op, a, b);
                    }
                    --sp;
                    continue;
                }
                case Op::LT: case Op::LE: case Op::GT: case Op::GE: {
                    const proto::ProtoObject* a = sp[-2];
                    const proto::ProtoObject* b = sp[-1];
                    if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                        const long long x = proto::asSmallInt(a), y = proto::asSmallInt(b);
                        const bool r = op == Op::LT ? x < y : op == Op::LE ? x <= y
                                     : op == Op::GT ? x > y : x >= y;
                        sp[-2] = r ? PROTO_TRUE : PROTO_FALSE;
                    } else {
                        sp[-2] = slowBinary(&frame, op, a, b);
                    }
                    --sp;
                    continue;
                }
                case Op::EQ: case Op::NE: {
                    const bool eq = valuesEqual(&frame, L, sp[-2], sp[-1]);
                    sp[-2] = (eq == (op == Op::EQ)) ? PROTO_TRUE : PROTO_FALSE;
                    --sp;
                    continue;
                }
                case Op::NEG: {
                    const proto::ProtoObject* a = sp[-1];
                    if (proto::isSmallInt(a) && proto::asSmallInt(a) != proto::PROTO_SMALL_INT_MIN)
                        sp[-1] = proto::makeSmallInt(-proto::asSmallInt(a));
                    else if (isNumberFast(a))
                        sp[-1] = a->negate(&frame);
                    else
                        sp[-1] = send(&frame, a,
                                      proto::ProtoString::createSymbol(&frame, "unary_-"), nullptr, 0);
                    continue;
                }
                case Op::NOT: {
                    const proto::ProtoObject* a = sp[-1];
                    if (a == PROTO_TRUE) sp[-1] = PROTO_FALSE;
                    else if (a == PROTO_FALSE) sp[-1] = PROTO_TRUE;
                    else sp[-1] = send(&frame, a,
                                       proto::ProtoString::createSymbol(&frame, "unary_!"), nullptr, 0);
                    continue;
                }
            }
            // Every handled opcode continues the loop or returns; reaching this
            // point means the module holds an opcode value the VM does not know
            // (a compiler bug or a corrupted module), which is never skipped.
            throw std::logic_error("unknown opcode " +
                                   std::to_string(static_cast<unsigned>(op)) + " in " + mod.name());
        }
    } catch (ScalaError& e) {
        if (e.line == 0) e.line = mod.lineAt(static_cast<std::size_t>(ip - code) - 1);
        throw;
    } catch (const std::runtime_error& e) {
        // A protoCore error (e.g. "Objects are not integer types for division.")
        // becomes a Scala RuntimeException; it is never swallowed (DESIGN §7).
        ScalaError se("RuntimeException", e.what());
        se.line = mod.lineAt(static_cast<std::size_t>(ip - code) - 1);
        throw se;
    }
}

const proto::ProtoObject* ExecutionEngine::slowBinary(proto::ProtoContext* ctx, Op op,
                                                      const proto::ProtoObject* a,
                                                      const proto::ProtoObject* b) {
    const RuntimeLayout& L = layout_;
    // String concatenation: either side a String (Scala String.+ / Int.+(String)).
    if (op == Op::ADD && (proto::ProtoObject::isStringTagFast(a) || proto::ProtoObject::isStringTagFast(b))) {
        const proto::ProtoObject* sa = toScalaString(ctx, L, a);
        const proto::ProtoObject* sb = toScalaString(ctx, L, b);
        return reinterpret_cast<const proto::ProtoString*>(sa)
            ->appendLast(ctx, reinterpret_cast<const proto::ProtoString*>(sb))->asObject(ctx);
    }
    // "ab" * 3
    if (op == Op::MUL && proto::ProtoObject::isStringTagFast(a) && isIntegerFast(b))
        return a->multiply(ctx, b);
    // Numbers, with Char promoted to its code point (Scala Char arithmetic yields Int).
    const proto::ProtoObject* x = widenChar(a);
    const proto::ProtoObject* y = widenChar(b);
    if (isNumberFast(x) && isNumberFast(y)) {
        switch (op) {
            case Op::ADD: return x->add(ctx, y);
            case Op::SUB: return x->subtract(ctx, y);
            case Op::MUL: return x->multiply(ctx, y);
            default: break;
        }
        const auto c = x->partialCompare(ctx, y);  // IEEE: NaN compares false
        const bool r = op == Op::LT ? c < 0 : op == Op::LE ? c <= 0 : op == Op::GT ? c > 0 : c >= 0;
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    if (op != Op::ADD && op != Op::SUB && op != Op::MUL &&
        proto::ProtoObject::isStringTagFast(a) && proto::ProtoObject::isStringTagFast(b)) {
        const auto c = a->partialCompare(ctx, b);
        const bool r = op == Op::LT ? c < 0 : op == Op::LE ? c <= 0 : op == Op::GT ? c > 0 : c >= 0;
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    // Anything else is an ordinary method call on the left operand.
    const proto::ProtoObject* argv[1] = {b};
    return send(ctx, a, proto::ProtoString::createSymbol(ctx, opSymbol(op)), argv, 1);
}

} // namespace protoScala
