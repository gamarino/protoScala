# Platform spec: `ProtoMPSCQueue` (protoCore)

> **Status:** specified, not implemented (2026-09-22). Maintainer decision
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
