#include "runtime/Futures.h"
#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/FutureYield.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace protoScala::futures {

namespace {

// One condition variable for every blocking waiter in the process, plus an
// epoch bumped by each completion and a counter of blocked threads, so a
// completion pays nothing when nobody is blocked (protoST's mainWaitingOn
// trick, generalised to any thread).
std::mutex g_waitMutex;
std::condition_variable g_waitCv;
std::atomic<unsigned long long> g_epoch{0};
std::atomic<int> g_blocked{0};

void wakeBlockedThreads() {
    g_epoch.fetch_add(1, std::memory_order_release);
    if (g_blocked.load(std::memory_order_seq_cst) != 0) {
        std::lock_guard<std::mutex> lk(g_waitMutex);  // pairs with the waiter's lock
        g_waitCv.notify_all();
    }
}

// A continuation must not suspend: it may run on a worker in the middle of
// another actor's turn, so `await` inside one would park the wrong actor. The
// current actor is cleared and the native depth raised for the duration, which
// makes `await` refuse with the D43 message.
struct ActorTurnPause {
    const proto::ProtoObject* saved;
    ExecutionEngine::NativeDepthGuard depth;
    ActorTurnPause() : saved(currentActor()) { setCurrentActor(nullptr); }
    ~ActorTurnPause() { setCurrentActor(saved); }
    ActorTurnPause(const ActorTurnPause&) = delete;
    ActorTurnPause& operator=(const ActorTurnPause&) = delete;
};

const proto::ProtoObject* callHook(proto::ProtoContext* ctx, const proto::ProtoObject* fn,
                                   const proto::ProtoObject* const* args, unsigned argc) {
    const ActiveCallContext* active = activeCallContext();
    if (!active || !fn)
        throw std::logic_error("prelude hook used before bindPreludeHooks");
    return active->engine->invoke(ctx, fn, args, argc);
}

void runContinuations(proto::ProtoContext* ctx, const RuntimeLayout& L,
                      const proto::ProtoObject* f);

} // namespace

const proto::ProtoObject* create(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    auto* f = const_cast<proto::ProtoObject*>(L.futureProto->newChild(ctx, /*isMutable=*/true));
    f->setAttribute(ctx, L.fstateKey, proto::makeSmallInt(kPending));
    f->setAttribute(ctx, L.fvalueKey, PROTO_NONE);
    f->setAttribute(ctx, L.ferrorKey, PROTO_NONE);
    f->setAttribute(ctx, L.waitersKey, ctx->newList()->asObject(ctx));
    f->setAttribute(ctx, L.contsKey, ctx->newList()->asObject(ctx));
    return f;
}

bool isFuture(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return v && v != PROTO_NONE && isObjectCellFast(v) &&
           v->hasAttribute(ctx, L.fstateKey) == PROTO_TRUE;
}

long long state(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f) {
    const proto::ProtoObject* s = f->getOwnAttributeDirect(ctx, L.fstateKey);
    if (!s || !proto::isSmallInt(s)) return kPending;
    const long long v = proto::asSmallInt(s);
    // A claimed-but-not-yet-written future is still pending to everyone else.
    return v == kClaiming ? kPending : v;
}

const proto::ProtoObject* valueOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                  const proto::ProtoObject* f) {
    const proto::ProtoObject* v = f->getOwnAttributeDirect(ctx, L.fvalueKey);
    return v ? v : PROTO_NONE;
}

const proto::ProtoObject* errorOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                  const proto::ProtoObject* f) {
    const proto::ProtoObject* v = f->getOwnAttributeDirect(ctx, L.ferrorKey);
    return v ? v : PROTO_NONE;
}

void raiseError(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* err) {
    std::string cls = "RuntimeException", msg;
    if (err && err != PROTO_NONE) {
        const proto::ProtoObject* c = err->getOwnAttributeDirect(ctx, L.classNameField);
        const proto::ProtoObject* m = err->getOwnAttributeDirect(ctx, L.messageField);
        if (c && proto::ProtoObject::isStringTagFast(c))
            cls = reinterpret_cast<const proto::ProtoString*>(c)->toStdString(ctx);
        if (m && proto::ProtoObject::isStringTagFast(m))
            msg = reinterpret_cast<const proto::ProtoString*>(m)->toStdString(ctx);
    }
    throw ScalaError(cls, msg);
}

const proto::ProtoObject* result(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                 const proto::ProtoObject* f) {
    if (state(ctx, L, f) == kFailure) raiseError(ctx, L, errorOf(ctx, L, f));
    return valueOf(ctx, L, f);
}

const proto::ProtoObject* tryOf(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                const proto::ProtoObject* f) {
    const bool failed = state(ctx, L, f) == kFailure;
    const proto::ProtoObject* a[1] = {failed ? errorOf(ctx, L, f) : valueOf(ctx, L, f)};
    return callHook(ctx, failed ? L.hooks.failure : L.hooks.success, a, 1);
}

bool complete(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
              const proto::ProtoObject* value, bool failed) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    // One CAS claims the completion, and only the winner writes the value: a
    // loser must not overwrite the value that belongs to the winner. The
    // transient kClaiming state still reads as pending, so no reader can see a
    // completed future without its value (protoClojure's deliverPromise, with
    // the claim and the write separated).
    if (!fut->setAttributeIfEqual(ctx, L.fstateKey, proto::makeSmallInt(kPending),
                                  proto::makeSmallInt(kClaiming)))
        return false;  // someone else completed it first
    fut->setAttribute(ctx, failed ? L.ferrorKey : L.fvalueKey, value ? value : PROTO_NONE);
    fut->setAttribute(ctx, L.fstateKey, proto::makeSmallInt(failed ? kFailure : kSuccess));
    // Waiters are read after the final state is stored: a waiter that
    // registered before it is in this list, and one that registers after sees
    // the settled state and does not park (design note 8).
    const proto::ProtoObject* waiters = fut->getOwnAttributeDirect(ctx, L.waitersKey);
    if (waiters && waiters != PROTO_NONE) {
        const proto::ProtoList* list = waiters->asList(ctx);
        for (unsigned long i = 0; i < list->getSize(ctx); ++i)
            ActorScheduler::instance().resume(ctx, list->getAt(ctx, static_cast<int>(i)));
    }
    runContinuations(ctx, L, f);
    wakeBlockedThreads();
    return true;
}

bool completeError(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
                   const char* className, const std::string& message) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoObject* a[2] = {makeString(&scope, className), makeString(&scope, message)};
    const proto::ProtoObject* err = callHook(&scope, L.hooks.runtimeError, a, 2);
    scope.returnValue = err;
    return complete(&scope, L, f, err, /*failed=*/true);
}

bool addWaiter(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
               const proto::ProtoObject* actor) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    for (;;) {
        if (state(ctx, L, f) != kPending) return false;
        proto::ProtoContext scope(ctx->space, ctx);
        scope.resizeAutomaticLocals(1);
        const proto::ProtoObject* cur = fut->getOwnAttributeDirect(&scope, L.waitersKey);
        scope.setAutomaticLocal(0, cur);   // rooted across appendLast (P1)
        const proto::ProtoObject* next =
            cur->asList(&scope)->appendLast(&scope, actor)->asObject(&scope);
        scope.returnValue = next;
        if (fut->setAttributeIfEqual(&scope, L.waitersKey, cur, next))
            // Re-check: a completion that won between the state read and this
            // CAS has already walked the old list, so we must not park.
            return state(&scope, L, f) == kPending;
    }
}

namespace {

// Runs every continuation registered on a completed future. Called by
// complete() on the completing thread and by onComplete() when the future is
// already done (D48).
void runContinuations(proto::ProtoContext* ctx, const RuntimeLayout& L,
                      const proto::ProtoObject* f) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    proto::ProtoContext held(ctx->space, ctx);
    held.resizeAutomaticLocals(1);
    const proto::ProtoObject* conts = fut->getOwnAttributeDirect(&held, L.contsKey);
    if (!conts || conts == PROTO_NONE || conts->asList(&held)->getSize(&held) == 0) return;
    held.setAutomaticLocal(0, conts);   // rooted across newList and after the CAS (P1)
    // Take the list once: a continuation registered after this point is run by
    // onComplete itself, because the future is already completed.
    if (!fut->setAttributeIfEqual(&held, L.contsKey, conts, held.newList()->asObject(&held)))
        return;
    const proto::ProtoList* list = conts->asList(&held);
    ActorTurnPause pause;
    for (unsigned long i = 0; i < list->getSize(&held); ++i) {
        proto::ProtoContext scope(held.space, &held);
        const proto::ProtoObject* arg = tryOf(&scope, L, f);
        scope.returnValue = arg;
        try {
            callHook(&scope, list->getAt(&scope, static_cast<int>(i)), &arg, 1);
        } catch (ScalaError& e) {
            std::fflush(stdout);
            std::fprintf(stderr, "protoscala: future continuation failed: %s\n", e.what());
        }
    }
}

} // namespace

void onComplete(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
                const proto::ProtoObject* fn) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    for (;;) {
        if (state(ctx, L, f) != kPending) {
            proto::ProtoContext scope(ctx->space, ctx);
            const proto::ProtoObject* arg = tryOf(&scope, L, f);
            scope.returnValue = arg;
            ActorTurnPause pause;
            try {
                callHook(&scope, fn, &arg, 1);
            } catch (ScalaError& e) {
                std::fflush(stdout);
                std::fprintf(stderr, "protoscala: future continuation failed: %s\n", e.what());
            }
            return;
        }
        proto::ProtoContext scope(ctx->space, ctx);
        scope.resizeAutomaticLocals(1);
        const proto::ProtoObject* cur = fut->getOwnAttributeDirect(&scope, L.contsKey);
        scope.setAutomaticLocal(0, cur);   // rooted across appendLast (P1)
        const proto::ProtoObject* next =
            cur->asList(&scope)->appendLast(&scope, fn)->asObject(&scope);
        scope.returnValue = next;
        if (fut->setAttributeIfEqual(&scope, L.contsKey, cur, next)) {
            // A completion that won between the state read and this CAS has
            // already taken the old list, so run it here instead.
            if (state(&scope, L, f) != kPending) runContinuations(&scope, L, f);
            return;
        }
    }
}

const proto::ProtoObject* awaitBlocking(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* f) {
    // The future's state is a ProtoObject attribute, which an unmanaged thread
    // may NOT read (the UnmanagedScope contract), so the predicate is the plain
    // epoch and the state is re-read after leaving the region on every wake.
    for (;;) {
        // The epoch is read BEFORE the state, so a completion that lands
        // between the two bumps it and the predicate below returns at once.
        // Reading it after the state (inside the lock) loses exactly that
        // wake-up and costs the full 50 ms bound on every ask.
        const unsigned long long seen = g_epoch.load(std::memory_order_acquire);
        if (state(ctx, L, f) != kPending) break;  // managed: the read is legal here
        g_blocked.fetch_add(1, std::memory_order_seq_cst);
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);  // opened before the lock (P6)
            std::unique_lock<std::mutex> lk(g_waitMutex);
            // The 50 ms bound is a safety net, not a poll: it turns a lost
            // wake-up into a 50 ms delay instead of a hung process.
            g_waitCv.wait_for(lk, std::chrono::milliseconds(50),
                              [&] { return g_epoch.load(std::memory_order_acquire) != seen; });
        }
        g_blocked.fetch_sub(1, std::memory_order_seq_cst);
    }
    return result(ctx, L, f);
}

} // namespace protoScala::futures
