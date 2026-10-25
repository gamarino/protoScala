#include "runtime/ExecutionEngine.h"
#include <protoScala/GeneratedModule.h>
#include "runtime/OpcodeOps.h"
#include "compiler/GlobalTable.h"
#include "compiler/BytecodeModule.h"
#include "runtime/CallerSlots.h"
#include "runtime/Debugger.h"
#include "runtime/ErrorFactory.h"
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"
#include "runtime/StackGuard.h"
#include "runtime/Values.h"
#include "umd/SmalltalkSelector.h"
#include "protoCore.h"

#include <cmath>
#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace protoScala {

namespace {

thread_local ActiveCallContext tl_active{};
thread_local bool tl_activeSet = false;

// Native frames between the running bytecode frame and the current C++ frame
// (D43): 1 while a native method runs, more when a native re-entered the VM.
thread_local unsigned tl_nativeDepth = 0;

// The innermost VM frame's spare slots (CallerSlots.h). Defined here so that
// execute's publication of it is a direct thread-local access, and in the
// initial-exec TLS model where the toolchain has one: every activation saves
// and restores it, and the general-dynamic model's __tls_get_addr calls cost
// fib(30) about 6 % (measured interleaved, 15 rounds, 2026-10-25). The cost of
// the model is a static TLS slot of 16 bytes, which a dlopen of libprotoScala
// takes from glibc's static TLS surplus.
#if defined(__GNUC__) && defined(__ELF__)
#define PROTOSCALA_TLS_INITIAL_EXEC __attribute__((tls_model("initial-exec")))
#else
#define PROTOSCALA_TLS_INITIAL_EXEC
#endif
thread_local FrameSpare tl_frameSpare PROTOSCALA_TLS_INITIAL_EXEC = {};

// Published by a VM frame for as long as it runs; restores the enclosing
// frame's spare slots on every exit path.
class FrameSpareScope {
public:
    FrameSpareScope(proto::ProtoContext* frame, const proto::ProtoObject** spare)
        : saved_(tl_frameSpare) {
        tl_frameSpare = FrameSpare{frame, spare};
    }
    ~FrameSpareScope() { tl_frameSpare = saved_; }
    FrameSpareScope(const FrameSpareScope&) = delete;
    FrameSpareScope& operator=(const FrameSpareScope&) = delete;

private:
    FrameSpare saved_;
};

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

} // namespace

const ActiveCallContext* activeCallContext() { return tl_activeSet ? &tl_active : nullptr; }
FrameSpare& currentFrameSpare() { return tl_frameSpare; }

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

namespace {
// The innermost outstanding foreign call on this thread (ForeignCallMark).
thread_local proto::ProtoThread* tl_foreignCallThread = nullptr;
thread_local const proto::ProtoSpace* tl_foreignCallSpace = nullptr;
} // namespace

ExecutionEngine::ForeignCallMark::ForeignCallMark(proto::ProtoContext* caller)
    : savedThread_(tl_foreignCallThread), savedSpace_(tl_foreignCallSpace) {
    tl_foreignCallThread = caller->thread;
    tl_foreignCallSpace = caller->space;
}
ExecutionEngine::ForeignCallMark::~ForeignCallMark() {
    tl_foreignCallThread = savedThread_;
    tl_foreignCallSpace = savedSpace_;
}

proto::ProtoContext* ExecutionEngine::foreignReentryContext(const proto::ProtoSpace* scalaSpace) {
    if (!tl_foreignCallThread || tl_foreignCallSpace != scalaSpace) return nullptr;
    proto::ProtoContext* current = tl_foreignCallThread->getCurrentContext();
    return current && current->space == scalaSpace ? current : nullptr;
}

const proto::ProtoObject* ExecutionEngine::smalltalkEntryOf(proto::ProtoContext* ctx,
                                                            const proto::ProtoObject* receiver) const {
    if (receiver == PROTO_NONE || !isObjectCellFast(receiver)) return nullptr;
    const proto::ProtoObject* e = receiver->getAttribute(ctx, layout_.stSendKey);
    if (!e || e == PROTO_NONE || !e->isMethod(ctx)) return nullptr;
    layout_.smalltalkSeen.store(true, std::memory_order_relaxed);
    return e;
}

// A Smalltalk message (D135). The selector and the arguments travel as the
// entry's positional arguments; the values are the cells themselves, in both
// directions. callNative is the boundary: a protoST error
// arrives as the ProtoException carrying it and passes through untouched, a
// refusal (another thread, an actor) as a std::exception the frame translates.
const proto::ProtoObject* ExecutionEngine::sendSmalltalk(proto::ProtoContext* ctx,
                                                         const proto::ProtoObject* entry,
                                                         const proto::ProtoObject** base,
                                                         const proto::ProtoString* name,
                                                         unsigned argc) {
    // A member of Scala's Any (eq, ne, ==, !=, equals, hashCode, ##, toString)
    // keeps its Scala meaning on a protoST object: every value has them, and
    // `a eq b` is identity whatever `a` is. Everything else is a message.
    const proto::ProtoObject* universal = layout_.anyProto->getAttribute(ctx, name);
    if (universal && universal != PROTO_NONE && universal->isMethod(ctx))
        return callNative(ctx, universal->asMethod(ctx), base[0], base + 1, argc);
    // Throwable.getMessage, which a Scala handler writes on any caught
    // exception, is Smalltalk's `messageText` (D136, decided by the maintainer).
    std::string scalaName = plainName(name->toStdString(ctx));
    if (name == layout_.getMessageName && argc == 0) scalaName = "messageText";
    std::string why;
    const std::optional<std::string> selector = smalltalkSelector(scalaName, argc, &why);
    if (!selector) throw ScalaError("IllegalArgumentException", why);
    // The selector is an interned symbol (perennial) and the arguments are the
    // caller's traced slots base[1..argc], so a C++ array carries them: no
    // context of its own (the maintainer's context rule, CallerSlots.h).
    ArgBuffer a(argc + 1);
    a[0] = reinterpret_cast<const proto::ProtoObject*>(
        proto::ProtoString::createSymbol(ctx, selector->c_str()));
    for (unsigned k = 0; k < argc; ++k) a[k + 1] = base[k + 1];
    ForeignCallMark mark(ctx);
    return callNative(ctx, entry->asMethod(ctx), base[0], a.data(), argc + 1);
}
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
                                                      unsigned argc,
                                                      const proto::ProtoKeywordArguments* keywords) {
    // The native runs in the caller's context: a ProtoContext belongs to a
    // method activation only (the maintainer's context rule). The arguments are
    // read in place (protoCore's array call form): the caller roots them, in its
    // frame's slots or in storage it traces. The window gives the native the
    // calling frame's spare slots (CallerSlots.h).
    const proto::ProtoArguments list(args, argc);
    NativeCallScope window(ctx, tl_frameSpare);
    // A native that re-enters the VM raises the depth for everything it calls,
    // and the destructor lowers it on unwinding too, so a FutureYield passing
    // through does not leave it raised (D43, A0-2).
    NativeDepthGuard depth;
    const proto::ProtoObject* r = nullptr;
    // The C++ boundary to a native. runLoop's chain below already translates
    // every std:: exception through DESIGN §7's table, and already passes
    // FutureYield and every proto::ProtoException (a Scala throw, a module's
    // error, protoCore's own) through, so the one clause that was missing is
    // the last resort: a native from a dlopen'd provider plug-in that throws
    // something which is not a std::exception at all would otherwise escape the
    // VM and terminate the process (Phase 6 plan A0-7). std::logic_error reaches
    // the std::exception arm and is re-thrown untouched, so D74 survives: a VM
    // defect stays uncatchable.
    // Every clause, the translating one included, re-throws after the catch
    // (see runFrame).
    std::exception_ptr passOn;
    try {
        r = fn(ctx, self, nullptr, &list, keywords);
    } catch (FutureYield&) {
        passOn = std::current_exception();  // a cooperative suspension, not an error
    } catch (const proto::ProtoException&) {
        passOn = std::current_exception();  // a language value in flight, untouched
    } catch (ScalaError&) {
        passOn = std::current_exception();  // a native throw site's own translation
    } catch (const std::exception&) {
        passOn = std::current_exception();  // runLoop translates it, and attaches the source line
    } catch (...) {
        passOn = std::make_exception_ptr(ScalaError("RuntimeException", "native exception"));
    }
    if (passOn) std::rethrow_exception(passOn);
    return r ? r : PROTO_NONE;
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
            // `self` is reachable from the callee, which the caller roots.
            ArgBuffer a(argc + 1);
            a[0] = self;
            for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
            return execute(ctx, *m, a.data(), argc + 1, caps);
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
    // dispatch may overwrite base[0] with an intermediate result (a
    // parameterless member's value), a young cell of `ctx`'s chain or a
    // callee's anchored return value, so the buffer needs no tracing.
    ArgBuffer base(argc + 1);
    base[0] = receiver;
    for (unsigned k = 0; k < argc; ++k) base[k + 1] = args[k];
    return dispatch(ctx, base.data(), name, argc);
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
        if (receiver->hasAttribute(ctx, name) != PROTO_TRUE) {
            // A keyword selector is never an attribute name (D135).
            if (const proto::ProtoObject* st = smalltalkEntryOf(ctx, receiver))
                return sendSmalltalk(ctx, st, base, name, argc);
            // Throwable.getMessage on a foreign value that is not a protoST
            // object -- a module's error, another language's: its `message`
            // attribute, the convention protoCore's own error values and its
            // example modules follow (D136).
            if (name == layout_.getMessageName && argc == 0 &&
                isForeignObject(ctx, layout_, receiver) &&
                receiver->hasAttribute(ctx, layout_.messageKey) == PROTO_TRUE)
                return receiver->getAttribute(ctx, layout_.messageKey);
            throwMissingMember(ctx, receiver, name);
        }
        if (argc == 0 && !applied) return PROTO_NONE;
        throw ScalaError("NullPointerException", "cannot call null");
    }
    // `obj.p()` or `obj.p(args)` where `p` is written without a parameter list:
    // Scala applies the *result* of the member (`def g = () => n; c.g()` is
    // `c.g.apply()`, and `def env: Map[..]; sys.env("HOME")` is
    // `sys.env.apply("HOME")`), and rejects the call when the result takes no
    // arguments ("method p in class H does not take parameters"). `def p() = e`
    // is arity-1 too but not paramless, so it is called directly.
    if (applied)
        if (const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m))
            if (mod->isMethod() && mod->isParamless() &&
                !m->getOwnAttributeDirect(ctx, layout_.selfKey)) {
                const proto::ProtoObject* r = execute(ctx, *mod, base, 1, nullptr);
                base[0] = r;  // the receiver is no longer needed; keep r rooted
                if (r != PROTO_NONE &&
                    (compiledModuleOf(ctx, layout_, r) || r->isMethod(ctx) ||
                     r->hasAttribute(ctx, layout_.applyName) == PROTO_TRUE))
                    return invoke(ctx, r, base + 1, argc);
                throw ScalaError("NoSuchMethodError", "method " + plainName(name->toStdString(ctx)) +
                                                          " does not take parameters");
            }
    return callMember(ctx, m, base, argc, applied, name);
}

// Calls the value `m` found for a member of the receiver base[0]: a native
// method, a Scala method (receiver in slot 0), a lazy val holder, a
// function-valued field, a plain field, or an object with `apply`.
const proto::ProtoObject* ExecutionEngine::callMember(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* m,
                                                      const proto::ProtoObject** base, unsigned argc,
                                                      bool applied, const proto::ProtoString* name) {
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
    // A member Scala cannot call. On a protoST object it is one of its methods --
    // an object carrying protoST bytecode, or a primitive marker (a large
    // integer) -- and the call sends it as a Smalltalk message (D135). A call
    // with an argument list probes the receiver at once: Scala has no other
    // meaning left for it. A read with NO argument list -- `c.value`, which is
    // also how every Scala field is read -- probes only once the session has met
    // a protoST object, and then only for a member that could be a protoST
    // method. Measured, 5M reads, instructions: an Int field read +48 % when
    // every read probed the receiver; with the member tests below, +1.5 % for an
    // Int field and +24 % for a field holding a Scala object, which is why a
    // session that never meets protoST skips them all.
    if (name &&
        (!select || (L.smalltalkSeen.load(std::memory_order_relaxed) &&
                     (isLargeIntFast(m) || (isObjectCellFast(m) && isForeignObject(ctx, L, m))))))
        if (const proto::ProtoObject* st = smalltalkEntryOf(ctx, base[0]))
            return sendSmalltalk(ctx, st, base, name, argc);
    if (select) return m;                            // a field
    return send(ctx, m, L.applyName, base + 1, argc);  // obj.x(args) with x an object: x.apply(args)
}

const proto::ProtoObject* ExecutionEngine::callWithReceiver(proto::ProtoContext* ctx,
                                                            const proto::ProtoObject* m,
                                                            const proto::ProtoObject** base,
                                                            unsigned argc,
                                                            const proto::ProtoKeywordArguments* keywords) {
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), base[0], base + 1, argc, keywords);
    const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m);
    if (!mod || !mod->isMethod()) throw std::logic_error("callWithReceiver: not a method");
    return execute(ctx, *mod, base, argc + 1, nullptr, keywords);
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
    // The receiver is rooted by the caller; the thunk's frame copies it.
    const proto::ProtoObject* const self[1] = {receiver};
    const proto::ProtoObject* v = execute(ctx, *mod, self, 1, nullptr);
    holder->setAttribute(ctx, L.valueKey, v);  // mutable holder: in place
    return v;
}

// new cls(args): base[0] = cls, base[1..argc] the arguments.
const proto::ProtoObject* ExecutionEngine::instantiate(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject** base,
                                                       const proto::ProtoString* ctorKey,
                                                       unsigned argc,
                                                       const proto::ProtoKeywordArguments* keywords) {
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
    return callWithReceiver(ctx, init, base, argc, keywords);
}

const proto::ProtoObject* ExecutionEngine::construct(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* cls,
                                                     const proto::ProtoObject* const* args,
                                                     unsigned argc) {
    // instantiate replaces a[0] with the fresh instance, a young cell of ctx.
    ArgBuffer a(argc + 1);
    a[0] = cls;
    for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
    return instantiate(ctx, a.data(), layout_.initKey, argc);
}

// MAKE_CLASS: an immutable prototype whose chain is exactly the pushed
// parents (the linearization without the class, Design notes 1-2), carrying
// the members, the class metadata and its membership marker.
const proto::ProtoObject* ExecutionEngine::makeClass(proto::ProtoContext* ctx,
                                                     const BytecodeModule::Const& spec,
                                                     const proto::ProtoObject* const* base) {
    const RuntimeLayout& L = layout_;
    // Every intermediate shape is a young cell of ctx: no context of its own.
    const proto::ProtoList* chain = ctx->newList(spec.argc, base);
    // The prototype is MUTABLE (Phase 4). Two things need it, and neither has an
    // alternative that keeps the object model intact:
    //
    //  - an extension method is installed on it after the class exists, and every
    //    instance already created must see it (D6, D82). An immutable prototype's
    //    setAttribute answers a NEW object, which the existing instances' chains
    //    would not point at;
    //  - `superSend` finds the defining class in the linearization by POINTER
    //    identity, so the prototype's identity must survive any later write.
    //
    // It also makes class creation cheaper: the members below now mutate one
    // object instead of building a fresh immutable copy per member. Instances are
    // unaffected — their own mutability is the separate `__mutable__` decision.
    const proto::ProtoObject* shape =
        L.anyProto->newChild(ctx, /*isMutable=*/true)->setParents(ctx, chain);
    for (std::size_t k = 0; k < spec.nameSymbols.size(); ++k)
        shape = shape->setAttribute(ctx, spec.nameSymbols[k], base[spec.argc + k]);
    shape = shape->setAttribute(ctx, spec.keySymbol, PROTO_TRUE);
    shape = shape->setAttribute(ctx, L.nameKey, makeString(ctx, spec.sval));
    if (spec.flags & BytecodeModule::kClassMutableInstances)
        shape = shape->setAttribute(ctx, L.mutableKey, PROTO_TRUE);
    if (spec.flags & BytecodeModule::kClassCase) {
        shape = shape->setAttribute(ctx, L.prefixKey, makeString(ctx, spec.sval));
        if (!(spec.flags & BytecodeModule::kClassCaseObject)) {
            const proto::ProtoList* fields = ctx->newList();
            for (const proto::ProtoString* f : spec.fieldSymbols)
                fields = fields->appendLast(ctx, f->asObject(ctx));
            shape = shape->setAttribute(ctx, L.fieldsKey, fields->asObject(ctx));
            if (spec.flags & BytecodeModule::kClassFieldsOnlyInit)
                shape = shape->setAttribute(ctx, L.fieldsOnlyInitKey, PROTO_TRUE);
        }
    }
    return shape;
}

const proto::ProtoObject* ExecutionEngine::makeTuple(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* const* elems,
                                                     unsigned n) {
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* t = L.tupleProto[n]->newChild(ctx, false);
    for (unsigned k = 0; k < n; ++k) t = t->setAttribute(ctx, L.tupleFieldKey[k + 1], elems[k]);
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
    const proto::proto_ulong n = chain->getSize(ctx);
    const std::string ownerName = GlobalTable::nameOfKey(site.key.substr(1));
    const std::string spelling =
        "super" + (site.exact ? "[" + ownerName + "]" : "") + "." + site.sval;
    proto::proto_ulong k = 0;
    while (k < n && chain->getAt(ctx, static_cast<int>(k)) != owner) ++k;
    if (k == n)
        throw ScalaError("NoSuchMethodError",
                         ownerName + " is not in the linearization of " +
                             typeName(ctx, L, base[0]) + ", so " + spelling +
                             " has no meaning here");
    // super[T].m starts AT T, so T's own definition is the one the programmer
    // named; plain super.m starts after the defining class (DESIGN §4.4).
    if (!site.exact) ++k;
    for (; k < n; ++k) {
        const proto::ProtoObject* m =
            chain->getAt(ctx, static_cast<int>(k))->getOwnAttributeDirect(ctx, site.symbol);
        if (m) return callMember(ctx, m, base, site.argc);
    }
    throw ScalaError("NoSuchMethodError",
                     spelling + " has no implementation after " + ownerName);
}

namespace {

// The named arguments of a SEND_KW / CALL_KW / NEW site: (interned name, value)
// pairs over the
// values the site left on the operand stack, which the collector traces, so
// nothing is allocated (protoCore's ProtoKeywordArguments). More names than
// kInline go to a vector.
struct SiteKeywords {
    static constexpr std::size_t kInline = 8;
    proto::ProtoKeywordArgument inlinePairs[kInline];
    std::vector<proto::ProtoKeywordArgument> more;
    proto::ProtoKeywordArguments args;

    SiteKeywords(const proto::ProtoObject* const* base, const BytecodeModule::Const& site)
        : args(fill(base, site), static_cast<unsigned>(site.nameSymbols.size())) {}
    SiteKeywords(const SiteKeywords&) = delete;
    SiteKeywords& operator=(const SiteKeywords&) = delete;

private:
    proto::ProtoKeywordArgument* fill(const proto::ProtoObject* const* base,
                                      const BytecodeModule::Const& site) {
        const std::size_t n = site.nameSymbols.size();
        proto::ProtoKeywordArgument* pairs = inlinePairs;
        if (n > kInline) {
            more.resize(n);
            pairs = more.data();
        }
        for (std::size_t k = 0; k < n; ++k) pairs[k] = {site.nameSymbols[k], base[1 + site.argc + k]};
        return pairs;
    }
};

// The name of the first named argument that is not one of the callee's
// parameters: each pair carries its interned name (SiteKeywords).
std::string unmatchedKeywordName(proto::ProtoContext* ctx, const BytecodeModule& mod,
                                 const proto::ProtoKeywordArguments* keywords) {
    const auto& symbols = mod.paramNameSymbols();
    for (unsigned i = 0; i < keywords->count(); ++i) {
        const proto::ProtoString* name = keywords->pairs()[i].name;
        bool known = false;
        for (const proto::ProtoString* s : symbols)
            if (s == name) { known = true; break; }
        if (!known) return name->toStdString(ctx);
    }
    return "?";
}

}  // namespace

void ExecutionEngine::bindKeywordsAndDefaults(proto::ProtoContext& frame, const BytecodeModule& mod,
                                              const proto::ProtoKeywordArguments* keywords,
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
        proto::proto_ulong matched = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto key = reinterpret_cast<proto::proto_ulong>(symbols[i]);
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
        // The default block mirrors this frame's slot prefix — parameters,
        // locals and captures — so it is run with those slots as its arguments,
        // and its own arity is exactly how many of them it expects.
        const BytecodeModule& dflt = mod.block(block);
        slots[i] = execute(&frame, dflt, slots, static_cast<unsigned>(dflt.arity()), nullptr);
        filled[i] = true;
    }
}

// Named arguments reach every callable as protoCore's ProtoKeywordArguments,
// (interned parameter name, value) pairs (DESIGN §5.2, plan A0-12): native
// methods, Scala-defined methods and foreign callables alike, with no special
// case for any of them.
const proto::ProtoObject* ExecutionEngine::sendKeywords(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject** base,
                                                        const BytecodeModule::Const& site) {
    const proto::ProtoObject* receiver = base[0];
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + plainName(site.sval) + "' on null");
    const proto::ProtoString* name = siteName(ctx, receiver, site);
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE || (!m->isMethod(ctx) && !compiledModuleOf(ctx, layout_, m)))
        if (smalltalkEntryOf(ctx, receiver))
            throw ScalaError("IllegalArgumentException",
                             plainName(site.sval) +
                                 ": a Smalltalk message takes no named arguments");
    if (!m || m == PROTO_NONE) throwMissingMember(ctx, receiver, name);
    if (const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m)) {
        if (mod->isMethod() && !m->getOwnAttributeDirect(ctx, layout_.selfKey)) {
            // A method of the receiver: `this` travels in slot 0, so the
            // positional arguments start at base[0] (the receiver itself).
            const SiteKeywords siteKeywords(base, site);
            return execute(ctx, *mod, base, site.argc + 1, nullptr, &siteKeywords.args);
        }
        // A function-valued member (or a bound method): call it as a value.
        const SiteKeywords siteKeywords(base, site);
        const proto::ProtoKeywordArguments* keywords = &siteKeywords.args;
        const proto::ProtoObject* caps =
            mod->captureCount() ? m->getOwnAttributeDirect(ctx, layout_.capturesKey) : nullptr;
        return mod->isMethod() ? execute(ctx, *mod, base, site.argc + 1, caps, keywords)
                               : execute(ctx, *mod, base + 1, site.argc, caps, keywords);
    }
    if (!m->isMethod(ctx))
        throw ScalaError("IllegalArgumentException",
                         plainName(site.sval) + " is not a method of " +
                             typeName(ctx, layout_, receiver) + ", so it takes no named arguments");
    // The arguments and the keyword values sit in the operand stack: read in place.
    const SiteKeywords siteKeywords(base, site);
    return callNative(ctx, m->asMethod(ctx), receiver, base + 1, site.argc, &siteKeywords.args);
}

// CALL_KW: `f(x = 1)` where `f` is a function value, a global `def` or a local
// function. base[0] is the callee, base[1..argc] the positional arguments and
// the keyword values follow them.
const proto::ProtoObject* ExecutionEngine::callKeywords(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject** base,
                                                        const BytecodeModule::Const& site) {
    const proto::ProtoObject* callee = base[0];
    if (callee == PROTO_NONE) throw ScalaError("NullPointerException", "cannot call null");
    const SiteKeywords siteKeywords(base, site);
    const proto::ProtoKeywordArguments* keywords = &siteKeywords.args;
    if (const BytecodeModule* mod = compiledModuleOf(ctx, layout_, callee)) {
        const proto::ProtoObject* caps =
            mod->captureCount() ? callee->getOwnAttributeDirect(ctx, layout_.capturesKey) : nullptr;
        const proto::ProtoObject* r;
        if (mod->isMethod()) {
            // A bound method: its receiver travels in slot 0.
            const proto::ProtoObject* self =
                callee->getOwnAttributeDirect(ctx, layout_.selfKey);
            if (!self) throw std::logic_error("callKeywords: unbound method");
            ArgBuffer a(site.argc + 1);  // self is reachable from the callee in base[0]
            a[0] = self;
            for (unsigned k = 0; k < site.argc; ++k) a[k + 1] = base[1 + k];
            r = execute(ctx, *mod, a.data(), site.argc + 1, caps, keywords);
        } else {
            r = execute(ctx, *mod, base + 1, site.argc, caps, keywords);
        }
        return r;
    }
    if (callee->isMethod(ctx))  // the operand stack, read in place
        return callNative(ctx, callee->asMethod(ctx), callee->asMethodSelf(ctx), base + 1,
                          site.argc, keywords);
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
                                                   const proto::ProtoKeywordArguments* keywords) {
    checkNativeStack();
    // A TRANSPILED block (Phase 7): the body is a proto::ProtoMethod and there are
    // no code words to run. Everything above this line and below it is unchanged,
    // and so is every caller: a transpiled module carries the same metadata an
    // interpreted one does, so `compiledModuleOf` and its nineteen readers cannot
    // tell the two apart -- which is the whole reason the entry lives on the module
    // rather than in a second callable shape.
    //
    // `self`, for a block that is NOT a method, carries the captures: the generated
    // frame reads its capture slots from it. For a method, slot 0 is the receiver,
    // exactly as it is here, and a method never has captures (callMember passes
    // none, and a module that needs them fails loudly below).
    if (const proto::ProtoMethod entry = mod.nativeEntry()) {
        const bool method = mod.isMethod();
        const proto::ProtoObject* self = method ? (argc ? args[0] : PROTO_NONE) : captures;
        const unsigned skip = method ? 1u : 0u;
        // Read in place: the caller roots `args`, and the generated frame
        // (gen::Frame, the body's one context) copies them into its slots.
        const unsigned n = argc > skip ? argc - skip : 0u;
        const proto::ProtoArguments positional(n ? args + skip : nullptr, n);
        NativeDepthGuard depth;
        const proto::ProtoObject* r = entry(parent, self, nullptr, &positional, keywords);
        return r ? r : PROTO_NONE;
    }
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
    const unsigned spareBase = stackBase + static_cast<unsigned>(mod.maxStack());
    frame.resizeAutomaticLocals(spareBase + kPrimitiveSlots);
    const proto::ProtoObject** slots = frame.getAutomaticLocals();  // never resized again
    // The natives this frame calls run in it and keep what they hold in these
    // spare slots (CallerSlots.h).
    FrameSpareScope spare(&frame, slots + spareBase);
    const unsigned positional = argc < fixed ? argc : fixed;
    for (unsigned k = 0; k < positional; ++k) slots[k] = args[k];
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
    // Named arguments and defaults are bound HERE, in the callee, and AFTER the
    // captures: a default block mirrors this frame's parameters and captures, so
    // both must already be in place. A caller that does not know its callee
    // statically therefore still works — the platform is late-binding even where
    // Scala is not; late detection is accepted, silent failure is not.
    if (keywords || (mod.hasDefaults() && positional < fixed))
        bindKeywordsAndDefaults(frame, mod, keywords, slots, positional);

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
    // In the suspending frame's context: `rec` is young there until the actor's
    // snapshot (a mutable object the actor keeps) references it.
    auto* rec = const_cast<proto::ProtoObject*>(L.frameProto->newChild(ctx, /*isMutable=*/true));
    // The module address travels as a SmallInteger, as MAKE_FN already does;
    // modules are owned by the Session for the whole session, so it cannot dangle.
    rec->setAttribute(ctx, L.modKey,
                      ctx->fromInteger(static_cast<long long>(reinterpret_cast<std::intptr_t>(&mod))));
    rec->setAttribute(ctx, L.ipKey, proto::makeSmallInt(ipOffset));
    rec->setAttribute(ctx, L.fbaseKey, proto::makeSmallInt(pendingBase));
    rec->setAttribute(ctx, L.fslotsKey, ctx->newList(pendingBase, slots)->asObject(ctx));
    const proto::ProtoObject* cur = actor->getOwnAttributeDirect(ctx, L.snapshotKey);
    actor->setAttribute(ctx, L.snapshotKey,
                        cur->asList(ctx)->appendFirst(ctx, rec)->asObject(ctx));
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
    const unsigned spareBase = stackBase + static_cast<unsigned>(mod.maxStack());
    frame.resizeAutomaticLocals(spareBase + kPrimitiveSlots);
    const proto::ProtoObject** slots = frame.getAutomaticLocals();
    FrameSpareScope spare(&frame, slots + spareBase);  // CallerSlots.h
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
    // A named prelude class if there is one, else RuntimeException carrying the
    // original class name in its message, so nothing is ever lost (plan A0-6).
    auto it = L.hooks.throwableClasses.find(e.className());
    const bool known = it != L.hooks.throwableClasses.end();
    const proto::ProtoObject* cls = known ? it->second : L.hooks.runtimeException;
    if (!cls)
        throw std::logic_error("materialiseError: the prelude exception classes are not bound");
    // The message string is a young cell of ctx, live until ctx's chain is
    // submitted, which nothing does while the constructor runs (CallerSlots.h).
    const proto::ProtoObject* const message[1] = {
        makeString(ctx, known ? e.message() : e.className() + ": " + e.message())};
    return construct(ctx, cls, message, 1);
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
    // Exceptions passing through a VM frame are re-thrown AFTER the catch clause
    // has completed (std::rethrow_exception), never with `throw;` inside it. Under
    // the Itanium ABI (Linux, macOS) the two are the same. Under MSVC a catch clause
    // runs before the stack below it is released, so a `throw;` inside it starts
    // the next dispatch below the frames already left behind: each Scala frame an
    // exception passed through would cost several KiB, and a StackOverflowError
    // raised at depth would overflow the native stack while propagating.
    // The exceptions this frame's handlers caught, so that a value the frame
    // re-raises leaves as the SAME C++ exception (protoCore rule E3): a foreign
    // runtime's subclass keeps what it carries. Frame state, on the C++ stack.
    gen::InFlightExceptions inflight;
    for (;;) {
        std::size_t faultPc = 0;
        std::exception_ptr passOn;
        try {
            return runLoop(frame, mod, slots, sp, ip, &faultPc, &inflight);
        } catch (const proto::ProtoException& t) {
            // Every language value arrives here: a Scala throw, an error a module
            // raised, protoCore's own (built by the error factory). protoCore pins
            // the value while the exception is in flight; enterHandler moves it
            // into a traced slot before the handler body allocates. The handler
            // body selects by the value's chain and RETHROWs what it does not
            // match (CompilePatterns.cpp).
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (h) {
                inflight.record(static_cast<unsigned>(h->slot), t.value(),
                                std::current_exception());
                sp = enterHandler(mod, slots, *h, t.value(), &ip);
                continue;   // leaves the catch block: the handler body then runs with
                            // NO live C++ handler, so it suspends like any other code
            }
            passOn = std::current_exception();
        } catch (ScalaError& e) {
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (h) {
                const proto::ProtoObject* v = materialiseError(&frame, e);
                frame.returnValue = v;   // rooted until enterHandler stores it in a slot
                if (debug::attached()) debug::noteMaterialised(&frame, debug::kNativeError, v);
                sp = enterHandler(mod, slots, *h, v, &ip);
                continue;
            }
            passOn = std::current_exception();   // the uncaught path allocates nothing
        }
        std::rethrow_exception(passOn);
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
    if (!h) throw ScalaThrow(&frame, payload);
    const Instr* ip = nullptr;
    const proto::ProtoObject** newSp = enterHandler(mod, slots, *h, payload, &ip);
    return runFrame(frame, mod, slots, newSp, ip);
}

const proto::ProtoObject* ExecutionEngine::runLoop(proto::ProtoContext& frame,
                                                   const BytecodeModule& mod,
                                                   const proto::ProtoObject** slots,
                                                   const proto::ProtoObject** sp,
                                                   const Instr* ip,
                                                   std::size_t* faultPc,
                                                   const gen::InFlightExceptions* inflight) {
    const Instr* const code = mod.code().data();
    const RuntimeLayout& L = layout_;
    // The operand-stack index where the in-flight call will write its result,
    // or kNoPendingCall when this frame is not inside a call. Two stores per
    // call opcode; it is what makes a frame resumable after a cooperative
    // yield (DESIGN §8.3, D43).
    unsigned pendingBase = kNoPendingCall;
    // Set by every clause below, translating ones included, and re-thrown
    // after the catch has completed (runFrame).
    std::exception_ptr passOn;
    // The debugger's view of this frame (Debugger.h): kept only while one is
    // attached, so a detached run pays one branch here and none per instruction.
    debug::FrameRecord dbgFrame;
    const bool dbgOn = debug::attached();
    if (dbgOn) debug::enterFrame(dbgFrame, mod, &frame, slots);
    struct DbgLeave {
        debug::FrameRecord* r;
        ~DbgLeave() { if (r) debug::leaveFrame(*r); }
    } dbgLeave{dbgOn ? &dbgFrame : nullptr};
    // What the debugger's exception filters are told (debugRaised): set by the
    // clauses below for the first VM frame a C++ exception passes through.
    bool dbgFresh = false;
    const proto::ProtoObject* dbgValue = nullptr;
    std::string dbgClass, dbgMessage;
    try {
        for (;;) {
            if (dbgOn)
                debug::atInstruction(dbgFrame, static_cast<std::size_t>(ip - code));
            Instr word = *ip++;
            Op op = static_cast<Op>(word & 0xFF);
            std::uint64_t operand = word >> kOperandShift;
            if (op == Op::EXTEND) {
                const Instr next = *ip++;
                operand = (operand << 24) | (next >> kOperandShift);
                op = static_cast<Op>(next & 0xFF);
            }
            switch (op) {
                case Op::EXTEND: continue;
                case Op::PUSH_CONST: {
                    const auto& c = mod.constAt(operand);
                    *sp++ = ops::constantOf(&frame, c.kind, c.ival, c.dval, c.base, c.sval,
                                            mod.name());
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
                    slots[operand] = ops::makeCell(&frame, L);
                    continue;
                case Op::PUSH_CELL:
                    *sp++ = ops::cellGet(&frame, L, slots[operand]);
                    continue;
                case Op::STORE_CELL:
                    slots[operand] = ops::cellSet(&frame, L, slots[operand], sp[-1]);
                    --sp;
                    continue;
                case Op::PUSH_GLOBAL: {
                    const auto& c = mod.constAt(operand);
                    *sp++ = ops::pushGlobal(&frame, L, c.symbol, c.sval);
                    continue;
                }
                case Op::STORE_GLOBAL:
                    ops::storeGlobal(&frame, L, mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::MAKE_FN: {
                    const BytecodeModule& sub = mod.block(operand);
                    const unsigned nc = static_cast<unsigned>(sub.captureCount());
                    // User-space addresses are below 2^47, inside the SmallInteger range.
                    const auto address = reinterpret_cast<std::intptr_t>(&sub);
                    const proto::ProtoObject* fn = ops::makeFunctionObject(
                        &frame, L, static_cast<unsigned>(sub.arity()), L.codeKey,
                        proto::makeSmallInt(address), sp - nc, nc);
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
                        // The values are the frame's slots and the elements of
                        // the list in sp[-1]: rooted, so a C++ array carries them.
                        ArgBuffer a(n + extra);
                        for (unsigned k = 0; k < n; ++k) a[k] = base[1 + k];
                        for (unsigned k = 0; k < extra; ++k) a[n + k] = list->getAt(&frame, static_cast<int>(k));
                        r = invoke(&frame, base[0], a.data(), n + extra);
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
                    base[0] = ops::sendNamed(&frame, *this, base, site.symbol, site.argc,
                                             site.keySymbol, op == Op::SEND_APPLY);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::RETURN: {
                    const proto::ProtoObject* r = sp[-1];
                    frame.returnValue = r;
                    return r;
                }
                case Op::MAKE_LAZY:
                    sp[-1] = ops::makeLazy(&frame, L, sp[-1]);
                    continue;
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
                    if (!ops::isZeroArgThunk(&frame, L, sp[-1])) continue;
                    pendingBase = static_cast<unsigned>(sp - 1 - slots);
                    sp[-1] = invoke(&frame, sp[-1], nullptr, 0);
                    pendingBase = kNoPendingCall;
                    continue;
                }
                case Op::JUMP: ip += operand; continue;
                case Op::JUMP_IF_FALSE:
                    if (!ops::truthy(&frame, L, *--sp)) ip += operand;
                    continue;
                case Op::JUMP_IF_TRUE:
                    if (ops::truthy(&frame, L, *--sp)) ip += operand;
                    continue;
                case Op::JUMP_BACK:
                    ip -= operand;
                    ops::safepoint(&frame);  // Open question Q21; every live value is in a slot
                    continue;
                case Op::ADD: case Op::SUB: case Op::MUL:
                    sp[-2] = ops::arith(&frame, *this, op, sp[-2], sp[-1]);
                    --sp;
                    continue;
                case Op::LT: case Op::LE: case Op::GT: case Op::GE:
                    sp[-2] = ops::compare(&frame, *this, op, sp[-2], sp[-1]);
                    --sp;
                    continue;
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
                    sp[-2] = ops::equality(&frame, L, op, sp[-2], sp[-1]);
                    --sp;
                    continue;
                }
                case Op::NEG:
                    sp[-1] = ops::neg(&frame, *this, sp[-1]);
                    continue;
                case Op::NOT:
                    sp[-1] = ops::notOp(&frame, *this, sp[-1]);
                    continue;
                case Op::CONCAT: {
                    const unsigned n = static_cast<unsigned>(operand);
                    const proto::ProtoObject** base = sp - n;
                    ops::concat(&frame, L, base, n, mod.name());
                    sp = base + 1;
                    continue;
                }
                case Op::MAKE_CLASS: {
                    const auto& spec = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - spec.argc - static_cast<unsigned>(spec.nameSymbols.size());
                    base[0] = ops::Engine::makeClass(*this, &frame, spec, base);
                    sp = base + 1;
                    continue;
                }
                case Op::NEW: {
                    // A KwSendSite operand means `new C(x = 1)`: the keyword
                    // values follow the positional ones and the constructor's own
                    // prologue binds them, so no extra opcode is needed.
                    const auto& site = mod.constAt(operand);
                    const unsigned named = static_cast<unsigned>(site.nameSymbols.size());
                    const proto::ProtoObject** base = sp - site.argc - named - 1;  // [cls a1..an k1..km]
                    pendingBase = static_cast<unsigned>(base - slots);
                    if (named == 0) {
                        base[0] = instantiate(&frame, base, site.symbol, site.argc);
                    } else {
                        const SiteKeywords siteKeywords(base, site);
                        base[0] = instantiate(&frame, base, site.symbol, site.argc, &siteKeywords.args);
                    }
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
                        // As CALL_SPREAD: rooted values, a C++ array.
                        ArgBuffer a(n + extra + 1);
                        for (unsigned k = 0; k <= n; ++k) a[k] = base[k];
                        for (unsigned k = 0; k < extra; ++k)
                            a[n + 1 + k] = list->getAt(&frame, static_cast<int>(k));
                        r = instantiate(&frame, a.data(), site.symbol, n + extra);
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
                    base[0] = ops::Engine::callWithReceiver(*this, &frame, init, base + 1,
                                                            site.argc);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::STORE_FIELD:  // constructors: `this` is slot 0 (Design note 10)
                    slots[0] = ops::storeField(&frame, slots[0], mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::STORE_FIELD_IF_NEW:
                    slots[0] =
                        ops::storeFieldIfNew(&frame, slots[0], mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::STORE_FIELDS_IF_NEW: {  // a constructor's parameter fields, one group
                    const auto& keys = mod.constAt(operand).nameSymbols;
                    const unsigned n = static_cast<unsigned>(keys.size());
                    slots[0] = ops::storeFieldsIfNew(&frame, slots[0], keys.data(), n, sp - n);
                    sp -= n;
                    continue;
                }
                case Op::SET_FIELD:  // setters of var fields: the instance is mutable
                    ops::setField(&frame, sp[-2], mod.constAt(operand).symbol, sp[-1]);
                    sp -= 2;
                    continue;
                case Op::SEND_SUPER: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // [this a1..an]
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = ops::Engine::superSend(*this, &frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::TEST_TYPE:
                    sp[-1] = ops::Engine::testType(*this, &frame, static_cast<TypeCode>(operand),
                                                   sp[-1]) ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                case Op::TEST_PROTO:  // class membership by marker (Design note 5)
                    sp[-1] = ops::testProtoKey(&frame, mod.constAt(operand).symbol, sp[-1])
                                 ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                case Op::UNAPPLY_FIELDS: {
                    const auto& names = mod.constAt(operand);
                    const proto::ProtoObject* v = *--sp;  // also held by the match's scrutinee slot
                    sp += ops::unapplyFieldsWith(&frame, names.nameSymbols.data(),
                                                 static_cast<unsigned>(names.nameSymbols.size()), v,
                                                 sp);
                    continue;
                }
                case Op::UNCONS:
                    ops::uncons(&frame, sp[-1], sp - 1, sp);
                    ++sp;
                    continue;
                case Op::MATCH_ERROR:
                    ops::matchError(&frame, L, sp[-1]);
                case Op::CAST_FAIL:
                    ops::castFail(&frame, L, sp[-1], mod.constAt(operand).sval);
                case Op::MAKE_TUPLE: {
                    const unsigned n = static_cast<unsigned>(operand);
                    if (n < 2 || n > kMaxTupleArity)  // Tuple2..Tuple22 only
                        throw std::logic_error("MAKE_TUPLE of arity " + std::to_string(n) + " in " +
                                               mod.name());
                    const proto::ProtoObject** base = sp - n;
                    base[0] = ops::Engine::makeTuple(*this, &frame, base, n);
                    sp = base + 1;
                    continue;
                }
                case Op::SEND_KW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = ops::Engine::sendKeywords(*this, &frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::CALL_KW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
                    pendingBase = static_cast<unsigned>(base - slots);
                    base[0] = ops::Engine::callKeywords(*this, &frame, base, site);
                    pendingBase = kNoPendingCall;
                    sp = base + 1;
                    continue;
                }
                case Op::THROW:
                    // `throw e` of a value this frame caught: the original exception (E3).
                    if (inflight) inflight->rethrowIfCaught(sp[-1]);
                    ops::throwValue(&frame, L, *--sp);
                case Op::RETHROW:
                    // A Catch cascade that matched nothing, or a Finally body that
                    // has finished: re-raise the value the handler saved in slot
                    // `operand` (plan A0-4).
                    // The ORIGINAL exception, unchanged, when this frame caught it
                    // (protoCore rule E3); a new one only for a value it did not
                    // catch as a ProtoException (a materialised ScalaError, a frame
                    // resumed after a suspension).
                    if (inflight) inflight->rethrowIfCaught(slots[operand]);
                    ops::rethrow(&frame, slots[operand], mod.name());
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
        if (pendingBase == kNoPendingCall) {
            passOn = std::make_exception_ptr(
                ScalaError("UnsupportedOperationException",
                           "await is not supported here: " + mod.name() +
                               " cannot be suspended at this instruction"));
        } else {
            appendSuspendedFrame(&frame, layout_, mod, static_cast<unsigned>(ip - code),
                                 pendingBase, slots);
            passOn = std::current_exception();
        }
    } catch (ScalaThrow& t) {
        // A Scala exception value: runFrame searches this module's handler table.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (dbgOn && t.line == 0) {
            dbgFresh = true;
            dbgValue = t.value();
        }
        if (t.line == 0) t.line = mod.lineAt(*faultPc);
        passOn = std::current_exception();
    } catch (const proto::ProtoException& e) {
        // A language value raised outside protoScala's own throw sites: protoCore's
        // error (built by the error factory as a Scala class), a module's error, or
        // another runtime's. Only protoScala's OWN value in a plain ProtoException --
        // protoCore's error built by the factory, which carries nothing but that
        // value -- is re-raised as a ScalaThrow, to give it this source line (D14's
        // report shape). Anything foreign passes through as the same C++ object
        // (protoCore rule E3), and an uncaught one is reported without a line.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (dbgOn) {
            // A foreign one passes through every frame unchanged: the debugger
            // tells it from the next by its value (debug::alreadyReported).
            dbgFresh = true;
            dbgValue = e.value();
        }
        if (typeid(e) == typeid(proto::ProtoException) && isScalaInstance(&frame, L, e.value())) {
            ScalaThrow t(&frame, e.value());
            t.line = mod.lineAt(*faultPc);
            passOn = std::make_exception_ptr(t);
        } else {
            passOn = std::current_exception();
        }
    } catch (ScalaError& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (dbgOn && e.line == 0) {
            dbgFresh = true;
            dbgClass = e.className();
            dbgMessage = e.message();
        }
        if (e.line == 0) e.line = mod.lineAt(*faultPc);
        passOn = std::current_exception();
    } catch (const std::invalid_argument& e) {
        // The std:: arms below translate C++ library errors -- a native's, or a
        // UMD module's (a foreign C++ exception is a module defect, not a language
        // value). protoCore itself no longer throws them since 3.0.0: its errors
        // are ProtoExceptions, handled above.
        // Caught by its EXACT type: std::invalid_argument derives from
        // std::logic_error, and a std::logic_error is a VM or compiler defect
        // that must stay uncatchable, so this file never carries a
        // `catch (const std::logic_error&)` clause (plan A0-6, D74).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IllegalArgumentException", e.what());
        se.line = mod.lineAt(*faultPc);
        if (dbgOn) {
            dbgFresh = true;
            dbgClass = se.className();
            dbgMessage = se.message();
        }
        passOn = std::make_exception_ptr(se);
    } catch (const std::out_of_range& e) {
        // Also a std::logic_error subclass: caught by its exact type, as above.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IndexOutOfBoundsException", e.what());
        se.line = mod.lineAt(*faultPc);
        if (dbgOn) {
            dbgFresh = true;
            dbgClass = se.className();
            dbgMessage = se.message();
        }
        passOn = std::make_exception_ptr(se);
    } catch (const std::overflow_error& e) {
        // Derives from std::runtime_error, so it must precede that clause.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("ArithmeticException", e.what());
        se.line = mod.lineAt(*faultPc);
        if (dbgOn) {
            dbgFresh = true;
            dbgClass = se.className();
            dbgMessage = se.message();
        }
        passOn = std::make_exception_ptr(se);
    } catch (const std::bad_alloc& e) {
        // An Error, not an Exception: `case e: Exception` must not catch it.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("OutOfMemoryError", e.what());
        se.line = mod.lineAt(*faultPc);
        if (dbgOn) {
            dbgFresh = true;
            dbgClass = se.className();
            dbgMessage = se.message();
        }
        passOn = std::make_exception_ptr(se);
    } catch (const std::runtime_error& e) {
        // A C++ runtime error from a native or a module (e.g. the geometry
        // example's legacyArea) becomes a Scala RuntimeException carrying what();
        // it is never swallowed (DESIGN §7).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("RuntimeException", e.what());
        se.line = mod.lineAt(*faultPc);
        if (dbgOn) {
            dbgFresh = true;
            dbgClass = se.className();
            dbgMessage = se.message();
        }
        passOn = std::make_exception_ptr(se);
    }
    // After the catch clause, while this frame and its callers are intact: a
    // stop here shows the faulting line with its locals.
    if (dbgFresh && debug::exceptionBreaks() != 0 && !debug::suppressed())
        debugRaised(frame, dbgFrame, dbgValue, dbgClass, dbgMessage);
    std::rethrow_exception(passOn);
}

// --- The debugger's exception filters (Debugger.h, docs/debugging.md) --------
//
// Called after runLoop's catch clause, for the first VM frame a C++ exception
// passes through, so the faulting frame and every caller are intact: a stop
// here shows the faulting line, its locals and evaluate. It is reached once per
// exception: later frames see a ScalaThrow/ScalaError whose line is set, and a
// value already reported (a RETHROW, `throw e` of a caught e, a foreign
// exception passing through unchanged) is recognised by debug::alreadyReported.
//
// The "uncaught" filter searches the handler tables of this thread's frames,
// innermost first, with each frame's current pc and the catch cascade's summary
// (BytecodeModule::CatchFilter). The search ends at a native boundary
// (debug::NativeBoundary): one that captures the exception (Try, a Future body)
// counts as caught -- the program may still look at the Failure -- and one that
// only reports it (a future continuation) as uncaught, like the thread's top.
// A native that lets an exception pass unchanged is transparent.
//
// Exceptions the prelude raises and catches itself, before any frame of the
// program sees them, are internal: they never stop.
void ExecutionEngine::debugRaised(proto::ProtoContext& frame, debug::FrameRecord& rec,
                                  const proto::ProtoObject* value, const std::string& className,
                                  const std::string& message) {
    if (value && debug::alreadyReported(value, nullptr)) return;
    debug::FrameRecord* top = &rec;
    while (top && (!top->src || top->src->library)) top = top->parent;
    if (!top) return;  // no frame of the program on this thread
    const unsigned breaks = debug::exceptionBreaks();
    const RuntimeLayout& L = layout_;
    // In the faulting frame's context: the values materialised below are young
    // cells of it, live until the frame's chain is submitted.
    proto::ProtoContext* const ctx = &frame;
    const proto::ProtoObject* held[2] = {value, nullptr};  // [0] in flight, pinned by its exception

    enum class Fate { Caught, MaybeCaught, Uncaught };
    Fate fate = Fate::Uncaught;
    bool internal = false;
    if ((breaks & debug::kBreakUncaught) || top != &rec) {
        debug::Suppress quiet;  // what the search runs never stops
        // The value the type tests look at: a native error is materialised for
        // them (it is not the instance a handler will build). An error raised
        // because memory or the stack ran out is not: that could fail again.
        bool triedValue = false;
        auto testValue = [&]() -> const proto::ProtoObject* {
            if (held[0] || triedValue) return held[0] ? held[0] : held[1];
            triedValue = true;
            if (className == "OutOfMemoryError" || className == "StackOverflowError") return nullptr;
            try {
                held[1] = materialiseError(ctx, ScalaError(className, message));
            } catch (...) {
                held[1] = nullptr;
            }
            return held[1];
        };
        auto catches = [&](const debug::FrameRecord& r) -> bool {
            for (const BytecodeModule::Handler& h : r.mod->handlers()) {
                if (r.pc < h.startPc || r.pc >= h.endPc) continue;
                if (h.kind != BytecodeModule::HandlerKind::Catch) continue;  // a finally re-raises
                const BytecodeModule::CatchFilter* f = r.mod->catchFilter(h.handlerPc);
                if (!f || f->all || f->unknown) return true;
                const proto::ProtoObject* v = testValue();
                if (!v) return true;  // cannot tell: assume it is caught
                const auto* throwable = proto::ProtoString::createSymbol(ctx, kThrowableKey);
                for (const BytecodeModule::CatchFilter::Key& k : f->keys) {
                    const auto* key = proto::ProtoString::createSymbol(ctx, k.key.c_str());
                    if (ops::testProtoKey(ctx, key, v)) return true;
                    if (k.orForeign && !ops::testProtoKey(ctx, throwable, v)) return true;
                }
            }
            return false;
        };
        try {
            for (debug::FrameRecord* r = &rec; r; r = r->parent) {
                if (catches(*r)) {
                    fate = Fate::Caught;
                    internal = r->depth > top->depth;
                    break;
                }
                if (r->boundary == debug::Boundary::Captures) {
                    fate = Fate::MaybeCaught;
                    internal = r->depth > top->depth;
                    break;
                }
                if (r->boundary == debug::Boundary::Reports) break;
            }
        } catch (...) {
            fate = Fate::MaybeCaught;  // the search itself failed: do not claim "uncaught"
        }
    }
    if (internal) return;
    const bool raised = (breaks & debug::kBreakRaised) != 0;
    const bool uncaught = (breaks & debug::kBreakUncaught) != 0 && fate == Fate::Uncaught;
    if (!raised && !uncaught) return;
    // A filter's condition sees the exception as `e`: a native error is
    // materialised for it (not the instance a handler will build).
    if (!held[0] && !held[1] && className != "OutOfMemoryError" &&
        className != "StackOverflowError") {
        debug::Suppress quiet;
        try {
            held[1] = materialiseError(ctx, ScalaError(className, message));
        } catch (...) {
            held[1] = nullptr;
        }
    }
    std::string note;
    const unsigned stopping = (raised ? debug::kBreakRaised : 0u) | (uncaught ? debug::kBreakUncaught : 0u);
    if (!debug::exceptionConditionHolds(stopping, ctx, *top, held[0] ? held[0] : held[1], &note))
        return;

    debug::ExceptionStop ex;
    ex.breakMode = uncaught ? "userUnhandled" : "always";
    if (value) {
        debug::Suppress quiet;
        const std::string text = describeThrown(ctx, value);
        std::string type;
        if (isScalaInstance(ctx, L, value)) {
            const proto::ProtoObject* n = value->getAttribute(ctx, L.nameKey);
            if (proto::ProtoObject::isStringTagFast(n))
                type = reinterpret_cast<const proto::ProtoString*>(n)->toStdString(ctx);
        }
        if (type.empty()) type = text.substr(0, text.find(": "));
        ex.typeName = type;
        ex.message = text.rfind(type + ": ", 0) == 0 ? text.substr(type.size() + 2)
                     : text == type                   ? std::string()
                                                      : text;
        ex.description = text;
    } else {
        ex.typeName = className;
        ex.message = message;
        ex.description = message.empty() ? className : className + ": " + message;
    }
    // Where it came from, when that is not a `throw` on the line shown.
    if (top != &rec) {
        ex.description += " (raised in " + rec.mod->name() + ", called from this line)";
    } else {
        const Op op = static_cast<Op>(rec.mod->code()[rec.pc] & 0xFF);
        if (op != Op::THROW && op != Op::RETHROW)
            ex.description += " (raised by the runtime on this line)";
    }
    debug::markReported(ctx, value, value ? nullptr : debug::kNativeError);
    debug::stopForException(*top, ctx, ex, note.empty() ? nullptr : &note);
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
