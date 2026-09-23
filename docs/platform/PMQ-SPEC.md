# Platform spec: `ProtoMPSCQueue` (protoCore)

> **Status:** implemented (protoCore 2.1.0, branch `feature/mpsc-queue`, 2026-09-23). Maintainer decision
> (DESIGN R9, option 3): actor mailboxes are a **new protoCore type**, shared
> by protoScala, protoClojure and protoST. This is roadmap Phase P2, a
> prerequisite of protoScala Phase 5 and of the mailbox part of tracks C and S.

## 1. Problem

Every actor runtime in the family needs a multi-producer / single-consumer
mailbox that is lock-free on the send path and visible to the GC:

- **protoClojure** uses three `std::atomic<ActorMessage*>` stacks per actor on
  the C++ heap; the message function, arguments and promise are **not rooted**
  (`protoClojure/src/runtime/ActorScheduler.h:61-89`).
- **protoST** keeps an immutable `ProtoList` mailbox updated with
  `setAttributeIfEqual`: GC-safe, but every send rebuilds the list version
  (O(log n)) and retries under contention.

Neither is both lock-free-O(1) and GC-safe. Per P3 the missing capability is
added to protoCore as a new object type.

## 2. Semantics

`ProtoMPSCQueue` is a **mutable, lock-free, multi-producer / single-consumer
FIFO of `ProtoObject*` items whose contents are traced by the GC**.

- `push` may be called concurrently from any number of threads; it is
  lock-free and O(1).
- `takeAll` removes and returns every item pushed so far, in push order, as an
  immutable `ProtoList`. Only one consumer may call it at a time; the caller
  guarantees exclusivity (an actor runtime already does, through its claimed
  flag). A violation is a caller bug, not undefined memory behaviour: the
  implementation must stay memory-safe, only the partition of items between
  the racing consumers is unspecified.
- Items are never copied: the queue stores the pointer (O(1) message passing
  of immutable data).
- An item pushed before `takeAll` begins is returned by that call or a later
  one; never lost, never duplicated.

### 2.1 Public API

```cpp
class ProtoMPSCQueue {
public:
    // Any thread. Lock-free, O(1).
    void push(ProtoContext*, const ProtoObject* item) const;

    // Single consumer. Removes every queued item and returns them in FIFO
    // order; an empty list when nothing is queued.
    const ProtoList* takeAll(ProtoContext*) const;

    // Any thread; a snapshot that may be stale by the time it is used.
    bool isEmpty(ProtoContext*) const;

    const ProtoObject* asObject(ProtoContext*) const;
    unsigned long getHash(ProtoContext*) const;   // identity hash
};

// ProtoContext
const ProtoMPSCQueue* newMPSCQueue();

// ProtoObject
bool isMPSCQueue(ProtoContext*) const;
const ProtoMPSCQueue* asMPSCQueue(ProtoContext*) const;
```

An actor runtime holds one queue per priority band (three per actor).

## 3. GC constraints (binding)

protoCore's collector stops user threads only to take stack roots, the
mutables-tree root and global-structure roots; everything else is traced
concurrently, because everything reachable from those roots is immutable
(lessons of 2026-09-15). A lock-free queue is mutable state, so its design
must fit that model rather than import mutable-heap techniques:

1. **No new stop-the-world work.** Nothing proportional to queue length or
   node count may be done inside the pause.
2. **No write barriers, card marking or mutator-side GC bookkeeping** on
   `push`/`takeAll`.
3. **No lost items under concurrent marking.** An item pushed while the GC is
   marking must be reachable in that cycle, or protected by the pushing
   thread's context until a later cycle covers it (the pattern the
   `TupleInterner` uses: "a young cell of `context`, protected by it until the
   table snapshot of a later GC cycle covers the entry").
4. **The queue's reachable state is captured the way other mutable state is**,
   through the mutables-tree snapshot or an equivalent O(1)-per-queue capture
   agreed with the maintainer.

The implementation strategy that satisfies 1–4 (for example, a published
head pointer in the queue's mutable-root slot whose nodes are immutable once
linked, with the consumer detaching the whole chain by CAS) is chosen in
Task 0 of the P2 plan **with the maintainer**; this spec fixes the
constraints, not the algorithm.

## 4. Tagged-pointer budget

- `ProtoMPSCQueue` consumes **one** pointer tag for its public handle
  (PROTOMAP-SPEC §3 rules; after P1 and P2, 35 of 64 tags remain free).
- Queue nodes are internal cells with their own `CellType`; they take no tag
  and are never exposed as `ProtoObject*` words.
- There is no iterator: `takeAll` returns an ordinary `ProtoList`.

## 5. Tests (protoCore, GoogleTest)

- FIFO order for one producer; per-producer order preserved with several
  producers; `takeAll` on an empty queue returns an empty list.
- **No loss, no duplication:** 8 producers × 1M pushes against one consumer
  looping on `takeAll`; the multiset of consumed items equals the pushed one.
- **GC safety:** items referenced only by the queue, pushed while collections
  are forced under a low `ProtoSpace` memory limit (including pushes during
  concurrent marking); every item is recovered intact.
- A parked producer/consumer inside `UnmanagedScope` never delays a pause.
- Stress with ThreadSanitizer (an ASan/TSan build directory inside the
  protoCore tree).
- Microbenchmark (self-reporting): push throughput with 1/2/4/8 producers and
  `takeAll` batch cost, compared with protoST's CAS-list mailbox and
  protoClojure's atomic stacks on the same machine.

## 6. Rollout

1. Implement in protoCore with the tests above; minor version bump (ABI
   addition).
2. Full clean rebuild and test of every embedder (protoPython, protoJS —
   sequential build, sequential test262 — protoST, protoClojure, protoScala).
3. protoScala Phase 5 builds its mailboxes on it.
4. Track C: protoClojure's `ActorMessage` stacks are replaced by three
   `ProtoMPSCQueue`s per actor; messages become protoCore objects; its
   unrooted-payload defect is closed. `actor-bench.sh` is re-run before and
   after and both tables are recorded.
5. Track S: protoST's `__mailbox__` CAS list is replaced by `ProtoMPSCQueue`;
   its actor benchmarks are re-run before and after and both tables recorded.

## 7. Decisions (P2)

Recorded 2026-09-23. Every decision below was taken by the implementing
agent under the maintainer's authorisation for the overnight run, and is
**pending review**. A decision the maintainer overturns is re-planned, not
patched. The full argument for each is in
`docs/plans/2026-09-23-phase-p2-protompscqueue.md`, Task 0.

| Id | Decision | Taken by |
|---|---|---|
| D1 | GC design **(b′)**: an atomic `head` of immutable CAS-prepended nodes inside the queue cell, made safe without any stop-the-world capture by a **retain chain**. `takeAll` publishes a retain cell holding the chain onto `retained` *before* it detaches the chain from `head`; `processReferences` loads `head` *before* `retained`. Both orderings are load-bearing. | agent, pending review |
| D2 | `retained` is released by the consumer, at its first `takeAll` in a new GC cycle (`ProtoSpace::getGCCycleCount()` differs from the queue's `retainedEpoch`), inside the publish window. `processReferences` stays **non-destructive**: the young-chain walk is a second caller of it in the same cycle, so a destructive read there would be a silent second consumer. | agent, pending review |
| D3 | Critical-section scope in `takeAll`: **the publish window only** (read epoch → maybe release → load `head` → fill and publish the retain cell → detach). The O(n) reversal and `ProtoList` construction run outside it, so a large batch never delays a pause. | agent, pending review |
| D4 | Own prototype `ProtoSpace::mpscQueuePrototype` (one new field; layout change covered by the mandatory clean rebuild of every embedder). | agent, pending review |
| D5 | protoCore `2.0.0` → `2.1.0`; `SOVERSION` stays `2` (minor bump, ABI addition, §6.1). | agent, pending review |
| D6 | `implAsObject` for the internal node and retain cells returns the raw, untagged cell address (tag 0), exactly as `ProtoMapIteratorImplementation` does. Binding invariant: neither cell is ever handed to `newChild`, `addParent`, `getAttribute` or any other path that can reach the attribute chain. | agent, pending review |
| D7 | Stress-test size: 8 producers × 1,000,000 pushes under a bounded heap, scalable through `PMQ_STRESS_PUSHES` for a smaller local run. The default stays 1,000,000 so the spec's test is the one that runs in CI. | agent, pending review |
| D8 | `takeAll` on an empty queue returns a fresh empty `ProtoList` and allocates no retain cell (fast path: one atomic load before any allocation). | agent, pending review |
| D9 | One fresh retain cell per `takeAll`. The stack link must **not** be folded into the detached chain's own head node: that node may already be marked, the marker never revisits a marked cell, and the link would then be invisible — losing every older retained chain. | agent, pending review |
| D10 | Concurrency and GC tests spawn their producers with `ProtoSpace::newThread` (registered protoCore threads that park at stop-the-world), not raw `std::thread`s with an unregistered `ProtoContext`. An unregistered thread is not counted in `runningThreads`, so it never parks and the pause completes while it runs — which would make the tests weaker, not stronger, and silently invalidates the critical-section argument. | agent, pending review |

### Recorded, not asked (facts the maintainer should see)

- **Deviation from §3.4.** §3.4 says the queue's reachable state is captured
  "through the mutables-tree snapshot or an equivalent O(1)-per-queue
  capture". The chosen design **captures nothing**: it makes a lazy read of
  the two mutable words provably a superset of the chain that existed at the
  pause. That is strictly less pause work than either option the spec
  anticipated, but it is a deviation from the spec's framing and the
  maintainer should confirm it. The only pause addition in the whole phase is
  one `addRootObj(space->mpscQueuePrototype)`, O(1).
- **First protoCore cell with post-publication mutable state.** §2.1 declares
  `push`, `takeAll` and `isEmpty` `const`, so the queue's three words are
  `mutable std::atomic<...>`. Every field that is written after publication is
  atomic; fields written only in the constructor follow protoCore's existing
  convention.
- **A Vyukov-style exchange-based MPSC queue is impossible here.**
  `prev = head.exchange(n); prev->next = n;` leaves the chain briefly
  unlinked. A consumer can spin for the missing link; the collector cannot.
  CAS-prepend is not a matter of style, it is the only shape whose
  intermediate states the marker can read.
- **The one relaxed `gcCycleCount` read per `takeAll` batch is not a write
  barrier.** It does no work proportional to the references involved, writes
  nothing the collector reads, and its omission could only retain memory —
  never lose an object.
- **Price of D1/D2.** A consumed chain's *nodes* stay reachable until the
  consumer's next `takeAll` in a later GC cycle: at most one extra cycle of
  node retention. A queue that is never drained again keeps its last batch's
  nodes reachable until the queue itself becomes garbage, at which point the
  whole structure is collected normally.
- **ABA is impossible by construction and must stay that way.** Reusing the
  address `head` was loaded from would require a sweep to complete between the
  load and the CAS; a sweep requires a pause, and a pause requires this thread
  to park — which it does only in `allocCell`/`safepoint`, neither of which is
  reached inside the publish window. **No allocation and no protoCore call may
  be added between the load and the CAS.**
