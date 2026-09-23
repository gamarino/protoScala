#include "runtime/ExecutionEngine.h"
#include "compiler/GlobalTable.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"
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

// Native frames between the running bytecode frame and the current C++ frame
// (D43): 1 while a native method runs, more when a native re-entered the VM.
thread_local unsigned tl_nativeDepth = 0;

const char* opSymbol(Op op) {
    switch (op) {
        case Op::ADD: return "+";  case Op::SUB: return "-";  case Op::MUL: return "*";
        case Op::LT:  return "<";  case Op::LE:  return "<="; case Op::GT:  return ">";
        case Op::GE:  return ">="; default:      return "?";
    }
}

// "Box::value" -> "value": a private member's class-qualified key (D5) is an
// implementation detail and never appears in a message.
std::string plainName(std::string n) {
    const auto sep = n.rfind("::");
    if (sep != std::string::npos && sep > 0 && sep + 2 < n.size()) n = n.substr(sep + 2);
    return n;
}

[[noreturn, gnu::cold]] void throwNotBoolean(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                             const proto::ProtoObject* v) {
    throw ScalaError("ClassCastException", typeName(ctx, L, v) + " cannot be cast to Boolean");
}

} // namespace

const ActiveCallContext* activeCallContext() { return tl_activeSet ? &tl_active : nullptr; }

ExecutionEngine::ActiveCallGuard::ActiveCallGuard(ExecutionEngine* engine, const RuntimeLayout* layout)
    : saved_(tl_active), wasSet_(tl_activeSet) {
    tl_active = ActiveCallContext{engine, layout};
    tl_activeSet = true;
}
ExecutionEngine::ActiveCallGuard::~ActiveCallGuard() {
    tl_active = saved_;
    tl_activeSet = wasSet_;
}

unsigned ExecutionEngine::nativeReentryDepth() { return tl_nativeDepth; }
ExecutionEngine::NativeDepthGuard::NativeDepthGuard() { ++tl_nativeDepth; }
ExecutionEngine::NativeDepthGuard::~NativeDepthGuard() { --tl_nativeDepth; }

const proto::ProtoObject* ExecutionEngine::run(proto::ProtoContext* parent, const BytecodeModule& mod) {
    ActiveCallGuard guard(this, &layout_);
    return execute(parent, mod, nullptr, 0, nullptr);
}

std::string ExecutionEngine::showTopLevel(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    ActiveCallGuard guard(this, &layout_);
    return show(ctx, layout_, v);
}

const proto::ProtoObject* ExecutionEngine::callTopLevel(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* callable,
                                                        const proto::ProtoObject* const* args,
                                                        unsigned argc) {
    ActiveCallGuard guard(this, &layout_);
    return invoke(ctx, callable, args, argc);
}

const proto::ProtoObject* ExecutionEngine::callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                                      const proto::ProtoObject* self,
                                                      const proto::ProtoObject* const* args,
                                                      unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* list = scope.newList(argc, args);
    // A native that re-enters the VM raises the depth for everything it calls,
    // and the destructor lowers it on unwinding too, so a FutureYield passing
    // through does not leave it raised (D43, A0-2).
    NativeDepthGuard depth;
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
        if (m->isMethod()) {  // a bound method: its receiver travels in slot 0
            const proto::ProtoObject* self = callee->getOwnAttributeDirect(ctx, layout_.selfKey);
            if (!self) throw std::logic_error("invoke: unbound method");
            const proto::ProtoObject* caps =
                m->captureCount() ? callee->getOwnAttributeDirect(ctx, layout_.capturesKey) : nullptr;
            proto::ProtoContext scope(ctx->space, ctx);
            scope.resizeAutomaticLocals(argc + 1);
            const proto::ProtoObject** a = scope.getAutomaticLocals();
            a[0] = self;
            for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
            const proto::ProtoObject* r = execute(&scope, *m, a, argc + 1, caps);
            scope.returnValue = r;
            return r;
        }
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
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(argc + 1);
    const proto::ProtoObject** base = scope.getAutomaticLocals();
    base[0] = receiver;
    for (unsigned k = 0; k < argc; ++k) base[k + 1] = args[k];
    const proto::ProtoObject* r = dispatch(&scope, base, name, argc);
    scope.returnValue = r;
    return r;
}

// A private member is addressed by its class-qualified key (D5). A receiver
// that does not carry that key is not an instance of the class the site was
// compiled in, so the send retries under the plain name - the resolution
// Scala's static types would have made.
const proto::ProtoString* ExecutionEngine::siteName(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* receiver,
                                                    const BytecodeModule::Const& site) const {
    if (!site.keySymbol || receiver == PROTO_NONE) return site.symbol;
    return receiver->hasAttribute(ctx, site.symbol) == PROTO_TRUE ? site.symbol : site.keySymbol;
}

const proto::ProtoObject* ExecutionEngine::dispatch(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject** base,
                                                    const proto::ProtoString* name, unsigned argc,
                                                    bool applied) {
    const proto::ProtoObject* receiver = base[0];
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + plainName(name->toStdString(ctx)) + "' on null");
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE) {
        // PROTO_NONE is also a stored null: probe presence (DESIGN §4.1).
        if (receiver->hasAttribute(ctx, name) != PROTO_TRUE) throwMissingMember(ctx, receiver, name);
        if (argc == 0 && !applied) return PROTO_NONE;
        throw ScalaError("NullPointerException", "cannot call null");
    }
    // `obj.p()` where `p` is written without a parameter list: Scala applies the
    // *result* of the member (`def g = () => n; c.g()` is `c.g.apply()`), and
    // rejects the call when the result takes no arguments ("method p in class H
    // does not take parameters"). `def p() = e` is arity-1 too but not
    // paramless, so it is called directly.
    if (applied && argc == 0)
        if (const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m))
            if (mod->isMethod() && mod->isParamless() &&
                !m->getOwnAttributeDirect(ctx, layout_.selfKey)) {
                const proto::ProtoObject* r = execute(ctx, *mod, base, 1, nullptr);
                base[0] = r;  // the receiver is no longer needed; keep r rooted
                if (r != PROTO_NONE &&
                    (compiledModuleOf(ctx, layout_, r) || r->isMethod(ctx) ||
                     r->hasAttribute(ctx, layout_.applyName) == PROTO_TRUE))
                    return invoke(ctx, r, base + 1, 0);
                throw ScalaError("NoSuchMethodError", "method " + plainName(name->toStdString(ctx)) +
                                                          " does not take parameters");
            }
    return callMember(ctx, m, base, argc, applied);
}

// Calls the value `m` found for a member of the receiver base[0]: a native
// method, a Scala method (receiver in slot 0), a lazy val holder, a
// function-valued field, a plain field, or an object with `apply`.
const proto::ProtoObject* ExecutionEngine::callMember(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* m,
                                                      const proto::ProtoObject** base, unsigned argc,
                                                      bool applied) {
    const RuntimeLayout& L = layout_;
    // `select`: the site wrote no argument list, so a member that is not a
    // method yields its value instead of being applied (`c.f` vs `c.f()`).
    const bool select = argc == 0 && !applied;
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), base[0], base + 1, argc);
    if (const BytecodeModule* mod = compiledModuleOf(ctx, L, m)) {
        // A member method runs on this receiver; a bound method (the result of
        // an eta-expansion) stored in a field is an ordinary function value
        // carrying its own receiver in __self__, and falls through to invoke.
        if (mod->isMethod() && !m->getOwnAttributeDirect(ctx, L.selfKey)) {
            // `obj.m` for a method with parameters: eta-expansion (D10).
            if (select && mod->arity() > 1 && !mod->isVariadic()) return bindMethod(ctx, m, base[0]);
            return execute(ctx, *mod, base, argc + 1, nullptr);
        }
        if (select) return m;                        // a function-valued field
        return invoke(ctx, m, base + 1, argc);       // obj.f(args) = obj.f.apply(args)
    }
    if (isObjectCellFast(m) && m->getPrototype(ctx) == L.lazyProto) {  // a lazy val member
        const proto::ProtoObject* v = forceMember(ctx, m, base[0]);
        if (select) return v;
        base[0] = v;  // the receiver is no longer needed; keep v rooted
        return invoke(ctx, v, base + 1, argc);
    }
    if (select) return m;                            // a field
    return send(ctx, m, L.applyName, base + 1, argc);  // obj.x(args) with x an object: x.apply(args)
}

const proto::ProtoObject* ExecutionEngine::callWithReceiver(proto::ProtoContext* ctx,
                                                            const proto::ProtoObject* m,
                                                            const proto::ProtoObject** base,
                                                            unsigned argc) {
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), base[0], base + 1, argc);
    const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m);
    if (!mod || !mod->isMethod()) throw std::logic_error("callWithReceiver: not a method");
    return execute(ctx, *mod, base, argc + 1, nullptr);
}

// A function value for `receiver.m` (eta-expansion): a Function<N> object
// sharing the method's code and its captures, with the receiver in __self__.
const proto::ProtoObject* ExecutionEngine::bindMethod(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* method,
                                                      const proto::ProtoObject* receiver) {
    const RuntimeLayout& L = layout_;
    const BytecodeModule* mod = compiledModuleOf(ctx, L, method);
    const proto::ProtoObject* fn = L.functionProtoFor(static_cast<unsigned>(mod->arity() - 1))
                                       ->newChild(ctx)
                                       ->setAttribute(ctx, L.codeKey,
                                                      method->getOwnAttributeDirect(ctx, L.codeKey))
                                       ->setAttribute(ctx, L.selfKey, receiver);
    if (mod->captureCount() > 0)  // a method of a class declared inside a function
        fn = fn->setAttribute(ctx, L.capturesKey, method->getOwnAttributeDirect(ctx, L.capturesKey));
    return fn;
}

// A lazy val member: the holder's thunk is a method, run once with the
// receiver of the access (Open question Q12); the holder is mutable.
const proto::ProtoObject* ExecutionEngine::forceMember(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* holder,
                                                       const proto::ProtoObject* receiver) {
    const RuntimeLayout& L = layout_;
    if (holder->hasOwnAttribute(ctx, L.valueKey) == PROTO_TRUE)
        return holder->getOwnAttributeDirect(ctx, L.valueKey);
    const BytecodeModule* mod =
        compiledModuleOf(ctx, L, holder->getOwnAttributeDirect(ctx, L.thunkKey));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    scope.setAutomaticLocal(0, receiver);
    const proto::ProtoObject* v = execute(&scope, *mod, scope.getAutomaticLocals(), 1, nullptr);
    holder->setAttribute(&scope, L.valueKey, v);  // mutable holder: in place
    scope.returnValue = v;
    return v;
}

// new cls(args): base[0] = cls, base[1..argc] the arguments.
const proto::ProtoObject* ExecutionEngine::instantiate(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject** base,
                                                       const proto::ProtoString* ctorKey,
                                                       unsigned argc) {
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* cls = base[0];
    // Mutability is inherited: a subclass of a class with a `var` field also
    // builds mutable instances, so this is a chain lookup (the shape's chain
    // is the linearization, as TEST_PROTO relies on). The constructor is not:
    // inheriting a parent's <init> would skip the subclass's own fields.
    const bool mutableInstances = cls->getAttribute(ctx, L.mutableKey) == PROTO_TRUE;
    const proto::ProtoObject* init = cls->getOwnAttributeDirect(ctx, ctorKey);
    base[0] = cls->newChild(ctx, mutableInstances);  // the instance's chain keeps cls alive
    if (!init)
        throw ScalaError("IllegalArgumentException",
                         typeName(ctx, L, base[0]) + " has no constructor taking " +
                             std::to_string(argc) + " arguments");
    return callWithReceiver(ctx, init, base, argc);
}

const proto::ProtoObject* ExecutionEngine::construct(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* cls,
                                                     const proto::ProtoObject* const* args,
                                                     unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(argc + 1);
    const proto::ProtoObject** a = scope.getAutomaticLocals();
    a[0] = cls;
    for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
    const proto::ProtoObject* r = instantiate(&scope, a, layout_.initKey, argc);
    scope.returnValue = r;
    return r;
}

// MAKE_CLASS: an immutable prototype whose chain is exactly the pushed
// parents (the linearization without the class, Design notes 1-2), carrying
// the members, the class metadata and its membership marker.
const proto::ProtoObject* ExecutionEngine::makeClass(proto::ProtoContext* ctx,
                                                     const BytecodeModule::Const& spec,
                                                     const proto::ProtoObject* const* base) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);  // every intermediate shape is young in `scope`
    const proto::ProtoList* chain = scope.newList(spec.argc, base);
    const proto::ProtoObject* shape = L.anyProto->newChild(&scope, false)->setParents(&scope, chain);
    for (std::size_t k = 0; k < spec.nameSymbols.size(); ++k)
        shape = shape->setAttribute(&scope, spec.nameSymbols[k], base[spec.argc + k]);
    shape = shape->setAttribute(&scope, spec.keySymbol, PROTO_TRUE);
    shape = shape->setAttribute(&scope, L.nameKey, makeString(&scope, spec.sval));
    if (spec.flags & BytecodeModule::kClassMutableInstances)
        shape = shape->setAttribute(&scope, L.mutableKey, PROTO_TRUE);
    if (spec.flags & BytecodeModule::kClassCase) {
        shape = shape->setAttribute(&scope, L.prefixKey, makeString(&scope, spec.sval));
        if (!(spec.flags & BytecodeModule::kClassCaseObject)) {
            const proto::ProtoList* fields = scope.newList();
            for (const proto::ProtoString* f : spec.fieldSymbols)
                fields = fields->appendLast(&scope, f->asObject(&scope));
            shape = shape->setAttribute(&scope, L.fieldsKey, fields->asObject(&scope));
        }
    }
    scope.returnValue = shape;
    return shape;
}

const proto::ProtoObject* ExecutionEngine::makeTuple(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* const* elems,
                                                     unsigned n) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoObject* t = L.tupleProto[n]->newChild(&scope, false);
    for (unsigned k = 0; k < n; ++k) t = t->setAttribute(&scope, L.tupleFieldKey[k + 1], elems[k]);
    scope.returnValue = t;
    return t;
}

// super.m(args) in a method defined by the template site.key (DESIGN §4.4):
// the first definition of m after that template in the receiver's
// linearization, found by pointer identity (O(n), R6).
const proto::ProtoObject* ExecutionEngine::superSend(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject** base,
                                                     const BytecodeModule::Const& site) {
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* owner = L.globals->getOwnAttributeDirect(ctx, site.keySymbol);
    const proto::ProtoList* chain = base[0]->getParents(ctx);  // young in ctx
    const unsigned long n = chain->getSize(ctx);
    const std::string ownerName = GlobalTable::nameOfKey(site.key.substr(1));
    unsigned long k = 0;
    while (k < n && chain->getAt(ctx, static_cast<int>(k)) != owner) ++k;
    if (k == n)
        throw ScalaError("NoSuchMethodError",
                         ownerName + " is not in the linearization of " +
                             typeName(ctx, L, base[0]) + ", so super." + site.sval +
                             " has no meaning here");
    for (++k; k < n; ++k) {
        const proto::ProtoObject* m =
            chain->getAt(ctx, static_cast<int>(k))->getOwnAttributeDirect(ctx, site.symbol);
        if (m) return callMember(ctx, m, base, site.argc);
    }
    throw ScalaError("NoSuchMethodError",
                     "super." + site.sval + " has no implementation after " + ownerName);
}

// Named arguments reach native methods (Product.copy) through protoCore's
// keyword ProtoSparseList, keyed by the interned name (DESIGN §5.2); Scala
// methods take them in a later phase (Open question Q9).
const proto::ProtoObject* ExecutionEngine::sendKeywords(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject** base,
                                                        const BytecodeModule::Const& site) {
    const proto::ProtoObject* receiver = base[0];
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + plainName(site.sval) + "' on null");
    const proto::ProtoString* name = siteName(ctx, receiver, site);
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE) throwMissingMember(ctx, receiver, name);
    if (!m->isMethod(ctx))
        throw ScalaError("UnsupportedOperationException",
                         "named arguments are not supported yet for methods written in Scala (" +
                             site.sval + ")");
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* positional = scope.newList(site.argc, base + 1);
    const proto::ProtoSparseList* keywords = scope.newSparseList();
    for (std::size_t k = 0; k < site.nameSymbols.size(); ++k)
        keywords = keywords->setAt(&scope, reinterpret_cast<unsigned long>(site.nameSymbols[k]),
                                   base[1 + site.argc + k]);
    const proto::ProtoObject* r = m->asMethod(ctx)(&scope, receiver, nullptr, positional, keywords);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}

bool ExecutionEngine::testType(proto::ProtoContext* ctx, TypeCode code,
                               const proto::ProtoObject* v) const {
    const RuntimeLayout& L = layout_;
    const bool isBool = v == PROTO_TRUE || v == PROTO_FALSE;
    const bool anyVal = isNumberFast(v) || isCharFast(v) || isBool || v == L.unit;
    switch (code) {
        case TypeCode::Integer:  return isIntegerFast(v);
        case TypeCode::Double:   return isDoubleFast(v);
        case TypeCode::Boolean:  return isBool;
        case TypeCode::Char:     return isCharFast(v);
        case TypeCode::String:   return proto::ProtoObject::isStringTagFast(v);
        case TypeCode::Unit:     return v == L.unit;
        case TypeCode::List:     return isListFast(v);
        case TypeCode::ConsList: return isListFast(v) && v->asList(ctx)->getSize(ctx) > 0;
        case TypeCode::Function: return v != PROTO_NONE && (compiledModuleOf(ctx, L, v) || v->isMethod(ctx));
        case TypeCode::AnyRef:   return v != PROTO_NONE && !anyVal;
        case TypeCode::AnyVal:   return anyVal;
        case TypeCode::Null:     return v == PROTO_NONE;
        case TypeCode::NonNull:  return v != PROTO_NONE;
        case TypeCode::Nothing:  return false;
    }
    return false;
}

void ExecutionEngine::throwMissingMember(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                         const proto::ProtoString* name) const {
    // A private member's key is "<Class>::<name>": report the name.
    std::string n = plainName(name->toStdString(ctx));
    // `x_=` on an object that has `x`: an assignment to a val.
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "_=") == 0) {
        const std::string field = n.substr(0, n.size() - 2);
        if (receiver->hasAttribute(ctx, proto::ProtoString::createSymbol(ctx, field.c_str())) == PROTO_TRUE)
            throw ScalaError("NoSuchMethodError", "Reassignment to val " + field);
    }
    throw ScalaError("NoSuchMethodError",
                     "value " + n + " is not a member of " + typeName(ctx, layout_, receiver));
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
    if (mod.isVariadic() ? argc < fixed : argc != arity) {
        const unsigned self = mod.isMethod() ? 1 : 0;
        throw ScalaError("IllegalArgumentException",
                         "wrong number of arguments for " + mod.name() + ": expected " +
                         std::to_string(fixed - self) + (mod.isVariadic() ? " or more" : "") +
                         ", got " + std::to_string(argc - self));
    }
    const unsigned stackBase = arity + static_cast<unsigned>(mod.localCount());

    proto::ProtoContext frame(parent->space, parent);
    frame.resizeAutomaticLocals(stackBase + static_cast<unsigned>(mod.maxStack()));
    const proto::ProtoObject** slots = frame.getAutomaticLocals();  // never resized again
    for (unsigned k = 0; k < fixed; ++k) slots[k] = args[k];
    if (mod.isVariadic())
        slots[fixed] = frame.newList(argc - fixed, args + fixed)->asObject(&frame);
    if (mod.captureCount() > 0) {
        // A module with captures is never run without them: filling its
        // capture slots with nulls would corrupt the frame silently. Scala
        // methods (callMember, callWithReceiver) pass none, so a class
        // declared inside a function whose members read an enclosing local
        // fails loudly here instead.
        if (!captures)
            throw std::logic_error(mod.name() + " needs " + std::to_string(mod.captureCount()) +
                                   " captured value(s) but was called without any");
        const proto::ProtoList* caps = captures->asList(&frame);
        const auto& specs = mod.captureSpecs();
        for (std::size_t k = 0; k < specs.size(); ++k)
            slots[specs[k].localSlot] = caps->getAt(&frame, static_cast<int>(k));
    }

    const proto::ProtoObject* out = runLoop(frame, mod, slots, slots + stackBase, mod.code().data());
    frame.returnValue = out;
    return out;
}

// One record of a suspended chain: where to continue, and the values that were
// live below the in-flight call. Everything above the call's base is dead --
// it was the callee's arguments, which the callee consumed.
static void appendSuspendedFrame(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                 const BytecodeModule& mod, unsigned ipOffset, unsigned pendingBase,
                                 const proto::ProtoObject** slots) {
    auto* actor = const_cast<proto::ProtoObject*>(currentActor());
    if (!actor)
        throw ScalaError("UnsupportedOperationException",
                         "await suspended outside an actor turn");
    proto::ProtoContext scope(ctx->space, ctx);
    auto* rec = const_cast<proto::ProtoObject*>(L.frameProto->newChild(&scope, /*isMutable=*/true));
    scope.returnValue = rec;
    // The module address travels as a SmallInteger, as MAKE_FN already does;
    // modules are owned by the Session for the whole session, so it cannot dangle.
    rec->setAttribute(&scope, L.modKey,
                      scope.fromInteger(static_cast<long long>(reinterpret_cast<std::intptr_t>(&mod))));
    rec->setAttribute(&scope, L.ipKey, proto::makeSmallInt(ipOffset));
    rec->setAttribute(&scope, L.fbaseKey, proto::makeSmallInt(pendingBase));
    rec->setAttribute(&scope, L.fslotsKey, scope.newList(pendingBase, slots)->asObject(&scope));
    const proto::ProtoObject* cur = actor->getOwnAttributeDirect(&scope, L.snapshotKey);
    actor->setAttribute(&scope, L.snapshotKey,
                        cur->asList(&scope)->appendFirst(&scope, rec)->asObject(&scope));
}

const proto::ProtoObject* ExecutionEngine::resumeFrames(proto::ProtoContext* parent,
                                                        const proto::ProtoList* frames,
                                                        unsigned idx,
                                                        const proto::ProtoObject* injected) {
    checkNativeStack();
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* rec = frames->getAt(parent, static_cast<int>(idx));
    const BytecodeModule& mod = *reinterpret_cast<const BytecodeModule*>(
        static_cast<std::intptr_t>(rec->getOwnAttributeDirect(parent, L.modKey)->asLong(parent)));
    const auto ipOffset = static_cast<std::size_t>(
        proto::asSmallInt(rec->getOwnAttributeDirect(parent, L.ipKey)));
    const auto base = static_cast<unsigned>(
        proto::asSmallInt(rec->getOwnAttributeDirect(parent, L.fbaseKey)));
    const proto::ProtoList* saved =
        rec->getOwnAttributeDirect(parent, L.fslotsKey)->asList(parent);

    const unsigned stackBase =
        static_cast<unsigned>(mod.arity()) + static_cast<unsigned>(mod.localCount());
    proto::ProtoContext frame(parent->space, parent);
    frame.resizeAutomaticLocals(stackBase + static_cast<unsigned>(mod.maxStack()));
    const proto::ProtoObject** slots = frame.getAutomaticLocals();
    for (unsigned k = 0; k < base; ++k) slots[k] = saved->getAt(&frame, static_cast<int>(k));

    // The inner frames finish first; their result is what this frame's
    // in-flight call would have returned. The innermost frame receives the
    // awaited value itself.
    const proto::ProtoObject* r = (idx + 1 < frames->getSize(&frame))
                                      ? resumeFrames(&frame, frames, idx + 1, injected)
                                      : injected;
    slots[base] = r;
    const proto::ProtoObject* out =
        runLoop(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset);
    frame.returnValue = out;
    return out;
}

const proto::ProtoObject* ExecutionEngine::runLoop(proto::ProtoContext& frame,
                                                   const BytecodeModule& mod,
                                                   const proto::ProtoObject** slots,
                                                   const proto::ProtoObject** sp,
                                                   const Instr* ip) {
    const Instr* const code = mod.code().data();
    const RuntimeLayout& L = layout_;
    // The operand-stack index where the in-flight call will write its result,
    // or kNoPendingCall when this frame is not inside a call. Two stores per
    // call opcode; it is what makes a frame resumable after a cooperative
    // yield (DESIGN §8.3, D43).
    unsigned pendingBase = kNoPendingCall;
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
                        case K::String: *sp++ = makeString(&frame, c.sval); break;  // may hold NUL
                        case K::Char:   *sp++ = frame.fromUnicodeChar(static_cast<unsigned>(c.ival)); break;
                        case K::Symbol: case K::SendSite: case K::Names:
                        case K::ClassSpec: case K::SuperSite: case K::KwSendSite:
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
                    pendingBase = static_cast<unsigned>(base - slots);
                    const proto::ProtoObject* r =
                        invoke(&frame, base[0], base + 1, static_cast<unsigned>(operand));
                    pendingBase = kNoPendingCall;
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
                    pendingBase = static_cast<unsigned>(base - slots);
                    {
                        proto::ProtoContext argScope(frame.space, &frame);
                        argScope.resizeAutomaticLocals(n + extra);
                        const proto::ProtoObject** a = argScope.getAutomaticLocals();
                        for (unsigned k = 0; k < n; ++k) a[k] = base[1 + k];
                        for (unsigned k = 0; k < extra; ++k) a[n + k] = list->getAt(&argScope, static_cast<int>(k));
                        r = invoke(&argScope, base[0], a, n + extra);
                        argScope.returnValue = r;
                    }
                    pendingBase = kNoPendingCall;
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
                case Op::SEND:
                case Op::SEND_APPLY: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // receiver
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = dispatch(&frame, base, siteName(&frame, base[0], site), site.argc,
                                       op == Op::SEND_APPLY);
                    pendingBase = kNoPendingCall;
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
                case Op::FORCE:
                    pendingBase = static_cast<unsigned>(sp - 1 - slots);
                    sp[-1] = force(&frame, sp[-1]);
                    pendingBase = kNoPendingCall;
                    continue;
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
                case Op::MAKE_CLASS: {
                    const auto& spec = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - spec.argc - static_cast<unsigned>(spec.nameSymbols.size());
                    base[0] = makeClass(&frame, spec, base);
                    sp = base + 1;
                    continue;
                }
                case Op::NEW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // [cls a1..an]
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = instantiate(&frame, base, site.symbol, site.argc);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::NEW_SPREAD: {
                    const auto& site = mod.constAt(operand);
                    const unsigned n = site.argc;
                    const proto::ProtoObject** base = sp - n - 2;  // [cls a1..an list]
                    const proto::ProtoObject* listObj = sp[-1];
                    if (!isListFast(listObj))
                        throw ScalaError("ClassCastException",
                                         typeName(&frame, L, listObj) +
                                             " cannot be spliced as arguments");
                    const proto::ProtoList* list = listObj->asList(&frame);
                    const unsigned extra = static_cast<unsigned>(list->getSize(&frame));
                    const proto::ProtoObject* r;
                    pendingBase = static_cast<unsigned>(base - slots);
                    {
                        proto::ProtoContext argScope(frame.space, &frame);
                        argScope.resizeAutomaticLocals(n + extra + 1);
                        const proto::ProtoObject** a = argScope.getAutomaticLocals();
                        for (unsigned k = 0; k <= n; ++k) a[k] = base[k];
                        for (unsigned k = 0; k < extra; ++k)
                            a[n + 1 + k] = list->getAt(&argScope, static_cast<int>(k));
                        r = instantiate(&argScope, a, site.symbol, n + extra);
                        argScope.returnValue = r;
                    }
                    pendingBase = kNoPendingCall;
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
                case Op::INVOKE_INIT: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 2;  // [cls this a1..an]
                    const proto::ProtoObject* init = base[0]->getOwnAttributeDirect(&frame, site.symbol);
                    if (!init) throw std::logic_error("INVOKE_INIT: no initialiser " + site.sval);
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = callWithReceiver(&frame, init, base + 1, site.argc);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::STORE_FIELD:  // constructors: `this` is slot 0 (Design note 10)
                    slots[0] = slots[0]->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::SET_FIELD: {  // setters of var fields: the instance is mutable
                    const proto::ProtoObject* obj = sp[-2];
                    if (obj->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]) != obj)
                        throw ScalaError("UnsupportedOperationException",
                                         "cannot assign a field of an immutable object");
                    sp -= 2;
                    continue;
                }
                case Op::SEND_SUPER: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // [this a1..an]
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = superSend(&frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::TEST_TYPE:
                    sp[-1] = testType(&frame, static_cast<TypeCode>(operand), sp[-1]) ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                case Op::TEST_PROTO: {  // class membership by marker (Design note 5)
                    const proto::ProtoObject* v = sp[-1];
                    sp[-1] = v != PROTO_NONE &&
                                     v->getAttribute(&frame, mod.constAt(operand).symbol) == PROTO_TRUE
                                 ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                }
                case Op::UNAPPLY_FIELDS: {
                    const auto& names = mod.constAt(operand);
                    const proto::ProtoObject* v = *--sp;  // also held by the match's scrutinee slot
                    for (const proto::ProtoString* key : names.nameSymbols) {
                        const proto::ProtoObject* f = v->getOwnAttributeDirect(&frame, key);
                        *sp++ = f ? f : PROTO_NONE;
                    }
                    continue;
                }
                case Op::UNCONS: {
                    const proto::ProtoList* list = sp[-1]->asList(&frame);
                    sp[-1] = list->getAt(&frame, 0);
                    *sp++ = list->removeFirst(&frame)->asObject(&frame);
                    continue;
                }
                case Op::MATCH_ERROR: {
                    const proto::ProtoObject* v = sp[-1];
                    throw ScalaError("MatchError", show(&frame, L, v) + " (of class " + typeName(&frame, L, v) + ")");
                }
                case Op::CAST_FAIL:
                    throw ScalaError("ClassCastException", typeName(&frame, L, sp[-1]) +
                                                               " cannot be cast to " + mod.constAt(operand).sval);
                case Op::MAKE_TUPLE: {
                    const unsigned n = static_cast<unsigned>(operand);
                    if (n < 2 || n > kMaxTupleArity)  // Tuple2..Tuple22 only
                        throw std::logic_error("MAKE_TUPLE of arity " + std::to_string(n) + " in " +
                                               mod.name());
                    const proto::ProtoObject** base = sp - n;
                    base[0] = makeTuple(&frame, base, n);
                    sp = base + 1;
                    continue;
                }
                case Op::SEND_KW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = sendKeywords(&frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
            }
            // Every handled opcode continues the loop or returns; reaching this
            // point means the module holds an opcode value the VM does not know
            // (a compiler bug or a corrupted module), which is never skipped.
            throw std::logic_error("unknown opcode " +
                                   std::to_string(static_cast<unsigned>(op)) + " in " + mod.name());
        }
    } catch (FutureYield&) {
        // This frame is part of a suspended chain. Record it and rethrow; the
        // frames prepend themselves, so the actor's list reads outermost-first
        // (the innermost frame is the first to catch).
        if (pendingBase == kNoPendingCall)
            throw ScalaError("UnsupportedOperationException",
                             "await is not supported here: " + mod.name() +
                                 " cannot be suspended at this instruction");
        appendSuspendedFrame(&frame, layout_, mod, static_cast<unsigned>(ip - code), pendingBase,
                             slots);
        throw;
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
