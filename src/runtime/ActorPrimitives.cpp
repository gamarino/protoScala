/*
 * ActorPrimitives — the native methods of the actor, future, thread and system
 * surface (DESIGN §8.1, §8.3). No new opcodes: `!`, `?`, `await` and friends
 * are ordinary sends (plan Task 0 A0-11).
 */
#include "compiler/GlobalTable.h"
#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"
#include "runtime/Futures.h"
#include "runtime/Mailbox.h"
#include "runtime/PrimitiveSupport.h"
#include "runtime/Primitives.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace protoScala {
namespace prim {
namespace {

// The blueprint a thread started by Thread.start installs (one runtime per
// process, R5), captured from the first Thread.start.
ActiveCallContext g_threadBlueprint{};

bool isCallable(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* v) {
    if (!v || v == PROTO_NONE) return false;
    if (v->isMethod(ctx)) return true;
    if (compiledModuleOf(ctx, L, v)) return true;
    return isObjectCellFast(v) && v->hasAttribute(ctx, L.applyName) == PROTO_TRUE;
}

const ProtoObject* makeTuple2(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* a,
                              const ProtoObject* b) {
    const ProtoObject* pair[2] = {a, b};
    return activeCallContext()->engine->send(ctx, L.tupleCompanion[2], L.applyName, pair, 2);
}

Band bandArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (proto::isSmallInt(v)) {
        const long long n = proto::asSmallInt(v);
        if (n >= 0 && n <= 2) return static_cast<Band>(static_cast<unsigned>(n));
    }
    (void)ctx;
    throw ScalaError("IllegalArgumentException",
                     std::string(method) +
                         " expects Priority.High, Priority.Medium or Priority.Low");
}

// Builds the envelope in its own child context and queues it. Returns the
// reply future for an ask, PROTO_NONE for a tell.
const ProtoObject* sendEnvelope(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* actor,
                                const ProtoObject* msg, Band band, bool wantsReply) {
    ActorScheduler& s = ActorScheduler::instance();
    if (!s.isActor(ctx, actor)) throw ScalaError("IllegalArgumentException", "not an actor");
    proto::ProtoContext scope(ctx->space, ctx);
    auto* env = const_cast<ProtoObject*>(L.envelopeProto->newChild(&scope, /*isMutable=*/true));
    scope.returnValue = env;
    env->setAttribute(&scope, L.msgKey, msg);
    const ProtoObject* future = PROTO_NONE;
    if (wantsReply) {
        future = futures::create(&scope, L);
        env->setAttribute(&scope, L.futureKey, future);
    }
    s.send(&scope, actor, band, env);
    scope.returnValue = future;
    return future;
}

// ---------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------

// Actor.spawn(state) -> an applier whose apply(handler) spawns the actor.
// Scala writes Actor.spawn(0) { (s, m) => ... }, which is
// Actor.spawn(0).apply(handler) (DESIGN §8.1).
PRIM(actor_spawnPartial) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* state = arg(ctx, args, 0, "Actor.spawn", 1);
    auto* partial = const_cast<ProtoObject*>(L.spawnPartialProto->newChild(ctx, true));
    partial->setAttribute(ctx, L.actorStateKey, state);
    return partial;
}

PRIM(actor_spawnApply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* handler = arg(ctx, args, 0, "Actor.spawn", 1);
    if (!isCallable(ctx, L, handler))
        throw ScalaError("IllegalArgumentException",
                         "Actor.spawn expects a handler function (state, message) => "
                         "(newState, reply)");
    ActorScheduler& s = ActorScheduler::instance();
    s.ensureStarted(ctx->space, ctx, L, activeCallContext()->engine);
    return s.spawn(ctx, handler, self->getOwnAttributeDirect(ctx, L.actorStateKey));
}

PRIM(actor_bang) {
    const RuntimeLayout& L = layoutOf();
    sendEnvelope(ctx, L, self, arg(ctx, args, 0, "!", 1), Band::Medium, /*wantsReply=*/false);
    return L.unit;
}

PRIM(actor_send) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "send", 2);
    sendEnvelope(ctx, L, self, args->getAt(ctx, 0), bandArg(ctx, args->getAt(ctx, 1), "send"),
                 /*wantsReply=*/false);
    return L.unit;
}

PRIM(actor_ask) {
    const RuntimeLayout& L = layoutOf();
    return sendEnvelope(ctx, L, self, arg(ctx, args, 0, "?", 1), Band::Medium, /*wantsReply=*/true);
}

PRIM(actor_askPriority) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "ask", 2);
    return sendEnvelope(ctx, L, self, args->getAt(ctx, 0), bandArg(ctx, args->getAt(ctx, 1), "ask"),
                        /*wantsReply=*/true);
}

// protoClojure's @actor: a read with no message, so it may observe a state
// older than a send that is still queued (D51).
PRIM(actor_value) {
    return self->getOwnAttributeDirect(ctx, layoutOf().actorStateKey);
}

PRIM(actor_toString) {
    const RuntimeLayout& L = layoutOf();
    return str(ctx, "Actor(" + show(ctx, L, self->getOwnAttributeDirect(ctx, L.actorStateKey)) + ")");
}

PRIM(actorObject_isActor) {
    return boolean(
        ActorScheduler::instance().isActor(ctx, arg(ctx, args, 0, "Actor.isActor", 1)));
}

PRIM(actorObject_stats) {
    const RuntimeLayout& L = layoutOf();
    const ActorScheduler& s = ActorScheduler::instance();
    const ProtoObject* a[2] = {
        proto::makeSmallInt(static_cast<long long>(s.workerCount())),
        proto::makeSmallInt(s.messagesProcessed())};
    return activeCallContext()->engine->invoke(ctx, L.hooks.actorStats, a, 2);
}

// ---------------------------------------------------------------------------
// Future
// ---------------------------------------------------------------------------

PRIM(future_await) {
    const RuntimeLayout& L = layoutOf();
    if (futures::state(ctx, L, self) != futures::kPending)
        return futures::result(ctx, L, self);  // raises a failed future's error
    const ProtoObject* actor = currentActor();
    if (!actor)  // the main thread, or Thread.start
        return futures::awaitBlocking(ctx, L, self);
    if (ExecutionEngine::nativeReentryDepth() > 1)
        throw ScalaError("UnsupportedOperationException",
                         "await is not supported inside a native higher-order method "
                         "(map, foreach, a Future continuation): the call chain cannot "
                         "be suspended");
    auto* a = const_cast<ProtoObject*>(actor);
    a->setAttribute(ctx, L.snapshotKey, ctx->newList()->asObject(ctx));  // frames prepend here
    a->setAttribute(ctx, L.waitingOnKey, self);
    if (!futures::addWaiter(ctx, L, self, actor)) {
        // It completed between the state read and the registration: do not
        // park, just answer now (design note 8).
        a->removeAttribute(ctx, L.snapshotKey);
        a->removeAttribute(ctx, L.waitingOnKey);
        return futures::result(ctx, L, self);
    }
    throw FutureYield(self);
}

PRIM(future_isCompleted) {
    const RuntimeLayout& L = layoutOf();
    return boolean(futures::state(ctx, L, self) != futures::kPending);
}

// Option[Try[T]]: None while pending (DESIGN §8.1).
PRIM(future_value) {
    const RuntimeLayout& L = layoutOf();
    ExecutionEngine* engine = activeCallContext()->engine;
    if (futures::state(ctx, L, self) == futures::kPending)
        return engine->invoke(ctx, L.hooks.noneValue, nullptr, 0);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* t = futures::tryOf(&scope, L, self);
    scope.returnValue = t;
    return engine->invoke(&scope, L.hooks.someCompanion, &t, 1);
}

PRIM(future_toString) {
    const RuntimeLayout& L = layoutOf();
    switch (futures::state(ctx, L, self)) {
        case futures::kSuccess:
            return str(ctx, "Future(" + show(ctx, L, futures::valueOf(ctx, L, self)) + ")");
        case futures::kFailure:
            return str(ctx, "Future(<failed: " + show(ctx, L, futures::errorOf(ctx, L, self)) + ">)");
        default: return str(ctx, "Future(<pending>)");
    }
}

// The continuation holders of map / flatMap / recover: one mutable object per
// registration, holding the derived future, the user function and the source
// future, so nothing is captured in C++ (P1).
const ProtoObject* makeCont(ProtoContext* ctx, const RuntimeLayout& L, proto::ProtoMethod fn,
                            const ProtoObject* derived, const ProtoObject* body,
                            const ProtoObject* source) {
    auto* holder = const_cast<ProtoObject*>(L.envelopeProto->newChild(ctx, /*isMutable=*/true));
    holder->setAttribute(ctx, L.futureKey, derived);
    holder->setAttribute(ctx, L.bodyKey, body ? body : PROTO_NONE);
    holder->setAttribute(ctx, L.waitingOnKey, source);
    return ctx->fromMethod(holder, fn);
}

PRIM(cont_map) {
    const RuntimeLayout& L = layoutOf();
    (void)args;
    const ProtoObject* derived = self->getOwnAttributeDirect(ctx, L.futureKey);
    const ProtoObject* body = self->getOwnAttributeDirect(ctx, L.bodyKey);
    const ProtoObject* source = self->getOwnAttributeDirect(ctx, L.waitingOnKey);
    if (futures::state(ctx, L, source) == futures::kFailure)
        futures::complete(ctx, L, derived, futures::errorOf(ctx, L, source), true);
    else
        try {
            const ProtoObject* v = futures::valueOf(ctx, L, source);
            const ProtoObject* r = activeCallContext()->engine->invoke(ctx, body, &v, 1);
            futures::complete(ctx, L, derived, r, false);
        } catch (ScalaError& e) {
            futures::completeError(ctx, L, derived, e.className().c_str(), e.message());
        }
    return L.unit;
}

// Copies the settled state of the inner future onto the derived one.
PRIM(cont_forward) {
    const RuntimeLayout& L = layoutOf();
    (void)args;
    const ProtoObject* derived = self->getOwnAttributeDirect(ctx, L.futureKey);
    const ProtoObject* source = self->getOwnAttributeDirect(ctx, L.waitingOnKey);
    const bool failed = futures::state(ctx, L, source) == futures::kFailure;
    futures::complete(ctx, L, derived,
                      failed ? futures::errorOf(ctx, L, source) : futures::valueOf(ctx, L, source),
                      failed);
    return L.unit;
}

PRIM(cont_flatMap) {
    const RuntimeLayout& L = layoutOf();
    (void)args;
    const ProtoObject* derived = self->getOwnAttributeDirect(ctx, L.futureKey);
    const ProtoObject* body = self->getOwnAttributeDirect(ctx, L.bodyKey);
    const ProtoObject* source = self->getOwnAttributeDirect(ctx, L.waitingOnKey);
    if (futures::state(ctx, L, source) == futures::kFailure) {
        futures::complete(ctx, L, derived, futures::errorOf(ctx, L, source), true);
        return L.unit;
    }
    try {
        const ProtoObject* v = futures::valueOf(ctx, L, source);
        const ProtoObject* inner = activeCallContext()->engine->invoke(ctx, body, &v, 1);
        proto::ProtoContext scope(ctx->space, ctx);
        scope.returnValue = inner;
        if (!futures::isFuture(&scope, L, inner))
            futures::completeError(&scope, L, derived, "ClassCastException",
                                   "flatMap expects a function returning a Future, got " +
                                       typeName(&scope, L, inner));
        else
            futures::onComplete(&scope, L, inner,
                                makeCont(&scope, L, &cont_forward, derived, nullptr, inner));
    } catch (ScalaError& e) {
        futures::completeError(ctx, L, derived, e.className().c_str(), e.message());
    }
    return L.unit;
}

PRIM(cont_recover) {
    const RuntimeLayout& L = layoutOf();
    (void)args;
    const ProtoObject* derived = self->getOwnAttributeDirect(ctx, L.futureKey);
    const ProtoObject* body = self->getOwnAttributeDirect(ctx, L.bodyKey);
    const ProtoObject* source = self->getOwnAttributeDirect(ctx, L.waitingOnKey);
    if (futures::state(ctx, L, source) != futures::kFailure) {
        futures::complete(ctx, L, derived, futures::valueOf(ctx, L, source), false);
        return L.unit;
    }
    try {
        const ProtoObject* e = futures::errorOf(ctx, L, source);
        const ProtoObject* r = activeCallContext()->engine->invoke(ctx, body, &e, 1);
        futures::complete(ctx, L, derived, r, false);
    } catch (ScalaError& e) {
        futures::completeError(ctx, L, derived, e.className().c_str(), e.message());
    }
    return L.unit;
}

const ProtoObject* derive(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* source,
                          const ProtoObject* body, proto::ProtoMethod fn, const char* method) {
    if (!isCallable(ctx, L, body))
        throw ScalaError("IllegalArgumentException", std::string(method) + " expects a function");
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* derived = futures::create(&scope, L);
    scope.returnValue = derived;
    futures::onComplete(&scope, L, source, makeCont(&scope, L, fn, derived, body, source));
    return derived;
}

PRIM(future_map) {
    const RuntimeLayout& L = layoutOf();
    return derive(ctx, L, self, arg(ctx, args, 0, "map", 1), &cont_map, "map");
}
PRIM(future_flatMap) {
    const RuntimeLayout& L = layoutOf();
    return derive(ctx, L, self, arg(ctx, args, 0, "flatMap", 1), &cont_flatMap, "flatMap");
}
PRIM(future_recover) {
    const RuntimeLayout& L = layoutOf();
    return derive(ctx, L, self, arg(ctx, args, 0, "recover", 1), &cont_recover, "recover");
}
PRIM(future_onComplete) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* fn = arg(ctx, args, 0, "onComplete", 1);
    if (!isCallable(ctx, L, fn))
        throw ScalaError("IllegalArgumentException", "onComplete expects a function");
    futures::onComplete(ctx, L, self, fn);
    return L.unit;
}

// The one-shot actor Future.apply runs its body on (DESIGN §8.3): the body is
// the native's receiver, so it stays reachable through the handler the actor
// holds (P1).
PRIM(futureBody_handler) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* state = args && args->getSize(ctx) > 0 ? args->getAt(ctx, 0) : L.unit;
    const ProtoObject* r = activeCallContext()->engine->invoke(ctx, self, nullptr, 0);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.returnValue = r;
    const ProtoObject* pair = makeTuple2(&scope, L, state, r);
    scope.returnValue = pair;
    return pair;
}

PRIM(futureObject_apply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* body = arg(ctx, args, 0, "Future.apply", 1);
    if (!isCallable(ctx, L, body))
        throw ScalaError("IllegalArgumentException",
                         "Future.apply takes its body by name: write Future(expr). It cannot be "
                         "reached through a method value");
    ActorScheduler& s = ActorScheduler::instance();
    s.ensureStarted(ctx->space, ctx, L, activeCallContext()->engine);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* handler = scope.fromMethod(const_cast<ProtoObject*>(body), &futureBody_handler);
    scope.returnValue = handler;
    const ProtoObject* actor = s.spawn(&scope, handler, L.unit);
    scope.returnValue = actor;
    return sendEnvelope(&scope, L, actor, L.unit, Band::Medium, /*wantsReply=*/true);
}

PRIM(futureObject_successful) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* v = arg(ctx, args, 0, "Future.successful", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* f = futures::create(&scope, L);
    scope.returnValue = f;
    futures::complete(&scope, L, f, v, false);
    return f;
}

PRIM(futureObject_failed) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* e = arg(ctx, args, 0, "Future.failed", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* f = futures::create(&scope, L);
    scope.returnValue = f;
    futures::complete(&scope, L, f, e, true);
    return f;
}

// ---------------------------------------------------------------------------
// Thread and System (D49)
// ---------------------------------------------------------------------------

const ProtoObject* threadEntry(ProtoContext* ctx, const ProtoObject*, const proto::ParentLink*,
                               const ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const ProtoObject* handle = args->getAt(ctx, 0);
    ExecutionEngine::ActiveCallGuard guard(g_threadBlueprint.engine, g_threadBlueprint.layout);
    const RuntimeLayout& L = *g_threadBlueprint.layout;
    try {
        const ProtoObject* body = handle->getOwnAttributeDirect(ctx, L.bodyKey);
        g_threadBlueprint.engine->invoke(ctx, body, nullptr, 0);
    } catch (ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: thread failed: %s\n", e.what());
    }
    return PROTO_NONE;
}

PRIM(threadObject_start) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* body = arg(ctx, args, 0, "Thread.start", 1);
    if (!isCallable(ctx, L, body))
        throw ScalaError("IllegalArgumentException",
                         "Thread.start expects a function: write Thread.start(() => expr)");
    g_threadBlueprint = *activeCallContext();
    proto::ProtoContext scope(ctx->space, ctx);
    auto* handle = const_cast<ProtoObject*>(L.threadProto->newChild(&scope, /*isMutable=*/true));
    scope.returnValue = handle;
    handle->setAttribute(&scope, L.bodyKey, body);
    // Anchored for the session, like an actor (D46): the OS thread reads the
    // body through the handle, so nothing may collect it.
    auto* reg = const_cast<ProtoObject*>(L.actorRegistry);
    for (;;) {
        proto::ProtoContext one(scope.space, &scope);
        one.resizeAutomaticLocals(1);
        const ProtoObject* cur = reg->getOwnAttributeDirect(&one, L.threadsKey);
        one.setAutomaticLocal(0, cur);   // rooted across appendLast (P1)
        const ProtoObject* next = cur->asList(&one)->appendLast(&one, handle)->asObject(&one);
        one.returnValue = next;
        if (reg->setAttributeIfEqual(&one, L.threadsKey, cur, next)) break;
    }
    const proto::ProtoString* name = proto::ProtoString::createSymbol(&scope, "protoscala-thread");
    const ProtoList* targs = scope.newList()->appendLast(&scope, handle);
    const proto::ProtoThread* t = scope.space->newThread(&scope, name, &threadEntry, targs, nullptr);
    handle->setAttribute(&scope, L.threadRefKey,
                         scope.fromInteger(static_cast<long long>(reinterpret_cast<std::intptr_t>(t))));
    return handle;
}

PRIM(thread_join) {
    const RuntimeLayout& L = layoutOf();
    (void)args;
    const ProtoObject* ref = self->getOwnAttributeDirect(ctx, L.threadRefKey);
    if (ref && ref != PROTO_NONE) {
        auto* t = reinterpret_cast<proto::ProtoThread*>(static_cast<std::intptr_t>(ref->asLong(ctx)));
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);  // opened before blocking (P6)
        t->join(ctx);
    }
    return L.unit;
}

PRIM(thread_toString) { (void)self; return str(ctx, "Thread"); }

PRIM(system_nanoTime) {
    (void)self; (void)args;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return ctx->fromInteger(
        static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()));
}

PRIM(system_currentTimeMillis) {
    (void)self; (void)args;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return ctx->fromInteger(
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
}

PRIM(system_getenv) {
    const char* v = std::getenv(stringArg(ctx, arg(ctx, args, 0, "System.getenv", 1),
                                          "System.getenv").c_str());
    return str(ctx, v ? v : "");
}

struct Entry {
    const char* name;
    proto::ProtoMethod fn;
};

void install(ProtoContext* ctx, proto::ProtoObject* target, const Entry* entries, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        target->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, entries[i].name),
                             ctx->fromMethod(nullptr, entries[i].fn));
}

} // namespace
} // namespace prim

// `Future(expr)` must not evaluate `expr` on the caller: the body belongs to the
// one-shot actor the future runs on (DESIGN §8.3), so `Future.apply` declares
// its single parameter by-name (D47). The compiler learns it from here.
const std::vector<BuiltinByNameSignature>& builtinByNameSignatures() {
    static const std::vector<BuiltinByNameSignature> sigs = {{"Future", {0x1u}}};
    return sigs;
}

void installActorPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace protoScala::prim;
    static constexpr Entry actorObject[] = {{"spawn", &actor_spawnPartial},
                                            {"isActor", &actorObject_isActor},
                                            {"stats", &actorObject_stats}};
    static constexpr Entry spawnPartial[] = {{"apply", &actor_spawnApply}};
    static constexpr Entry actors[] = {{"!", &actor_bang},   {"send", &actor_send},
                                       {"?", &actor_ask},    {"ask", &actor_askPriority},
                                       {"value", &actor_value}, {"toString", &actor_toString}};
    static constexpr Entry futuresEntries[] = {
        {"await", &future_await},         {"isCompleted", &future_isCompleted},
        {"value", &future_value},         {"toString", &future_toString},
        {"map", &future_map},             {"flatMap", &future_flatMap},
        {"recover", &future_recover},     {"onComplete", &future_onComplete}};
    static constexpr Entry futureObject[] = {{"apply", &futureObject_apply},
                                             {"successful", &futureObject_successful},
                                             {"failed", &futureObject_failed}};
    static constexpr Entry threadObject[] = {{"start", &threadObject_start}};
    static constexpr Entry threads[] = {{"join", &thread_join}, {"toString", &thread_toString}};
    static constexpr Entry system[] = {{"nanoTime", &system_nanoTime},
                                       {"currentTimeMillis", &system_currentTimeMillis},
                                       {"getenv", &system_getenv}};

    install(ctx, L.actorCompanion, actorObject, std::size(actorObject));
    install(ctx, L.spawnPartialProto, spawnPartial, std::size(spawnPartial));
    install(ctx, L.actorProto, actors, std::size(actors));
    install(ctx, L.futureProto, futuresEntries, std::size(futuresEntries));
    install(ctx, L.futureCompanion, futureObject, std::size(futureObject));
    install(ctx, L.threadCompanion, threadObject, std::size(threadObject));
    install(ctx, L.threadProto, threads, std::size(threads));
    install(ctx, L.systemCompanion, system, std::size(system));

    // Priority.High / Medium / Low are the fields 0 / 1 / 2 (D52).
    L.priorityCompanion->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "High"),
                                      proto::makeSmallInt(0));
    L.priorityCompanion->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Medium"),
                                      proto::makeSmallInt(1));
    L.priorityCompanion->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Low"),
                                      proto::makeSmallInt(2));

    auto bind = [&](const char* name, proto::ProtoObject* v) {
        L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name), v);
    };
    bind("Actor", L.actorCompanion);
    bind("Priority", L.priorityCompanion);
    bind("Future", L.futureCompanion);
    bind("Thread", L.threadCompanion);
    bind("System", L.systemCompanion);
}

void bindPreludeHooks(proto::ProtoContext* ctx, RuntimeLayout& layout, const GlobalTable& globals) {
    auto resolve = [&](const char* name) -> const proto::ProtoObject* {
        const GlobalBinding* b = globals.binding(name);
        if (!b)
            throw std::logic_error(std::string("prelude hook '") + name + "' is not defined");
        const auto* key = proto::ProtoString::createSymbol(ctx, b->key.c_str());
        const proto::ProtoObject* v = layout.globals->getOwnAttributeDirect(ctx, key);
        if (!v || v == PROTO_NONE)
            throw std::logic_error(std::string("prelude hook '") + name + "' has no value");
        return v;
    };
    layout.hooks.someCompanion = resolve("__mkSome");
    layout.hooks.noneValue = resolve("__mkNone");
    layout.hooks.success = resolve("__mkSuccess");
    layout.hooks.failure = resolve("__mkFailure");
    layout.hooks.runtimeError = resolve("__mkRuntimeError");
    layout.hooks.actorStats = resolve("__mkActorStats");
    layout.hooks.left = resolve("__mkLeft");
    layout.hooks.right = resolve("__mkRight");
    layout.hooks.bound = true;
}

} // namespace protoScala
