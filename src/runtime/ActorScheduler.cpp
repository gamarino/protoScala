#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/FutureYield.h"
#include "runtime/Futures.h"
#include "runtime/Mailbox.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace protoScala {

namespace {
// The actor whose handler is running on this thread (nullptr outside a turn).
thread_local const proto::ProtoObject* tl_currentActor = nullptr;
} // namespace

const proto::ProtoObject* currentActor() { return tl_currentActor; }
void setCurrentActor(const proto::ProtoObject* actor) { tl_currentActor = actor; }

ActorScheduler& ActorScheduler::instance() {
    static ActorScheduler s;
    return s;
}

unsigned ActorScheduler::configuredWorkerCount() {
    if (const char* env = std::getenv("PROTOSCALA_ACTOR_WORKERS"))
        if (*env) {
            const int n = std::atoi(env);
            if (n >= 1 && n <= 64) return static_cast<unsigned>(n);
        }
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 4;
    const unsigned n = hc > 2 ? hc - 2 : 2;  // protoClojure's default
    return n > 16 ? 16u : n;
}

// The thread entry protoCore calls: the scheduler travels as a SmallInteger,
// exactly as a BytecodeModule address does in MAKE_FN.
static const proto::ProtoObject* workerEntry(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                             const proto::ParentLink*, const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    auto* s = reinterpret_cast<ActorScheduler*>(args->getAt(ctx, 0)->asLong(ctx));
    s->workerLoop(ctx);
    return PROTO_NONE;
}

void ActorScheduler::ensureStarted(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                                   const RuntimeLayout& layout, ExecutionEngine* engine) {
    std::call_once(startFlag_, [&] {
        space_ = space;
        layout_ = &layout;
        engine_ = engine;
        started_.store(true, std::memory_order_release);
        const unsigned n = configuredWorkerCount();
        const proto::ProtoString* name =
            proto::ProtoString::createSymbol(ctx, "protoscala-actor-worker");
        const proto::ProtoObject* handle =
            ctx->fromInteger(static_cast<long long>(reinterpret_cast<std::intptr_t>(this)));
        for (unsigned i = 0; i < n; ++i) {
            const proto::ProtoList* targs = ctx->newList()->appendLast(ctx, handle);
            workers_.push_back(space_->newThread(ctx, name, &workerEntry, targs, nullptr));
        }
    });
}

unsigned ActorScheduler::workerCount() const { return static_cast<unsigned>(workers_.size()); }
long long ActorScheduler::messagesProcessed() const {
    return messages_.load(std::memory_order_relaxed);
}
unsigned ActorScheduler::suspendedCount() const {
    return suspended_.load(std::memory_order_relaxed);
}

bool ActorScheduler::isActor(proto::ProtoContext* ctx, const proto::ProtoObject* v) const {
    if (!layout_ || !v || v == PROTO_NONE || !isObjectCellFast(v)) return false;
    return v->hasOwnAttribute(ctx, layout_->stateRefKey) == PROTO_TRUE;
}

ActorState* ActorScheduler::stateOf(proto::ProtoContext* ctx, const proto::ProtoObject* actor) const {
    const proto::ProtoObject* ref = actor->getOwnAttributeDirect(ctx, layout_->stateRefKey);
    if (!ref || ref == PROTO_NONE)
        throw ScalaError("IllegalArgumentException", "not an actor");
    return reinterpret_cast<ActorState*>(static_cast<std::intptr_t>(ref->asLong(ctx)));
}

const proto::ProtoObject* ActorScheduler::spawn(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* handler,
                                                const proto::ProtoObject* initialState) {
    const RuntimeLayout& L = *layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    auto* actor = const_cast<proto::ProtoObject*>(L.actorProto->newChild(&scope, /*isMutable=*/true));
    scope.returnValue = actor;
    actor->setAttribute(&scope, L.handlerKey, handler);
    actor->setAttribute(&scope, L.actorStateKey, initialState);
    for (unsigned b = 0; b < kBands; ++b) {
        actor->setAttribute(&scope, L.mailboxKey[b], Mailbox::create(&scope));
        actor->setAttribute(&scope, L.pendingKey[b], scope.newList()->asObject(&scope));
    }
    ActorState* st;
    {
        std::lock_guard<std::mutex> g(ownersMtx_);
        owners_.push_back(std::make_unique<ActorState>());
        st = owners_.back().get();
    }
    st->actor = actor;
    actor->setAttribute(&scope, L.stateRefKey,
                        scope.fromInteger(static_cast<long long>(reinterpret_cast<std::intptr_t>(st))));
    // Anchor before returning: from here the GC reaches the actor, its
    // mailboxes and everything they hold from a root slot (D46).
    auto* reg = const_cast<proto::ProtoObject*>(L.actorRegistry);
    for (;;) {
        proto::ProtoContext one(scope.space, &scope);
        one.resizeAutomaticLocals(1);
        const proto::ProtoObject* cur = reg->getOwnAttributeDirect(&one, L.actorsKey);
        one.setAutomaticLocal(0, cur);   // rooted across appendLast (P1)
        const proto::ProtoObject* next = cur->asList(&one)->appendLast(&one, actor)->asObject(&one);
        one.returnValue = next;
        if (reg->setAttributeIfEqual(&one, L.actorsKey, cur, next)) break;
    }
    return actor;
}

void ActorScheduler::send(proto::ProtoContext* ctx, const proto::ProtoObject* actor, Band band,
                          const proto::ProtoObject* envelope) {
    ActorState* a = stateOf(ctx, actor);
    const unsigned b = static_cast<unsigned>(band);
    // The message is queued BEFORE the claim is attempted: a sender that loses
    // the claim is safe, because the worker that holds it drains again before
    // it releases (DESIGN §8.2).
    Mailbox::push(ctx, actor->getOwnAttributeDirect(ctx, layout_->mailboxKey[b]), envelope);
    for (;;) {
        unsigned s = a->sched.load(std::memory_order_acquire);
        if (s == 0) {
            if (a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) {
                enqueue(a, band);
                return;
            }
            continue;
        }
        if (s == 1) {
            if (a->sched.compare_exchange_strong(s, 2, std::memory_order_acq_rel)) return;
            continue;
        }
        // 2: a wake is already marked. 3: suspended -- the message waits until
        // the handler that is parked on a future finishes (DESIGN §8.3).
        return;
    }
}

void ActorScheduler::resume(proto::ProtoContext* ctx, const proto::ProtoObject* actor) {
    ActorState* a = stateOf(ctx, actor);
    for (;;) {
        unsigned s = a->sched.load(std::memory_order_acquire);
        if (s == 3) {  // parked: wake it
            if (a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) {
                suspended_.fetch_sub(1, std::memory_order_relaxed);
                enqueue(a, highestPendingBand(ctx, a));
                return;
            }
            continue;
        }
        if (s == 1) {  // the suspension is still in flight: mark it, so
                       // finishTurn re-enqueues instead of parking (design note 8)
            if (a->sched.compare_exchange_strong(s, 2, std::memory_order_acq_rel)) return;
            continue;
        }
        if (s == 0) {  // a stale wake (the yield was refused): an empty turn
            if (a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) {
                enqueue(a, Band::Medium);
                return;
            }
            continue;
        }
        return;  // 2
    }
}

void ActorScheduler::enqueue(ActorState* a, Band band) {
    ready_[static_cast<unsigned>(band)].push(a);
    work_.release();
}

ActorState* ActorScheduler::dequeue() {
    for (unsigned b = 0; b < kBands; ++b)  // strict priority
        if (ActorState* a = ready_[b].pop()) return a;
    return nullptr;
}

bool ActorScheduler::hasWork(proto::ProtoContext* ctx, ActorState* a) const {
    const RuntimeLayout& L = *layout_;
    for (unsigned b = 0; b < kBands; ++b) {
        const proto::ProtoObject* pend = a->actor->getOwnAttributeDirect(ctx, L.pendingKey[b]);
        if (pend && pend != PROTO_NONE && a->pendingIdx[b] < pend->asList(ctx)->getSize(ctx))
            return true;
        if (!Mailbox::isEmpty(ctx, a->actor->getOwnAttributeDirect(ctx, L.mailboxKey[b])))
            return true;
    }
    return false;
}

Band ActorScheduler::highestPendingBand(proto::ProtoContext* ctx, ActorState* a) const {
    const RuntimeLayout& L = *layout_;
    for (unsigned b = 0; b < kBands; ++b) {
        const proto::ProtoObject* pend = a->actor->getOwnAttributeDirect(ctx, L.pendingKey[b]);
        if (pend && pend != PROTO_NONE && a->pendingIdx[b] < pend->asList(ctx)->getSize(ctx))
            return static_cast<Band>(b);
        if (!Mailbox::isEmpty(ctx, a->actor->getOwnAttributeDirect(ctx, L.mailboxKey[b])))
            return static_cast<Band>(b);
    }
    return Band::Medium;
}

// The message a turn should process next in `band`: the leftovers of the last
// takeAll first, then a fresh takeAll of the whole band.
const proto::ProtoObject* ActorScheduler::nextMessage(proto::ProtoContext* ctx, ActorState* a,
                                                      unsigned band) {
    const RuntimeLayout& L = *layout_;
    const proto::ProtoObject* pendObj = a->actor->getOwnAttributeDirect(ctx, L.pendingKey[band]);
    const proto::ProtoList* pend = pendObj->asList(ctx);
    if (a->pendingIdx[band] < pend->getSize(ctx))
        return pend->getAt(ctx, static_cast<int>(a->pendingIdx[band]++));
    const proto::ProtoList* batch =
        Mailbox::takeAll(ctx, a->actor->getOwnAttributeDirect(ctx, L.mailboxKey[band]));
    if (batch->getSize(ctx) == 0) return nullptr;
    // The batch left the mailbox, so nothing in the heap refers to it: root it
    // in this context before setAttribute allocates, then store it on the
    // actor, so the GC never sees it only from this C++ frame (P1).
    ctx->returnValue = batch->asObject(ctx);
    const_cast<proto::ProtoObject*>(a->actor)->setAttribute(ctx, L.pendingKey[band],
                                                            batch->asObject(ctx));
    a->pendingIdx[band] = 1;
    return batch->getAt(ctx, 0);
}

void ActorScheduler::applyHandlerResult(proto::ProtoContext* ctx, ActorState* a,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* future) {
    const RuntimeLayout& L = *layout_;
    auto* actor = const_cast<proto::ProtoObject*>(a->actor);
    const bool isTuple2 =
        r && r != PROTO_NONE && r->getAttribute(ctx, L.tuple2Key) == PROTO_TRUE;
    if (!isTuple2)
        throw ScalaError("IllegalArgumentException",
                         "an actor handler must return (newState, reply), got " +
                             typeName(ctx, L, r));
    actor->setAttribute(ctx, L.actorStateKey, r->getOwnAttributeDirect(ctx, L.tupleFieldKey[1]));
    if (future && future != PROTO_NONE)
        futures::complete(ctx, L, future, r->getOwnAttributeDirect(ctx, L.tupleFieldKey[2]), false);
}

// Runs one message: handler(state, msg) must return (newState, reply) (D45).
// The actor keeps its previous state when the handler fails (DESIGN §8.4).
void ActorScheduler::deliver(proto::ProtoContext* ctx, ActorState* a,
                             const proto::ProtoObject* envelope, bool* suspended) {
    const RuntimeLayout& L = *layout_;
    auto* actor = const_cast<proto::ProtoObject*>(a->actor);
    proto::ProtoContext call(ctx->space, ctx);
    call.resizeAutomaticLocals(4);
    const proto::ProtoObject** slot = call.getAutomaticLocals();
    slot[0] = actor->getOwnAttributeDirect(&call, L.handlerKey);
    slot[1] = actor->getOwnAttributeDirect(&call, L.actorStateKey);
    const proto::ProtoObject* msg = envelope->getOwnAttributeDirect(&call, L.msgKey);
    slot[2] = msg ? msg : PROTO_NONE;
    const proto::ProtoObject* fut = envelope->getOwnAttributeDirect(&call, L.futureKey);
    slot[3] = fut ? fut : PROTO_NONE;
    setCurrentActor(actor);
    try {
        const proto::ProtoObject* r = engine_->invoke(&call, slot[0], slot + 1, 2);
        call.returnValue = r;
        applyHandlerResult(&call, a, r, slot[3]);
    } catch (FutureYield&) {
        // The snapshot and __waiting_on__ are already on the actor (written by
        // `await` and by every frame on the way out). Remember the message's
        // own future so the resumed turn can complete it.
        if (slot[3] != PROTO_NONE) actor->setAttribute(&call, L.turnFutureKey, slot[3]);
        else actor->removeAttribute(&call, L.turnFutureKey);
        setCurrentActor(nullptr);
        *suspended = true;
        return;
    } catch (ScalaError& e) {
        // DESIGN §8.4: the ask's future fails, the error is reported, the actor
        // keeps its previous state and stays alive. Supervision is out of scope.
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n", e.what());
        if (slot[3] != PROTO_NONE)
            futures::completeError(&call, L, slot[3], e.className().c_str(), e.message());
    }
    setCurrentActor(nullptr);
}

void ActorScheduler::resumeSuspendedTurn(proto::ProtoContext* ctx, ActorState* a,
                                         bool* suspended) {
    const RuntimeLayout& L = *layout_;
    auto* actor = const_cast<proto::ProtoObject*>(a->actor);
    proto::ProtoContext turn(ctx->space, ctx);
    turn.resizeAutomaticLocals(3);
    const proto::ProtoObject** slot = turn.getAutomaticLocals();
    slot[0] = actor->getOwnAttributeDirect(&turn, L.snapshotKey);  // rooted before clearing
    const proto::ProtoObject* waited = actor->getOwnAttributeDirect(&turn, L.waitingOnKey);
    slot[1] = waited ? waited : PROTO_NONE;
    const proto::ProtoObject* tf = actor->getOwnAttributeDirect(&turn, L.turnFutureKey);
    slot[2] = tf ? tf : PROTO_NONE;
    if (slot[1] == PROTO_NONE) {  // nothing to wait for: drop the stale snapshot
        actor->removeAttribute(&turn, L.snapshotKey);
        actor->removeAttribute(&turn, L.waitingOnKey);
        actor->removeAttribute(&turn, L.turnFutureKey);
        return;
    }
    const long long st = futures::state(&turn, L, slot[1]);
    if (st == futures::kPending) {
        // Not the completion's wake-up: a message, or a wake marked while the
        // suspension was in flight. Stay parked.
        *suspended = true;
        return;
    }
    actor->removeAttribute(&turn, L.snapshotKey);
    actor->removeAttribute(&turn, L.waitingOnKey);
    actor->removeAttribute(&turn, L.turnFutureKey);
    if (st == futures::kFailure) {
        // The awaited future failed. Without exceptions (Phase 4) the handler
        // cannot catch it, so the suspended chain is abandoned and the ask
        // inherits the failure (D50).
        const proto::ProtoObject* err = futures::errorOf(&turn, L, slot[1]);
        if (slot[2] != PROTO_NONE) futures::complete(&turn, L, slot[2], err, /*failed=*/true);
        return;
    }
    setCurrentActor(actor);
    bool yielded = false;
    try {
        const proto::ProtoObject* r = engine_->resumeFrames(&turn, slot[0]->asList(&turn), 0,
                                                            futures::valueOf(&turn, L, slot[1]));
        turn.returnValue = r;
        applyHandlerResult(&turn, a, r, slot[2]);
    } catch (FutureYield&) {
        if (slot[2] != PROTO_NONE) actor->setAttribute(&turn, L.turnFutureKey, slot[2]);
        yielded = true;
    } catch (ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n", e.what());
        if (slot[2] != PROTO_NONE)
            futures::completeError(&turn, L, slot[2], e.className().c_str(), e.message());
    }
    setCurrentActor(nullptr);
    *suspended = yielded;
}

bool ActorScheduler::runTurn(proto::ProtoContext* ctx, ActorState* a) {
    const RuntimeLayout& L = *layout_;
    bool suspended = false;
    // A turn that starts on a suspended frame resumes it first.
    if (a->actor->hasOwnAttribute(ctx, L.snapshotKey) == PROTO_TRUE) {
        resumeSuspendedTurn(ctx, a, &suspended);
        if (suspended) return true;
    }
    // Up to kBatchSize messages, always taking the highest non-empty band
    // first: the band scan restarts after every message, so a High-band
    // message that arrives mid-turn wins the next slot (DESIGN §8.2).
    for (unsigned budget = kBatchSize; budget > 0; --budget) {
        proto::ProtoContext turn(ctx->space, ctx);  // one context per message (P2)
        const proto::ProtoObject* env = nullptr;
        for (unsigned band = 0; band < kBands && !env; ++band) env = nextMessage(&turn, a, band);
        if (!env) break;  // nothing queued: the turn is over
        turn.returnValue = env;
        // Counted before the handler runs, not after: an ask's future is
        // completed inside `deliver`, so a caller that has its reply must
        // already see the message in Actor.stats.
        messages_.fetch_add(1, std::memory_order_relaxed);
        deliver(&turn, a, env, &suspended);
        if (suspended) return true;
    }
    return false;
}

void ActorScheduler::finishTurn(proto::ProtoContext* ctx, ActorState* a, bool suspended) {
    if (suspended) {
        // Stay claimed across the suspension (the single-method invariant holds
        // through it, DESIGN §8.3). If a completion raced the suspension it
        // marked state 2; then park nothing and run again immediately.
        unsigned expected = 1;
        if (a->sched.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) {
            suspended_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        a->sched.store(1, std::memory_order_release);
        enqueue(a, highestPendingBand(ctx, a));
        return;
    }
    for (;;) {
        unsigned s = a->sched.load(std::memory_order_acquire);
        if (s == 2) {
            if (!a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) continue;
            if (hasWork(ctx, a)) {
                enqueue(a, highestPendingBand(ctx, a));
                return;
            }
            unsigned claimed = 1;
            if (a->sched.compare_exchange_strong(claimed, 0, std::memory_order_acq_rel)) return;
            continue;  // a sender re-marked 2: go round again
        }
        if (!a->sched.compare_exchange_strong(s, 0, std::memory_order_acq_rel)) continue;
        // Released. A sender racing this store sees 0 and enqueues itself; one
        // that raced just before marked 2 and the CAS above failed. Only the
        // backlog this turn already knows about is left to us.
        if (hasWork(ctx, a)) {
            unsigned idle = 0;
            if (a->sched.compare_exchange_strong(idle, 1, std::memory_order_acq_rel))
                enqueue(a, highestPendingBand(ctx, a));
        }
        return;
    }
}

bool ActorScheduler::drainOne(proto::ProtoContext* ctx) {
    ActorState* a = dequeue();
    if (!a) return false;
    bool suspended = false;
    try {
        suspended = runTurn(ctx, a);
    } catch (...) {  // never leave an actor claimed on an unknown error
        finishTurn(ctx, a, false);
        throw;
    }
    finishTurn(ctx, a, suspended);
    return true;
}

void ActorScheduler::workerLoop(proto::ProtoContext* ctx) {
    ExecutionEngine::ActiveCallGuard active(engine_, layout_);
    for (;;) {
        while (drainOne(ctx)) {}
        if (shuttingDown_.load(std::memory_order_acquire)) {
            while (drainOne(ctx)) {}  // a sender may have pushed after our last pop
            return;
        }
        // Spin before parking: protoST measured -91 % parks with this budget
        // (8 rounds x 256 pauses, about 2-4 us), because a single-sender
        // workload otherwise pays a full futex cycle per message.
        bool got = false;
        for (unsigned round = 0; round < kSpinRounds && !got; ++round) {
            for (unsigned i = 0; i < kSpinPauses; ++i)
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#else
                std::this_thread::yield();
#endif
            got = drainOne(ctx);
        }
        if (got) continue;
        // Quiescent: no frame is live, so this is where the young generation
        // goes back before the thread sleeps.
        ctx->safepoint();
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);  // opened before any lock
            work_.acquire();
        }
    }
}

void ActorScheduler::shutdown(proto::ProtoContext* ctx) {
    if (!started_.load(std::memory_order_acquire)) return;
    shuttingDown_.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < workers_.size(); ++i) work_.release();
    {
        // The join blocks, so this thread must leave the GC quorum first: a
        // worker still allocating would otherwise wait for a safepoint we can
        // no longer reach (P6).
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        for (const proto::ProtoThread* t : workers_)
            if (t) const_cast<proto::ProtoThread*>(t)->join(ctx);
    }
    workers_.clear();
    started_.store(false, std::memory_order_release);
    if (const unsigned n = suspended_.load(std::memory_order_relaxed))
        std::fprintf(stderr,
                     "protoscala: %u actor(s) were still waiting on a future that never "
                     "completed\n",
                     n);
}

} // namespace protoScala
