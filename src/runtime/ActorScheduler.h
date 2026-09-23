/*
 * ActorScheduler — the process-wide actor pool (DESIGN §8.2).
 *
 * Actors are mutable protoCore objects holding their handler, their state and
 * three per-band mailboxes; every one of those is an attribute, so the GC
 * traces the whole live set from one registry list pinned in a root-context
 * slot (P1, D46). Workers are protoCore threads created with
 * ProtoSpace::newThread; they pop from three lock-free ready stacks in strict
 * priority order, drain a batch of eight messages per turn, and spin before
 * parking inside a ProtoContext::UnmanagedScope (P6).
 */
#pragma once
#include "runtime/ReadyStack.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <semaphore>
#include <vector>

namespace protoScala {

class ExecutionEngine;

enum class Band : unsigned { High = 0, Medium = 1, Low = 2 };
inline constexpr unsigned kBands = 3;
inline constexpr unsigned kBatchSize = 8;  // messages per turn (DESIGN §8.2, A0-10)
// protoST measured -91 % parks with this budget (8 rounds x 256 pauses, about
// 2-4 us): a single-sender workload otherwise pays a full futex cycle per
// message. These are the only tuning constants in the scheduler.
inline constexpr unsigned kSpinRounds = 8;
inline constexpr unsigned kSpinPauses = 256;

// Per-actor scheduling state. The single source of truth for the
// single-method invariant is `sched`:
//   0 Idle         not queued, not running
//   1 Claimed      queued on a ready stack, or running on a worker
//   2 ClaimedWake  running, and a wake arrived: re-enqueue at end of turn
//   3 Suspended    parked on a future; still claimed, so no other worker runs it
//
// P1 note: `actor` is the one ProtoObject* a C++ structure of this phase
// holds. It is safe because the actor is anchored in the registry list for the
// whole session (D46) and an ActorState is never freed before the scheduler is.
struct ActorState {
    const proto::ProtoObject* actor = nullptr;
    std::atomic<unsigned> sched{0};
    unsigned pendingIdx[kBands] = {0, 0, 0};  // read cursor into __pend<n>__
};

class ActorScheduler {
public:
    static ActorScheduler& instance();

    // Idempotent; the first call snapshots the runtime blueprint and starts
    // the workers. Called from the first Actor.spawn, so a script that uses no
    // actor pays nothing (DESIGN §1: cold start).
    void ensureStarted(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const RuntimeLayout& layout, ExecutionEngine* engine);
    // Joins every worker. Safe to call when nothing was started, and twice.
    void shutdown(proto::ProtoContext* ctx);

    const proto::ProtoObject* spawn(proto::ProtoContext* ctx, const proto::ProtoObject* handler,
                                    const proto::ProtoObject* initialState);
    // Queues `envelope` (a message object) and schedules the actor if needed.
    void send(proto::ProtoContext* ctx, const proto::ProtoObject* actor, Band band,
              const proto::ProtoObject* envelope);
    // A completed future wakes an actor suspended on it.
    void resume(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    unsigned workerCount() const;
    long long messagesProcessed() const;
    unsigned suspendedCount() const;
    bool isActor(proto::ProtoContext* ctx, const proto::ProtoObject* v) const;
    bool isStarted() const { return started_.load(std::memory_order_acquire); }
    static unsigned configuredWorkerCount();  // PROTOSCALA_ACTOR_WORKERS
    void workerLoop(proto::ProtoContext* ctx);  // public: the thread entry needs it

private:
    ActorScheduler() = default;
    ActorState* stateOf(proto::ProtoContext* ctx, const proto::ProtoObject* actor) const;
    void enqueue(ActorState* a, Band band);
    ActorState* dequeue();
    bool drainOne(proto::ProtoContext* ctx);  // one turn; false when nothing was ready
    bool runTurn(proto::ProtoContext* ctx, ActorState* a);  // true: the actor suspended
    void finishTurn(proto::ProtoContext* ctx, ActorState* a, bool suspended);
    const proto::ProtoObject* nextMessage(proto::ProtoContext* ctx, ActorState* a, unsigned band);
    bool hasWork(proto::ProtoContext* ctx, ActorState* a) const;
    Band highestPendingBand(proto::ProtoContext* ctx, ActorState* a) const;
    void deliver(proto::ProtoContext* ctx, ActorState* a, const proto::ProtoObject* envelope,
                 bool* suspended);
    void resumeSuspendedTurn(proto::ProtoContext* ctx, ActorState* a, bool* suspended);
    // Applies the (newState, reply) rule of D45 and completes `future`.
    void applyHandlerResult(proto::ProtoContext* ctx, ActorState* a, const proto::ProtoObject* r,
                            const proto::ProtoObject* future);

    ReadyStack<ActorState*> ready_[kBands];
    std::counting_semaphore<(1 << 20)> work_{0};
    std::atomic<bool> started_{false}, shuttingDown_{false};
    std::atomic<long long> messages_{0};
    std::atomic<unsigned> suspended_{0};
    proto::ProtoSpace* space_ = nullptr;
    const RuntimeLayout* layout_ = nullptr;
    ExecutionEngine* engine_ = nullptr;
    std::vector<const proto::ProtoThread*> workers_;
    std::mutex ownersMtx_;
    std::deque<std::unique_ptr<ActorState>> owners_;
    std::once_flag startFlag_;
};

} // namespace protoScala
