#include "runtime/ExecutionEngine.h"
#include "compiler/GlobalTable.h"
#include "compiler/BytecodeModule.h"
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"
#include "runtime/StackGuard.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <cmath>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

namespace {

thread_local ActiveCallContext tl_active{};
thread_local bool tl_activeSet = false;

// Native frames between the running bytecode frame and the current C++ frame
// (D43): 1 while a native method runs, more when a native re-entered the VM.
thread_local unsigned tl_nativeDepth = 0;

// The interned method name of a binary operator, read from the layout rather
// than interned per execution (Phase 3, Task 1 Step 3). `?` cannot occur: the
// dispatch loop only reaches slowBinary for these seven opcodes.
const proto::ProtoString* binaryOpName(const RuntimeLayout& L, Op op) {
    switch (op) {
        case Op::ADD: return L.binaryOpName[0]; case Op::SUB: return L.binaryOpName[1];
        case Op::MUL: return L.binaryOpName[2]; case Op::LT:  return L.binaryOpName[3];
        case Op::LE:  return L.binaryOpName[4]; case Op::GT:  return L.binaryOpName[5];
        case Op::GE:  return L.binaryOpName[6];
        default: throw std::logic_error("slowBinary: not a binary operator opcode");
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

namespace {

// The keyword ProtoSparseList of a SEND_KW / CALL_KW site.
//
// The key is the ADDRESS of the interned parameter-name symbol — protoCore's
// own calling convention, the same one every runtime in the family speaks
// (DESIGN §5.2). It is sound because interning guarantees exactly one address
// per name in a ProtoSpace, so the address IS the name's identity, and an
// interned string is PERENNIAL: never collected, never moved, so there is
// nothing for the collector to trace and an integer-keyed ProtoSparseList is
// exactly the right structure rather than a compromise. ProtoMap exists for
// arbitrary COLLECTABLE object keys, which is a different problem — the same
// principle that makes ProtoTuple interned and perennial.
const proto::ProtoSparseList* keywordsOfSite(proto::ProtoContext* ctx,
                                             const proto::ProtoObject** base,
                                             const BytecodeModule::Const& site) {
    const proto::ProtoSparseList* keywords = ctx->newSparseList();
    for (std::size_t k = 0; k < site.nameSymbols.size(); ++k)
        keywords = keywords->setAt(ctx, reinterpret_cast<unsigned long>(site.nameSymbols[k]),
                                   base[1 + site.argc + k]);
    return keywords;
}

// The name of the first keyword key that is not one of the callee's parameter
// names. Recovering the name from the key is a cast, and that is only sound
// because the key IS an interned symbol's address (see keywordsOfSite).
std::string unmatchedKeywordName(proto::ProtoContext* ctx, const BytecodeModule& mod,
                                 const proto::ProtoSparseList* keywords) {
    const auto& symbols = mod.paramNameSymbols();
    const proto::ProtoSparseListIterator* it = keywords->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const unsigned long key = it->nextKey(ctx);
        bool known = false;
        for (const proto::ProtoString* s : symbols)
            if (reinterpret_cast<unsigned long>(s) == key) { known = true; break; }
        if (!known) return reinterpret_cast<const proto::ProtoString*>(key)->toStdString(ctx);
        // `advance` is a non-const member of ProtoSparseListIterator, and
        // getIterator hands back a const pointer: the cast is protoCore's own
        // iteration idiom, not a mutation of a shared structure.
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
    }
    return "?";
}

}  // namespace

void ExecutionEngine::bindKeywordsAndDefaults(proto::ProtoContext& frame, const BytecodeModule& mod,
                                              const proto::ProtoSparseList* keywords,
                                              const proto::ProtoObject** slots,
                                              unsigned positional) {
    const auto& symbols = mod.paramNameSymbols();
    const auto& names = mod.paramNames();
    const std::size_t n = symbols.size();
    if (n != names.size())
        throw std::logic_error("bindKeywordsAndDefaults: unlinked module " + mod.name());
    // A callable with no recorded parameter names cannot bind by name at all;
    // that is a loud error rather than a silent drop.
    if (n == 0 && keywords && keywords->getSize(&frame) > 0)
        throw ScalaError("IllegalArgumentException",
                         mod.name() + " does not take named arguments");
    // Small and bounded: a Scala parameter list never reaches a size where a
    // heap allocation would be cheaper than this stack vector.
    std::vector<bool> filled(n, false);
    for (unsigned k = 0; k < positional && k < n; ++k) filled[k] = true;
    if (keywords) {
        unsigned long matched = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto key = reinterpret_cast<unsigned long>(symbols[i]);
            if (!keywords->has(&frame, key)) continue;
            ++matched;
            if (filled[i])
                throw ScalaError("IllegalArgumentException",
                                 mod.name() + " received parameter '" + names[i] + "' twice");
            slots[i] = keywords->getAt(&frame, key);
            filled[i] = true;
        }
        // A key that matched no parameter is a caller mistake and must be LOUD:
        // silently dropping it is exactly the failure mode a non-interning key
        // would produce, and it must not be reachable from this side either.
        if (matched != keywords->getSize(&frame))
            throw ScalaError("IllegalArgumentException",
                             mod.name() + " has no parameter named '" +
                                 unmatchedKeywordName(&frame, mod, keywords) + "'");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (filled[i]) continue;
        const std::size_t block = mod.defaultBlock(i);
        if (block == BytecodeModule::kNoDefault)
            throw ScalaError("IllegalArgumentException",
                             mod.name() + " is missing argument '" + names[i] + "'");
        // Defaults run in the callee prologue, in PARAMETER order, and a default
        // may read the parameters declared before it: the default block takes
        // exactly those slots as its own arguments, which is why they must
        // already be bound and live in the frame's traced slots (plan A0-11).
        slots[i] = execute(&frame, mod.block(block), slots, static_cast<unsigned>(i), nullptr);
        filled[i] = true;
    }
}

// Named arguments reach every callable through protoCore's keyword
// ProtoSparseList, keyed by the address of the interned parameter-name symbol
// (DESIGN §5.2, plan A0-12) — native methods, Scala-defined methods and, once
// UMD lands, foreign callables alike, with no special case for any of them.
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
    proto::ProtoContext scope(ctx->space, ctx);
    if (const BytecodeModule* mod = compiledModuleOf(&scope, layout_, m)) {
        if (mod->isMethod() && !m->getOwnAttributeDirect(&scope, layout_.selfKey)) {
            // A method of the receiver: `this` travels in slot 0, so the
            // positional arguments start at base[0] (the receiver itself).
            const proto::ProtoSparseList* keywords = keywordsOfSite(&scope, base, site);
            const proto::ProtoObject* r = execute(&scope, *mod, base, site.argc + 1, nullptr, keywords);
            scope.returnValue = r;
            return r;
        }
        // A function-valued member (or a bound method): call it as a value.
        const proto::ProtoSparseList* keywords = keywordsOfSite(&scope, base, site);
        const proto::ProtoObject* caps =
            mod->captureCount() ? m->getOwnAttributeDirect(&scope, layout_.capturesKey) : nullptr;
        const proto::ProtoObject* r =
            mod->isMethod()
                ? execute(&scope, *mod, base, site.argc + 1, caps, keywords)
                : execute(&scope, *mod, base + 1, site.argc, caps, keywords);
        scope.returnValue = r;
        return r;
    }
    if (!m->isMethod(ctx))
        throw ScalaError("IllegalArgumentException",
                         plainName(site.sval) + " is not a method of " +
                             typeName(ctx, layout_, receiver) + ", so it takes no named arguments");
    const proto::ProtoList* positional = scope.newList(site.argc, base + 1);
    const proto::ProtoSparseList* keywords = keywordsOfSite(&scope, base, site);
    const proto::ProtoObject* r = m->asMethod(ctx)(&scope, receiver, nullptr, positional, keywords);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}

// CALL_KW: `f(x = 1)` where `f` is a function value, a global `def` or a local
// function. base[0] is the callee, base[1..argc] the positional arguments and
// the keyword values follow them.
const proto::ProtoObject* ExecutionEngine::callKeywords(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject** base,
                                                        const BytecodeModule::Const& site) {
    const proto::ProtoObject* callee = base[0];
    if (callee == PROTO_NONE) throw ScalaError("NullPointerException", "cannot call null");
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoSparseList* keywords = keywordsOfSite(&scope, base, site);
    if (const BytecodeModule* mod = compiledModuleOf(&scope, layout_, callee)) {
        const proto::ProtoObject* caps =
            mod->captureCount() ? callee->getOwnAttributeDirect(&scope, layout_.capturesKey) : nullptr;
        const proto::ProtoObject* r;
        if (mod->isMethod()) {
            // A bound method: its receiver travels in slot 0.
            const proto::ProtoObject* self =
                callee->getOwnAttributeDirect(&scope, layout_.selfKey);
            if (!self) throw std::logic_error("callKeywords: unbound method");
            proto::ProtoContext argScope(scope.space, &scope);
            argScope.resizeAutomaticLocals(site.argc + 1);
            const proto::ProtoObject** a = argScope.getAutomaticLocals();
            a[0] = self;
            for (unsigned k = 0; k < site.argc; ++k) a[k + 1] = base[1 + k];
            r = execute(&argScope, *mod, a, site.argc + 1, caps, keywords);
            argScope.returnValue = r;
        } else {
            r = execute(&scope, *mod, base + 1, site.argc, caps, keywords);
        }
        scope.returnValue = r;
        return r;
    }
    if (callee->isMethod(ctx)) {
        const proto::ProtoList* positional = scope.newList(site.argc, base + 1);
        const proto::ProtoObject* r = callee->asMethod(ctx)(
            &scope, callee->asMethodSelf(ctx), nullptr, positional, keywords);
        if (!r) r = PROTO_NONE;
        scope.returnValue = r;
        return r;
    }
    // Any other value: `f(x = 1)` is `f.apply(x = 1)` (DESIGN §5.1).
    BytecodeModule::Const applySite = site;
    applySite.sval = "apply";
    applySite.symbol = layout_.applyName;
    applySite.key.clear();
    applySite.keySymbol = nullptr;
    return sendKeywords(ctx, base, applySite);
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
        // The one interning call left in this file, and deliberately: this runs
        // once, immediately before throwing, and the symbol already exists
        // whenever the answer is `true` (the class interned it when it declared
        // the field), so it allocates nothing on the path that matters.
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
                                                   const proto::ProtoObject* captures,
                                                   const proto::ProtoSparseList* keywords) {
    checkNativeStack();
    const unsigned arity = static_cast<unsigned>(mod.arity());
    const unsigned fixed = mod.isVariadic() ? arity - 1 : arity;
    // A call that passes fewer positional arguments than the callee declares is
    // acceptable when keyword arguments or defaults can fill the rest; the
    // prologue then raises a message that names the missing parameter, which is
    // strictly more informative than the count below (plan A0-11).
    const bool mayBind = !mod.isVariadic() && (keywords != nullptr || mod.hasDefaults());
    const bool ok = mod.isVariadic() ? argc >= fixed
                    : mayBind        ? argc <= arity
                                     : argc == arity;
    if (!ok) {
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
    const unsigned positional = argc < fixed ? argc : fixed;
    for (unsigned k = 0; k < positional; ++k) slots[k] = args[k];
    if (mod.isVariadic())
        slots[fixed] = frame.newList(argc - fixed, args + fixed)->asObject(&frame);
    // Named arguments and defaults are bound HERE, in the callee: a caller that
    // does not know its callee statically therefore still works. The platform
    // is late-binding even where Scala is not; late detection is accepted,
    // silent failure is not (plan A0-11).
    if (keywords || (mod.hasDefaults() && positional < fixed))
        bindKeywordsAndDefaults(frame, mod, keywords, slots, positional);
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

    // runFrame, never runLoop: the module's handler table must be active for
    // this frame, and the handler body must run outside the C++ catch (A0-3).
    const proto::ProtoObject* out = runFrame(frame, mod, slots, slots + stackBase, mod.code().data());
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
                                                        const proto::ProtoObject* injected,
                                                        const proto::ProtoObject* injectedThrow) {
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

    // The inner frames finish first; their result is what this frame's in-flight
    // call would have returned. The innermost frame receives the awaited value
    // itself — or, when the future failed, raises it AT the await's call site so
    // an enclosing try in the suspended handler can catch it (plan A0-7, 5).
    //
    // runFrame, not runLoop: without this a resumed frame would have no handler
    // table active and `try { f.await } catch { ... }` would silently catch
    // nothing (plan A0-7 invariant 3). That is the single line whose omission
    // breaks Phase 5 invisibly.
    if (idx + 1 < frames->getSize(&frame)) {
        const proto::ProtoObject* r =
            resumeFrames(&frame, frames, idx + 1, injected, injectedThrow);
        slots[base] = r;
        const proto::ProtoObject* out =
            runFrame(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset);
        frame.returnValue = out;
        return out;
    }
    if (injectedThrow) {
        const proto::ProtoObject* out =
            runFrameRaising(frame, mod, slots, ipOffset, injectedThrow);
        frame.returnValue = out;
        return out;
    }
    slots[base] = injected;
    const proto::ProtoObject* out =
        runFrame(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset);
    frame.returnValue = out;
    return out;
}

// --- Exceptions: the per-frame retry loop (DESIGN §7, plan A0-2 .. A0-4) ----

const proto::ProtoObject** ExecutionEngine::enterHandler(const BytecodeModule& mod,
                                                         const proto::ProtoObject** slots,
                                                         const BytecodeModule::Handler& h,
                                                         const proto::ProtoObject* value,
                                                         const Instr** ip) {
    const unsigned stackBase =
        static_cast<unsigned>(mod.arity()) + static_cast<unsigned>(mod.localCount());
    slots[h.slot] = value;                       // the caught value, in a traced slot
    *ip = mod.code().data() + h.handlerPc;
    return slots + stackBase + h.stackDepth;     // back to the try's entry depth
}

const proto::ProtoObject* ExecutionEngine::materialiseError(proto::ProtoContext* ctx,
                                                            const ScalaError& e) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const proto::ProtoObject** slot = scope.getAutomaticLocals();
    // A named prelude class if there is one, else RuntimeException carrying the
    // original class name in its message, so nothing is ever lost (plan A0-6).
    auto it = L.hooks.throwableClasses.find(e.className());
    const bool known = it != L.hooks.throwableClasses.end();
    const proto::ProtoObject* cls = known ? it->second : L.hooks.runtimeException;
    if (!cls)
        throw std::logic_error("materialiseError: the prelude exception classes are not bound");
    // The message string is written into a traced slot before the constructor
    // runs: `construct` allocates, and an accumulator held only in a C++ local
    // is the Phase 5 Mailbox::push bug.
    slot[0] = makeString(&scope, known ? e.message() : e.className() + ": " + e.message());
    const proto::ProtoObject* r = construct(&scope, cls, slot, 1);
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* ExecutionEngine::materialise(proto::ProtoContext* ctx,
                                                       const ScalaError& e) {
    return materialiseError(ctx, e);
}

const proto::ProtoObject* ExecutionEngine::runFrame(proto::ProtoContext& frame,
                                                    const BytecodeModule& mod,
                                                    const proto::ProtoObject** slots,
                                                    const proto::ProtoObject** sp,
                                                    const Instr* ip) {
    // Fast path: a module with no protected region can never enter a handler, so
    // it pays nothing beyond the one C++ try region a zero-cost-exceptions ABI
    // makes free on the non-throwing path.
    for (;;) {
        std::size_t faultPc = 0;
        try {
            return runLoop(frame, mod, slots, sp, ip, &faultPc);
        } catch (ScalaThrow& t) {
            // Re-root before anything allocates: the frames below this one have
            // already been destroyed, so this context's returnValue is the only
            // thing the collector can see the payload through (plan A0-2, E1).
            frame.returnValue = t.value;
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (!h) throw;
            sp = enterHandler(mod, slots, *h, t.value, &ip);
            continue;   // leaves the catch block: the handler body then runs with
                        // NO live C++ handler, so it suspends like any other code
        } catch (ScalaError& e) {
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (!h) throw;                       // the uncaught path allocates nothing
            const proto::ProtoObject* v = materialiseError(&frame, e);
            frame.returnValue = v;
            sp = enterHandler(mod, slots, *h, v, &ip);
            continue;
        }
    }
}

const proto::ProtoObject* ExecutionEngine::runFrameRaising(proto::ProtoContext& frame,
                                                           const BytecodeModule& mod,
                                                           const proto::ProtoObject** slots,
                                                           std::size_t ipOffset,
                                                           const proto::ProtoObject* payload) {
    // ipOffset is where the snapshot said to continue, i.e. just past the call
    // instruction that was in flight; that instruction's word index is
    // ipOffset - 1, which is the index runLoop's own catch reports (plan A0-3).
    frame.returnValue = payload;
    const BytecodeModule::Handler* h = mod.handlerFor(ipOffset - 1);
    if (!h) throw ScalaThrow(payload);
    const Instr* ip = nullptr;
    const proto::ProtoObject** newSp = enterHandler(mod, slots, *h, payload, &ip);
    return runFrame(frame, mod, slots, newSp, ip);
}

const proto::ProtoObject* ExecutionEngine::runLoop(proto::ProtoContext& frame,
                                                   const BytecodeModule& mod,
                                                   const proto::ProtoObject** slots,
                                                   const proto::ProtoObject** sp,
                                                   const Instr* ip,
                                                   std::size_t* faultPc) {
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
                case Op::FORCE_THUNK: {
                    // A read of a by-name parameter (D47). The thunk the call
                    // site built is a zero-argument protoScala function; any
                    // other value is the argument itself, already evaluated at
                    // a call site the compiler could not resolve (D53), and
                    // passes through untouched.
                    const BytecodeModule* thunk = compiledModuleOf(&frame, L, sp[-1]);
                    if (!thunk || thunk->isMethod() || thunk->arity() != 0) continue;
                    pendingBase = static_cast<unsigned>(sp - 1 - slots);
                    sp[-1] = invoke(&frame, sp[-1], nullptr, 0);
                    pendingBase = kNoPendingCall;
                    continue;
                }
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
                    // No SmallInteger fast path here, and deliberately: the
                    // first branch of valuesEqual already is one
                    // (Values.cpp:202, two tag tests then a word compare), so
                    // duplicating it buys nothing. Measured with
                    // `perf stat -r 3`, interleaved, on this host: on a loop
                    // dominated by `==` between integers the duplicate was
                    // 1.172 vs 1.180 Gcycles -- inside the error bars -- while
                    // `sum_loop`, which never executes EQ, cost 750 vs 693
                    // Mcycles, an 8 % regression from code layout alone in the
                    // hottest function of the runtime. Phase 3 Task 1 Step 2
                    // was therefore backed out under its own 3 % rule.
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
                        sp[-1] = send(&frame, a, L.unaryMinusName, nullptr, 0);
                    continue;
                }
                case Op::NOT: {
                    const proto::ProtoObject* a = sp[-1];
                    if (a == PROTO_TRUE) sp[-1] = PROTO_FALSE;
                    else if (a == PROTO_FALSE) sp[-1] = PROTO_TRUE;
                    else sp[-1] = send(&frame, a, L.unaryNotName, nullptr, 0);
                    continue;
                }
                case Op::CONCAT: {
                    const unsigned n = static_cast<unsigned>(operand);
                    if (n < 2)
                        throw std::logic_error("CONCAT of arity " + std::to_string(n) + " in " +
                                               mod.name());
                    const proto::ProtoObject** base = sp - n;
                    // toScalaString returns a string unchanged and calls a user
                    // toString through the active engine otherwise, so a rope
                    // argument is joined, never flattened. Each converted piece
                    // is written back into its operand-stack slot before the
                    // next allocation: an accumulator held only in a C++ local
                    // is the Phase 5 Mailbox::push bug.
                    base[0] = toScalaString(&frame, L, base[0]);
                    for (unsigned k = 1; k < n; ++k) {
                        base[k] = toScalaString(&frame, L, base[k]);
                        base[0] = reinterpret_cast<const proto::ProtoString*>(base[0])
                                      ->appendLast(&frame,
                                                   reinterpret_cast<const proto::ProtoString*>(base[k]))
                                      ->asObject(&frame);
                    }
                    sp = base + 1;
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
                case Op::CALL_KW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = callKeywords(&frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::THROW: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_NONE)
                        throw ScalaError("NullPointerException", "throw null");
                    // Only a Throwable may be thrown (plan A0-5, Scala's rule),
                    // tested with the Phase 2 per-class marker attribute and not
                    // with protoCore's isInstanceOf (R3).
                    if (v->getAttribute(&frame, L.throwableKey) != PROTO_TRUE)
                        throw ScalaError("IllegalArgumentException",
                                         "throw expects a Throwable, got " +
                                             typeName(&frame, L, v));
                    throw ScalaThrow(v);
                }
                case Op::RETHROW: {
                    // A Catch cascade that matched nothing, or a Finally body
                    // that has finished: re-raise the value the handler saved in
                    // slot `operand` (plan A0-4).
                    const proto::ProtoObject* v = slots[operand];
                    if (!v || v == PROTO_NONE)
                        throw std::logic_error("RETHROW with no saved exception in " + mod.name());
                    throw ScalaThrow(v);
                }
            }
            // Every handled opcode continues the loop or returns; reaching this
            // point means the module holds an opcode value the VM does not know
            // (a compiler bug or a corrupted module), which is never skipped.
            throw std::logic_error("unknown opcode " +
                                   std::to_string(static_cast<unsigned>(op)) + " in " + mod.name());
        }
    } catch (FutureYield&) {
        // A cooperative suspension, not an error: this frame is part of a
        // suspended chain. It never reaches runFrame's handler search, and no
        // `finally` runs, because the frame will be resumed rather than
        // abandoned (plan A0-7 invariants 1 and 2). This clause must stay
        // FIRST: FutureYield is not a std::exception, but the order is what a
        // reader checks, and a Scala `catch` must never see it.
        //
        // Record this frame and rethrow; the frames prepend themselves, so the
        // actor's list reads outermost-first (the innermost frame catches first).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (pendingBase == kNoPendingCall)
            throw ScalaError("UnsupportedOperationException",
                             "await is not supported here: " + mod.name() +
                                 " cannot be suspended at this instruction");
        appendSuspendedFrame(&frame, layout_, mod, static_cast<unsigned>(ip - code), pendingBase,
                             slots);
        throw;
    } catch (ScalaThrow& t) {
        // A Scala exception value: runFrame searches this module's handler table.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (t.line == 0) t.line = mod.lineAt(*faultPc);
        throw;
    } catch (ScalaError& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (e.line == 0) e.line = mod.lineAt(*faultPc);
        throw;
    } catch (const std::invalid_argument& e) {
        // protoCore argument errors (e.g. asIntegerString with a bad base).
        // Caught by its EXACT type: std::invalid_argument derives from
        // std::logic_error, and a std::logic_error is a VM or compiler defect
        // that must stay uncatchable, so this file never carries a
        // `catch (const std::logic_error&)` clause (plan A0-6, D74).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IllegalArgumentException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::out_of_range& e) {
        // Also a std::logic_error subclass: caught by its exact type, as above.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IndexOutOfBoundsException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::overflow_error& e) {
        // Derives from std::runtime_error, so it must precede that clause.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("ArithmeticException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::bad_alloc& e) {
        // An Error, not an Exception: `case e: Exception` must not catch it.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("OutOfMemoryError", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::runtime_error& e) {
        // A protoCore error (e.g. "Objects are not integer types for division.")
        // becomes a Scala RuntimeException; it is never swallowed (DESIGN §7).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("RuntimeException", e.what());
        se.line = mod.lineAt(*faultPc);
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
    // Anything else is an ordinary method call on the left operand. The operator
    // name comes from the layout: a class that overloads `+` would otherwise
    // re-intern the symbol on every application.
    const proto::ProtoObject* argv[1] = {b};
    return send(ctx, a, binaryOpName(L, op), argv, 1);
}

} // namespace protoScala
