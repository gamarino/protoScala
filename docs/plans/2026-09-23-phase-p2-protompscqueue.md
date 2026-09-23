# Phase P2 — ProtoMPSCQueue (protoCore) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add to protoCore a new object type, `ProtoMPSCQueue` — a mutable, lock-free, multi-producer / single-consumer FIFO of `ProtoObject*` items whose contents are traced by the garbage collector — with `push` O(1) and allocation-bounded, `takeAll` returning an immutable `ProtoList` in FIFO order, and **no new stop-the-world work, no write barriers and no lost items under concurrent marking**. Then rebuild every embedder from clean. protoScala Phase 5, protoClojure (track C) and protoST (track S) build their actor mailboxes on it.

**Architecture:** Three new cell classes and one new pointer tag.

- `ProtoMPSCQueueImplementation` (tag 28, the public handle) holds three atomic words: `head`, `retained`, `retainedEpoch`.
- `ProtoMPSCQueueNodeImplementation` (internal, no tag) is an immutable-after-publication `{item, next}` node. Producers CAS-prepend to `head`; the chain is therefore **always fully linked** and always walkable by the collector — the property a Vyukov exchange-based MPSC queue does *not* have.
- `ProtoMPSCQueueRetainImplementation` (internal, no tag) is a `{chain, next}` cell pushed onto `retained` by `takeAll` **before** it detaches the chain from `head`.

The GC integration is *only* `processReferences`. The collector is not modified at all: nothing is added to the pause, no registry is walked, no phase is added. Correctness rests on a monotonicity argument proved in Task 2 and restated in the source:

> During a cycle whose stop-the-world happened at time `S`, `head` may only shrink by a `takeAll`, and every node a `takeAll` removes is in `retained` *before* it leaves `head`; `retained` only grows during that cycle. The marker therefore loads `head` **first** and `retained` **second**, and the union of the two reads is a superset of the chain that existed at `S`.

`retained` is released by the consumer at the first `takeAll` it performs in a *new* GC cycle (`ProtoSpace::getGCCycleCount()` changed), inside a `CriticalSection` window that contains no allocation and no safepoint — so the observed cycle number cannot go stale between the decision and the publish. `gcCycleCount` is incremented under stop-the-world at the *start* of a cycle (`core/ProtoSpace.cpp:579`, before `stwFlag.store(false)` at `:629`), and the collector thread is a single sequential thread, so observing `C+1` proves that the mark and sweep of cycle `C` have finished.

**Tech Stack:** C++20, CMake ≥ 3.16, GoogleTest 1.14 (FetchContent, `file(GLOB)` test registration), Linux `perf stat`, ThreadSanitizer, AddressSanitizer.

**Spec:** `protoScala/docs/platform/PMQ-SPEC.md` (also `protoScala/docs/DESIGN.md` §8 and §11 R9)

---

## Global Constraints

- **Semantics are fixed by PMQ-SPEC §2.** `push` is lock-free and O(1) from any thread. `takeAll` removes and returns every item pushed so far in push order, as an immutable `ProtoList`; the caller guarantees single-consumer exclusivity, and a violation must stay memory-safe (only the partition of items between racing consumers is unspecified). Items are never copied. An item pushed before a `takeAll` begins is returned by that call or a later one: never lost, never duplicated.
- **GC principle (tasks/lessons.md, 2026-09-15).** The only work allowed inside the pause is thread stack roots, the mutables-tree root and the roots of global structures. This plan adds **zero** stop-the-world work; its single new global root is one `addRootObj(space->mpscQueuePrototype)` (O(1), a global-structure root, the same addition `ProtoMap` made in P1). No write barriers, no card marking, no "scan young objects for pointers to older ones". Never reason from Go/JVM premises.
- **No mutator-side GC bookkeeping beyond one relaxed counter read per `takeAll` batch** (PMQ-SPEC §3.2, decision D2 in Task 0). It is not a barrier: it does no work proportional to the references involved, writes nothing the collector reads, and its omission would only retain memory — it can never lose an object.
- **The new type uses at most ONE new pointer tag** (`POINTER_TAG_MPSC_QUEUE = 28`) for its public handle. Node and retain cells are internal, told apart by `CellType`, and are never exposed as `ProtoObject*` words (PMQ-SPEC §4, PROTOMAP-SPEC §3 rules 1-3). The tag-budget comment block in `proto_internal.h` is updated: used 0-28 (29), free 29-63 (35) — exactly the "35 of 64 tags remain free" PMQ-SPEC §4 predicts.
- **There is no iterator.** `takeAll` returns an ordinary `ProtoList` (PMQ-SPEC §4).
- **`ProtoMap`, `ProtoSparseList`, `ProtoList` and the collector are not modified.** The only edits to existing files are additive: tag/CellType/union/`BigCell`/`ExpectedTag` entries, one `getPrototype` case, two `cellTypeName` cases plus one, one prototype field + its creation + its root, one `ProtoContext` factory, two `ProtoObject` accessors, and the CMake source list.
- **Every mutator publishes inside a `ProtoContext::CriticalSection`** (P1 convention, `headers/protoCore.h:1720`): the guard both enforces the heap ceiling at the outermost boundary and guarantees that no stop-the-world completes between the decision and the publish. It must **never** be held across the O(n) list build in `takeAll` (that would delay a pause for the length of a batch).
- Ask before any design decision. Task 0 lists every open decision. **For this phase the maintainer has authorised the agent to decide them tonight and review them tomorrow**, so each decision is recorded as *decided-by-the-agent-pending-review* with its alternatives and their consequences. A decision the maintainer overturns is re-planned, not patched.
- **Workspace safety (tasks/lessons.md, 2026-09-15):** create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. Scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/`. `perf stat` only, never `perf record`. No `rm -rf` outside that scratch directory and the build directories; no `cmake --install`.
- **Build parallelism: `-j4` maximum** for protoCore. **protoJS builds use no `-j` at all and run test262 sequentially (`TEST262_CONCURRENCY=1`); ask the user before any full sweep** (DEV12 hangs otherwise).
- **Git:** work on branch `feature/pmq-p2` in `protoCore`. Never push. Stage explicitly by path (`git add <file> <file>`), never `git add -A` or `git add .`. Commit with the repository's configured identity (name "Gustavo Marino"; never override `user.email`). End every commit message with:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```
- **Version:** protoCore `2.0.0` → `2.1.0`; `SOVERSION` stays `2` (PMQ-SPEC §6.1: minor bump, ABI addition). The `ProtoSpace` layout still changes (one new prototype field), so the clean rebuild of every embedder is mandatory regardless of the soname.
- All code comments, documentation and commit messages in professional English.

---

## Task 0: Maintainer decisions

Decided by the agent on 2026-09-23 under the maintainer's explicit authorisation and **approved by the maintainer on 2026-09-23**. The executor records each decision verbatim in a new section `## 7. Decisions (P2)` at the end of `protoScala/docs/platform/PMQ-SPEC.md` (Task 11) and commits that file in the protoScala repository.

### D1 — The GC design (PMQ-SPEC §3, the central problem) — **DECIDED: (b′)**

The three candidates, measured against the collector as it actually is (`core/ProtoSpace.cpp:360-790`), not against a model of it:

**(a) The published state lives in the mutable-root shard, updated by CAS on an immutable node chain.**
The queue is a mutable `ProtoObjectCell`; `push` derives a new head-chain and installs it with `setAttributeIfEqual`.
- *Correctness under concurrent marking:* perfect, and free. Phase 2 snapshots every shard root under stop-the-world (`ProtoSpace.cpp:502-506`) and the marker never reads the live table again, so a version published after the pause cannot hide anything the pause saw.
- *Cost per push:* a new `ProtoObjectCell`, a path copy of its attribute AVL, **and** a path copy of the shard's AVL keyed by `mutable_ref`, then a CAS on `mutableRoot[ref % 256]` — 3 to 5 cell allocations and O(log n) work, retried under contention, on a shard shared with unrelated objects. This is exactly protoST's `__mailbox__` today (`protoST/src/runtime/STRuntime.cpp:1374-1397`), the cost PMQ-SPEC §1 exists to remove.
- *Pause:* nothing added. *Collector requirements:* none.
- **Rejected:** it satisfies every constraint and fails the purpose.

**(b) An atomic head of immutable nodes inside the cell, plus a TupleInterner-style "capture under STW, walk concurrently" hand-off.**
- *Correctness:* sound. The hand-off is not an optimisation here, it is a necessity: a lazily-read mutable word is unsound on its own (see the unsoundness proof below), so the head must be read while the world is stopped.
- *Cost per push:* one cell, one CAS on a word private to this queue. Optimal.
- *Pause:* the capture needs a registry of live queues and O(#live queues) atomic loads inside the window — 3 queues per actor, so 10,000 actors is 30,000 loads. PMQ-SPEC §3.4 explicitly allows "an equivalent O(1)-per-queue capture agreed with the maintainer", but it *is* new pause work that grows with the program, which is what the 2026-09-15 lesson forbids growing.
- *Collector requirements:* a `ProtoSpace` registry with weak entries (a strong one would make every queue immortal), a `finalize` hook to mark an entry dead before its cell is recycled, and dead-entry compaction. Substantial, and every piece of it is new machinery in the one file that must stay simple.

**(b′) The same atomic head, made safe without any capture, by a retain chain. — CHOSEN.**
`takeAll` publishes a retain cell holding the chain onto the queue's `retained` stack **before** detaching the chain from `head`; `processReferences` loads `head` **before** `retained`. Then, for the whole of a cycle, the pair (head, retained) read in that order is a superset of the chain that existed at the pause.
- *Correctness under concurrent marking:* proved in Task 2, Step 1, and encoded as tests in Tasks 5 and 6.
- *Cost:* `push` — one cell, one CAS. `takeAll` — one extra cell **per batch** (not per item), one CAS, one exchange, one relaxed read of `gcCycleCount`.
- *Pause:* **nothing.** *Collector requirements:* **none.** Only `processReferences` and the ordinary global-prototype root.
- *Price:* a consumed chain's *nodes* stay reachable until the consumer's next `takeAll` in a later GC cycle — at most one extra cycle of node retention (the items themselves are held by the returned `ProtoList` for as long as the actor is processing them anyway). protoCore defers collection by design; this is the same bargain the survivor pen already makes.

**(c) Items also anchored in a protoCore structure the GC already traces, with the atomic chain as a pure fast path.**
- *Correctness:* sound only if the anchor is updated on the same schedule as the items leave the young generation.
- *Cost:* anchoring on every push is (a)'s cost *plus* the atomic chain. Anchoring in batches leaves the unanchored items protected by the pushing thread's context (PMQ-SPEC §3.3 allows exactly that) but needs an epoch-driven flush — i.e. (b′)'s bookkeeping *and* (a)'s allocations, with two structures to keep consistent and two chances to lose an item.
- **Rejected:** strictly dominated by (b′).

**Why a lazily-read mutable word is unsound on its own** (the finding that selects the design): the marker runs *after* the pause (`ProtoSpace.cpp:629` resumes the world, the mark loop starts at `:711`). Suppose the queue cell simply reported `head.load()`. A `takeAll` that runs before the marker reaches that cell moves items into a fresh `ProtoList` `L`. `L` was allocated after the pause, so it sits in front of the young-chain head captured in Phase 2 and **its references are not walked this cycle** (`ProtoSpace.cpp:649-679`). `L` itself is not a candidate and survives, but the items are old cells inside `segmentsToProcess`, now unmarked and unreachable from any root the marker will read — sweep frees them under a live `ProtoList`. The retain chain is precisely what keeps the pause-time path alive until the marker has read it.

**ABA is impossible by construction, and must stay that way.** `push` CASes `head` from `h_old` to a fresh node whose `next` is `h_old`. Address reuse of `h_old` would need a full sweep to complete between the load and the CAS; a sweep requires a stop-the-world, a stop-the-world requires every running thread to park, and this thread parks only in `allocCell`/`safepoint` — neither of which is reached between the load and the CAS. The `CriticalSection` around the publish makes that an enforced invariant rather than an assumed one (`allocCell` and `safepoint` both skip parking while `criticalSectionDepth > 0`, `ProtoContext.cpp:428` and `:353`). **No allocation and no protoCore call may be added inside a publish window.**

- Alternatives if the maintainer prefers them: (b) is a drop-in replacement for the release mechanism (the `retained` chain stays, the epoch guard is replaced by a post-mark registry walk) and costs one pause addition; (a) is a rewrite of Task 3 and drops Tasks 2's retain cell entirely.

### D2 — When `retained` is released — **DECIDED: the consumer, at its first `takeAll` in a new GC cycle**

- **Chosen.** `takeAll` reads `ProtoSpace::getGCCycleCount()` inside the publish window; if it differs from `retainedEpoch`, the current `retained` stack is exchanged to `nullptr` and the epoch is updated. Because the window contains no allocation and no safepoint, no cycle can begin between the read and the publish, so a retain cell is never released in the cycle it was created. Observing `C` proves the pause of cycle `C` has happened, which proves the mark of cycle `C-1` finished (single collector thread, `STW → mark → sweep` in sequence), which proves everything released was already traced.
- **Alternative (i): destructive `processReferences`** — the marker `exchange`s `retained` to `nullptr` and reports what it took. Sound (it reports everything it removes) and it releases a cycle earlier, but it turns the collector's read-only tracing hook into a mutator. Any future non-GC caller — a heap dumper, a diagnostic, a test that calls `processReferences` directly — would silently drop untraced chains. Rejected as a landmine.
- **Alternative (ii): a `ProtoSpace` registry walked by the collector after the mark loop** — no mutator-side read at all, releases in the same cycle, but re-introduces the weak registry, the `finalize` hook and the compaction that D1 (b) was rejected for.
- **Consequence of the choice:** a queue that is never drained again keeps its last batch's nodes reachable forever. That is one batch per abandoned queue, and an abandoned queue is itself garbage the moment nothing references it — at which point the whole structure, retain chain included, is collected normally.

### D3 — Critical-section scope in `takeAll` — **DECIDED: the publish window only**

- **Chosen.** The `CriticalSection` covers `read epoch → maybe release → load head → fill and publish the retain cell → detach`. The O(n) reversal and `ProtoList` construction run **outside** it. Holding it across a 1,000,000-item batch would block a stop-the-world for the whole batch, which is a soft-real-time regression far worse than anything it protects against.
- **Why it is safe to allocate outside the guard:** during the build the items are reachable from the detached chain, which is reachable from `retained`, which is reachable from the queue cell, which the caller keeps reachable. The half-built list's cells are young cells of the caller's context: not candidates of the running cycle, and walked by the next cycle's young-chain scan.
- **Caller contract to document:** the queue must stay reachable for the duration of `takeAll`. An actor mailbox held in an attribute satisfies this; a queue held only in a C++ local does not, exactly as for every other protoCore handle.
- **Alternative:** guard the whole call. Rejected for the pause.

### D4 — Prototype — **DECIDED: its own `ProtoSpace::mpscQueuePrototype`**

- **Chosen**, mirroring `ProtoMap` (P1 D5). Adds one `ProtoSpace` field (layout change, already covered by the mandatory clean rebuild) and one `addRootObj` under stop-the-world — O(1), a global-structure root, allowed by the GC principle.
- **Alternative:** reuse `objectPrototype`. No layout change, but embedders cannot attach queue methods to a distinct prototype and `getPrototype` becomes uninformative.

### D5 — Version and SOVERSION — **DECIDED: `2.0.0` → `2.1.0`, `SOVERSION` stays `2`**

- PMQ-SPEC §6.1 asks for a minor bump for an ABI addition. The `ProtoSpace` layout does change, so a stale embedder binary would be silently wrong; the mandatory clean rebuild of Task 12 is what protects against that, as it did in P1.
- **Alternative:** `SOVERSION 3`, which turns a stale binary into a load-time error. Rejected because 2.0.0 shipped four days ago and a second soname bump in a week is churn for every packager; revisit if the maintainer disagrees.

### D6 — `implAsObject` for the internal cells — **DECIDED: the raw, untagged cell address (tag 0)**

- **Chosen**, exactly as `ProtoMapIteratorImplementation` does (P1 D1 = (a)). `Cell::implAsObject` is pure virtual, so the internal cells must return something; the raw address is honest and debuggable.
- **Binding invariant (the #92 class of bug, `proto_internal.h:208-218`):** neither cell is ever handed to `newChild`, `addParent`, `getAttribute` or any other path that can reach the attribute chain. `ProtoMPSCQueue` exposes no accessor that returns one, and `takeAll` returns items, never nodes. A test in Task 2 asserts that the public API never yields a tag-0 word.
- **Alternative:** return `PROTO_NONE`. Safer against a future mistake, but it makes a GC diagnostic print `None` for a real cell.

### D7 — Stress-test size — **DECIDED: 8 producers × 1,000,000 pushes under a hard heap limit, env-scalable**

- PMQ-SPEC §5 asks for 8 × 1M. 8,000,000 nodes at 64 bytes is 512 MB of nodes alone, and DEV12 is OOM-sensitive (memory: zvol-swap deadlock, earlyoom). The test therefore sets `space.setHeapLimits(0, heapSize + headroom)` so cycles actually run and the heap stays bounded — which doubles as the "pushes during concurrent marking" stress — and honours `PMQ_STRESS_PUSHES` for a smaller local run. The default stays 1,000,000 so the spec's test is the one that runs in CI.
- **Alternative:** lower the default to 125,000 and gate 1M behind the env var. Rejected: a stress test nobody runs at full size is not a stress test.

### D8 — `takeAll` on an empty queue — **DECIDED: returns a fresh empty `ProtoList` (one Small cell), allocates no retain cell**

- Matches `ProtoContext::newList()` semantics everywhere else in protoCore, and the fast path costs one atomic load before any allocation.
- **Alternative:** a cached per-space empty list. Rejected: `ProtoList` handles are compared by identity in some embedders, and a shared singleton would change that.

### Recorded, not asked (facts the maintainer should see)

- PMQ-SPEC §3.4 says the queue's reachable state is captured "through the mutables-tree snapshot or an equivalent O(1)-per-queue capture". **The chosen design captures nothing**: it makes the lazy read provably safe instead. That is strictly less pause work than either option the spec anticipated, but it is a deviation from the spec's framing and the maintainer should confirm it.
- PMQ-SPEC §2.1 declares `push`, `takeAll` and `isEmpty` `const`. The queue's three mutable words are therefore declared `mutable std::atomic<...>`, as `ProtoMap`'s value slot is not — this is the first protoCore cell with post-publication mutable state, and the reason every one of those words is atomic.
- A Vyukov-style exchange-based MPSC queue (`prev = head.exchange(n); prev->next = n;`) is **impossible here**: it leaves the chain temporarily unlinked, and while a consumer can spin for the missing link, the garbage collector cannot. CAS-prepend is not a choice of style; it is the only shape whose intermediate states the marker can read.
- `Cell` fields other than the three atomics are written in the constructor *after* `Cell(context)` has already published the cell to the young chain (`addCell2Context`). protoCore already relies on "a reference field is written once, at construction, so a cell still being constructed at the capture is read with either its initial null or its final value" (`ProtoSpace.cpp:669-672`). The new cells follow that existing convention; only fields mutated *after* publication are atomic. Task 8 measures this against a TSan baseline rather than pretending protoCore is race-free today.
- protoClojure's `ActorState` (`protoClojure/src/runtime/ActorScheduler.h:61-89`) already uses exactly this algorithm — three `std::atomic<ActorMessage*>` CAS-prepend stacks, exchanged and reversed by the worker. `ProtoMPSCQueue` is that algorithm with the payload made GC-visible, which is why track C is a mechanical replacement.

---

## File Structure

protoCore (`/home/gamarino/Documentos/proyectos/protoCore`):

| File | Change | Responsibility |
|---|---|---|
| `core/ProtoMPSCQueue.cpp` | **Create** | The three cell classes' constructors, `implAsObject` and `processReferences`; the public `ProtoMPSCQueue` trampolines (`push`, `takeAll`, `isEmpty`, `asObject`, `getHash`). Carries the GC correctness proof as a file-header comment. |
| `headers/proto_internal.h` | Modify | Forward declarations (`:55-57` block); `ProtoObjectPointer` union members (`:110-112` block); `POINTER_TAG_MPSC_QUEUE 28` and the tag-budget comment (`:237-273`); `ExpectedTag` specialisations (after `:332`); three `CellType` values (`:591-624`); the three cell classes (after `:1743`); `BigCell` union (`:1926-1954`); `static_assert`s (`:1956-1981`). |
| `headers/protoCore.h` | Modify | Forward declaration (`:31` block); `ProtoObject::isMPSCQueue` (near `:593`) and `asMPSCQueue` (near `:634`); the `ProtoMPSCQueue` class (after the `ProtoMap` class); `ProtoContext::newMPSCQueue` (after `:1534`); `ProtoSpace::mpscQueuePrototype` (after `:1871`). |
| `core/ProtoContext.cpp` | Modify (after `:686`) | `newMPSCQueue()`. |
| `core/ProtoObject.cpp` | Modify | `getPrototype` case (after `:600`); `isMPSCQueue`/`asMPSCQueue` (after `:1930`). |
| `core/ProtoSpace.cpp` | Modify | `cellTypeName` (`:126-164`) — three names; prototype creation (`:1193`); GC global root (`:423`). |
| `CMakeLists.txt` | Modify | Add `core/ProtoMPSCQueue.cpp` (`:32-58`) and `performance/mpsc_queue_benchmark.cpp` (`:142-177`); version `2.0.0` → `2.1.0` (`:8`). |
| `performance/mpsc_queue_benchmark.cpp` | **Create** | Self-reporting microbenchmark: 1/2/4/8 producers, `takeAll` batch cost, against a protoClojure-shaped C++ atomic stack and a protoST-shaped `setAttributeIfEqual` `ProtoList` mailbox, in one process on one machine. |
| `test/ProtoMPSCQueueCellTests.cpp` | **Create** | Tag, `CellType`, `BigCell` size, `processReferences` of each cell, the head-before-retained order, embedded items not traced, no tag-0 word escapes the public API. |
| `test/ProtoMPSCQueueTests.cpp` | **Create** | FIFO for one producer, empty `takeAll`, `isEmpty`, object-model integration (`isMPSCQueue`, `asMPSCQueue`, `asObject`, `getPrototype`, identity `getHash`). |
| `test/ProtoMPSCQueueConcurrencyTests.cpp` | **Create** | 8 producers × 1M with one looping consumer: no loss, no duplication, per-producer order preserved. |
| `test/ProtoMPSCQueueGCTests.cpp` | **Create** | Items referenced only by the queue survive forced cycles under a low heap limit; pushes *during* concurrent marking; a `takeAll` that races the marker; a parked producer/consumer inside `UnmanagedScope` never delays a pause. |
| `CHANGELOG.md` | Modify | `[2.1.0]` entry. |
| `docs/GarbageCollector.md` | Modify | New subsection "Lock-free queues without barriers" recording the monotonicity argument next to "Concurrent Mark Without Barriers". |

protoScala (`/home/gamarino/Documentos/proyectos/protoScala`):

| File | Change | Responsibility |
|---|---|---|
| `docs/platform/PMQ-SPEC.md` | Modify | Status line → implemented; new `## 7. Decisions (P2)` recording D1-D8 verbatim. |

**Every** place in protoCore that switches on a pointer tag or `CellType`, and what the new type needs there (found by reading each site, not by assuming):

| Site | What it does | P2 action |
|---|---|---|
| `headers/proto_internal.h:230-236` | `POINTER_TAG_*` definitions, currently 0-27 | add `POINTER_TAG_MPSC_QUEUE 28` (Task 2) |
| `headers/proto_internal.h:237-273` | tag-budget comment block (`used 0-27 (28), free 28-63 (36)`) | update to `used 0-28 (29), free 29-63 (35)`; record that tag 28 carries exactly one cell type (Task 2) |
| `headers/proto_internal.h:82-135` | `ProtoObjectPointer` union members | add `mpscQueue`, `mpscQueueImplementation` (Task 2) |
| `headers/proto_internal.h:321-332` + the block to `:470` | `ExpectedTag<T>` specialisations, used by `toImpl`'s debug assertions | add queue → tag 28; node and retain → `POINTER_TAG_OBJECT` (D6), exactly as `ProtoMapIteratorImplementation` (Task 2) |
| `headers/proto_internal.h:591-624` | `enum class CellType` | append `MPSCQueue`, `MPSCQueueNode`, `MPSCQueueRetain` (Task 2) |
| `headers/proto_internal.h:1926-1954` | `BigCell` union (sizeof gate) | add the three classes (Task 2) |
| `headers/proto_internal.h:1956-1981` | `static_assert(sizeof(X) <= 64)` | add three asserts (Task 2) |
| `headers/proto_internal.h:768-777` | `isObjectFast` — tag 0 + `getType() == CellType::Object` | none: node/retain cells are tag 0 but never reach it, and the virtual `getType()` already rejects them if one ever does (Task 2 asserts this) |
| `core/ProtoSpace.cpp:126-164` | `cellTypeName` debug switch (compiler warns on a missing enumerator) | add three names (Task 4) |
| `core/ProtoSpace.cpp:412-441` | STW global roots (`addRootObj` per prototype) | add `addRootObj(space->mpscQueuePrototype)` (Task 4, D4) — O(1), the only pause addition in this plan |
| `core/ProtoSpace.cpp:1192-1193` | prototype creation in the `ProtoSpace` constructor | create `mpscQueuePrototype` (Task 4) |
| `core/ProtoSpace.cpp:381-405` | STW thread-list scan, branches on `POINTER_TAG_SPARSE_LIST_SMALL` vs AVL | none: `space->threads` is a `ProtoSparseList` |
| `core/ProtoSpace.cpp:502-506` | Phase 2 mutable-shard snapshot | none: the queue publishes nothing into the mutables tree |
| `core/ProtoSpace.cpp:649-679` | Phase 4 young-chain walk (calls `processReferences` on every young cell) | none, but it is a **second caller** of `processReferences` in the same cycle — the reason D2 rejects a destructive read (Task 2) |
| `core/ProtoSpace.cpp:711-783` | Phase 4 mark loop; `pushReportedReference` aborts on a null or tagged child in debug/instrumented builds | none: every new `processReferences` loads each field once and reports only non-null, untagged cells (Task 2) |
| `core/ProtoSpace.cpp:199-276` | Phase 5b `releaseFinalizedMutableEntries` | none: the queue has no `mutable_ref` |
| `core/ProtoObject.cpp:560-620` | `getPrototype` tag switch | add `case POINTER_TAG_MPSC_QUEUE` (Task 4) |
| `core/ProtoObject.cpp:1638-1651` | `isCellPointer` / `asCellPointer` (everything but tag 1 is a cell) | none: tag 28 is a cell pointer already, which is what makes a queue traceable when it is stored as an attribute |
| `core/ProtoObject.cpp:1504-1527`, `:1733-1747` | `getHash` (virtual `Cell::getHash`, the address) and `compare` (identity fallback) | none: the queue's identity hash is `Cell::getHash`, asserted in Task 4 |
| `core/ProtoObject.cpp:1907-1931` | `isMap`/`asMap`, including the `__data__` (`literalData`) delegation for tag-0 wrappers | mirror it for `isMPSCQueue`/`asMPSCQueue` (Task 4) |
| `core/ProtoContext.cpp:410-447` | `allocCell` STW poll, skipped while `criticalSectionDepth > 0` | none, but it is the mechanism the publish window relies on (Task 3) |
| `core/ProtoContext.cpp:312-343` | `safepoint`, including the young-generation threshold submission | none, but no publish window may contain a call to it (Task 3) |
| `CMakeLists.txt:32-58` | explicit library source list (no glob) | add `core/ProtoMPSCQueue.cpp` (Task 2) |
| `test/CMakeLists.txt:19` | `file(GLOB TEST_SOURCES "*.cpp")` | none: new test files are picked up automatically after a re-configure |

No embedder source switches on a protoCore pointer tag (a grep of protoPython, protoJS, protoST, protoClojure and protoScala for `POINTER_TAG_` found nothing outside protoCore).

---

### Task 1: Baseline — branch, scratch directory, before-numbers

**Files:**
- Output (not committed): `/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/`

**Interfaces:**
- Consumes: nothing.
- Produces: a clean `feature/pmq-p2` branch and the baseline files `protocore-tests-before.txt`, `tsan-baseline.txt`.

- [ ] **Step 1: Confirm the tree is clean and create the branch**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore status --short
git -C /home/gamarino/Documentos/proyectos/protoCore switch -c feature/pmq-p2
mkdir -p /home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
```
Expected: `git status --short` lists only files another agent's 2.0.0 merge left behind (`CHANGELOG.md`, `CMakeLists.txt`, `README.md`, `docs/INSTALLATION.md`, `test/MapParentChainTests.cpp`). **If anything else is modified, stop and ask** — protoCore is being merged to 2.0.0 by another agent and this plan must not collide with it. If those five are still dirty, stop and ask whether to branch from the merge commit instead.

- [ ] **Step 2: Build the current library and record the test baseline**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_release --output-on-failure 2>&1 | tail -25 \
  | tee /home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/protocore-tests-before.txt
```
Expected: a `NN% tests passed` summary line. Any test failing here is pre-existing and is not attributed to this work; record the names.

- [ ] **Step 3: Record the ThreadSanitizer baseline**

protoCore has pre-existing benign races (a cell's constructor writes reference fields after `Cell(context)` has published it to the young chain). Task 8 requires "no *new* races on the queue paths", which needs a baseline.

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
cmake -S $P -B $P/build_tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build $P/build_tsan -j4
$P/build_tsan/test/proto_tests --gtest_filter='Swarm*:GCStress*:ConcurrentMark*' 2>&1 | tee $S/tsan-baseline.txt
grep -c "WARNING: ThreadSanitizer" $S/tsan-baseline.txt
```
Expected: a count (possibly non-zero). Record it and the distinct stack tops; Task 8 compares against this list.

- [ ] **Step 4: Commit nothing yet** — this task creates no repository change. Verify with `git -C /home/gamarino/Documentos/proyectos/protoCore status --short` that only the pre-existing five files are listed.

---

### Task 2: The three cells — tag 28, CellTypes and `processReferences`

**Files:**
- Modify: `headers/proto_internal.h`
- Create: `core/ProtoMPSCQueue.cpp` (cell half only)
- Modify: `CMakeLists.txt`
- Test: `test/ProtoMPSCQueueCellTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class ProtoMPSCQueueImplementation final : public Cell` with `mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> head; mutable std::atomic<const ProtoMPSCQueueRetainImplementation*> retained; mutable std::atomic<uint64_t> retainedEpoch;`
  - `class ProtoMPSCQueueNodeImplementation final : public Cell` with `const ProtoObject* const item; mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> next;`
  - `class ProtoMPSCQueueRetainImplementation final : public Cell` with `mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> chain; mutable std::atomic<const ProtoMPSCQueueRetainImplementation*> next;`
  - `#define POINTER_TAG_MPSC_QUEUE 28`
  - `CellType::MPSCQueue`, `CellType::MPSCQueueNode`, `CellType::MPSCQueueRetain`

- [ ] **Step 1: Write the correctness proof into the source before the code**

Create `core/ProtoMPSCQueue.cpp` starting with this header comment. Everything below in this plan implements exactly what it states; if an implementation step contradicts it, the implementation is wrong.

```cpp
/*
 * ProtoMPSCQueue.cpp
 *
 * A mutable, lock-free, multi-producer / single-consumer FIFO of
 * `ProtoObject*` items whose contents are traced by the collector.
 * Specification: protoScala/docs/platform/PMQ-SPEC.md.
 *
 * ---------------------------------------------------------------------
 * Why this is safe under protoCore's concurrent mark, with no barrier
 * and no stop-the-world work
 * ---------------------------------------------------------------------
 *
 * protoCore stops the world only to take thread stack roots, the
 * mutables-tree snapshot and the roots of global structures; the mark
 * phase then runs concurrently with the mutators (core/ProtoSpace.cpp,
 * the world resumes before the mark loop).  That is sound for every
 * other type because everything reachable from those roots is immutable:
 * the marker walks a frozen graph.
 *
 * This queue is mutable, so it has to earn the same property.  It does
 * not do it by being captured under the pause; it does it by making a
 * lazy read of its two mutable words a SUPERSET of what the pause saw.
 *
 * Definitions.  Let S be the instant the world resumed for the running
 * cycle, and H_S the node chain hanging from `head` at S.  Nodes are
 * immutable once published: `item` is written in the constructor and
 * `next` only before the publishing CAS succeeds.
 *
 *   push     allocates a node, sets node->next to the current head and
 *            CASes head from that value to the node.  It only ever
 *            PREPENDS, and it publishes a fully linked chain: at every
 *            instant, walking from `head` reaches every node in the
 *            queue.  (This is why a Vyukov exchange-based MPSC queue
 *            cannot be used: it leaves the chain briefly unlinked, and
 *            while a consumer may spin for the missing link, the
 *            collector may not.)
 *
 *   takeAll  publishes a retain cell holding the chain onto `retained`
 *            BEFORE detaching the chain from `head`, and releases the
 *            previous content of `retained` only when the GC cycle
 *            counter has moved on.
 *
 * Claim.  If the marker processes this cell at time T > S, loading
 * `head` at T1 and `retained` at T2 > T1, then every node of H_S is
 * reported.
 *
 * Proof.  Take n in H_S.  Either n is still in the chain at `head` at
 * T1 — reported — or some takeAll removed it before T1.  Consider the
 * first takeAll that removed n.  It loaded h from `head` at a time
 * after S, and nodes are only prepended, so chain(h) contains every
 * node of H_S not already removed; by induction on the sequence of
 * takeAlls, n is in chain(h).  That takeAll published a retain cell
 * carrying h onto `retained` before the detach, and `retained` only
 * grows during a cycle (its release is gated on the cycle counter
 * changing, and the counter is bumped under the pause at the START of
 * a cycle, core/ProtoSpace.cpp).  So n is reachable from `retained` at
 * every instant after its removal, in particular at T2.  QED.
 *
 * The two orderings are therefore load-bearing, not stylistic:
 *   * processReferences MUST load `head` before `retained`;
 *   * takeAll MUST publish the retain cell before it detaches.
 * Swap either and an item pushed before the pause and consumed during
 * the mark is freed under a live ProtoList.
 *
 * Nodes that a takeAll drops without retaining (pushed by another
 * producer between this consumer's `head` load and its detach) were
 * allocated after S, so they are young cells of the pushing context and
 * are not candidates of the running cycle: sweep cannot see them.  The
 * publish window is a ProtoContext::CriticalSection containing no
 * allocation and no safepoint, so no pause can complete inside it and
 * that window cannot span a cycle boundary.
 *
 * The same guard proves ABA impossible on the push CAS: reusing the
 * address of the node `head` was loaded from would require a sweep to
 * complete between the load and the CAS, a sweep requires a pause, and
 * a pause requires this thread to park — which it does only in
 * allocCell / safepoint, neither of which is reached inside the window.
 * NEVER add an allocation or a protoCore call inside a publish window.
 *
 * Release of `retained` is done by the consumer, at its first takeAll
 * in a new GC cycle.  Observing counter C proves the pause of cycle C
 * happened, which proves the mark and sweep of cycle C-1 finished (one
 * collector thread, STW then mark then sweep in sequence), which proves
 * everything being released was already traced.  It is NOT a write
 * barrier: no work proportional to the references involved, nothing the
 * collector reads, and omitting it could only retain memory — never
 * lose an object.  It is also not done by processReferences: the young-
 * chain walk calls processReferences too (core/ProtoSpace.cpp Phase 4),
 * so a destructive read there would be a second, silent consumer.
 */
```

- [ ] **Step 2: Write the cell tests (they must fail to compile first — that is the red state)**

`test/ProtoMPSCQueueCellTests.cpp`:

```cpp
// ProtoMPSCQueueCellTests.cpp — the three cells of ProtoMPSCQueue:
// tag discipline, CellType, 64-byte budget, and what each one reports
// to the collector.  PMQ-SPEC §3, §4.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <algorithm>
#include <vector>

using namespace proto;

namespace {

// Collects every cell a processReferences call reports, in order.
struct Collector {
    std::vector<const Cell*> refs;
};

void collect(ProtoContext*, void* self, const Cell* ref) {
    static_cast<Collector*>(self)->refs.push_back(ref);
}

unsigned long tagOf(const void* handle) {
    ProtoObjectPointer p{};
    p.oid = reinterpret_cast<const ProtoObject*>(handle);
    return p.op.pointer_tag;
}

}  // namespace

TEST(MPSCQueueCell, HandleCarriesTag28AndCellTypeMPSCQueue) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(tagOf(q), POINTER_TAG_MPSC_QUEUE);
    EXPECT_EQ(POINTER_TAG_MPSC_QUEUE, 28u);

    const Cell* cell = toImpl<const ProtoMPSCQueueImplementation>(q);
    EXPECT_EQ(cell->getType(), CellType::MPSCQueue);
}

TEST(MPSCQueueCell, EveryCellFitsInSixtyFourBytes) {
    EXPECT_LE(sizeof(ProtoMPSCQueueImplementation), 64u);
    EXPECT_LE(sizeof(ProtoMPSCQueueNodeImplementation), 64u);
    EXPECT_LE(sizeof(ProtoMPSCQueueRetainImplementation), 64u);
    EXPECT_LE(sizeof(BigCell), 64u);
}

// An empty queue reports nothing: both mutable words are null.
TEST(MPSCQueueCell, EmptyQueueReportsNoReference) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(ctx.newMPSCQueue());

    Collector c;
    impl->processReferences(&ctx, &c, collect);
    EXPECT_TRUE(c.refs.empty());
}

// The order is load-bearing (see the proof in core/ProtoMPSCQueue.cpp):
// `head` must be reported before `retained`.
TEST(MPSCQueueCell, ReportsHeadBeforeRetained) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(q);

    q->push(&ctx, ctx.fromInteger(1));
    (void) q->takeAll(&ctx);          // fills `retained`
    q->push(&ctx, ctx.fromInteger(2));  // refills `head`

    const Cell* head = reinterpret_cast<const Cell*>(impl->head.load());
    const Cell* retained = reinterpret_cast<const Cell*>(impl->retained.load());
    ASSERT_NE(head, nullptr);
    ASSERT_NE(retained, nullptr);

    Collector c;
    impl->processReferences(&ctx, &c, collect);
    ASSERT_EQ(c.refs.size(), 2u);
    EXPECT_EQ(c.refs[0], head);
    EXPECT_EQ(c.refs[1], retained);
}

// A node reports its item (when the item carries a cell) and its next.
TEST(MPSCQueueCell, NodeReportsItemAndNext) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(q);

    const ProtoObject* a = ctx.newList()->appendLast(&ctx, ctx.fromInteger(7))->asObject(&ctx);
    q->push(&ctx, a);
    const ProtoObject* b = ctx.newList()->appendLast(&ctx, ctx.fromInteger(8))->asObject(&ctx);
    q->push(&ctx, b);

    const ProtoMPSCQueueNodeImplementation* top = impl->head.load();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->getType(), CellType::MPSCQueueNode);

    Collector c;
    top->processReferences(&ctx, &c, collect);
    ASSERT_EQ(c.refs.size(), 2u);
    EXPECT_EQ(c.refs[0], ProtoObject::asCellPointer(b));   // item (pushed last)
    EXPECT_EQ(c.refs[1], reinterpret_cast<const Cell*>(top->next.load()));
}

// An embedded item (SmallInteger, boolean, None, inline string) carries no
// cell and must not be reported: pushReportedReference aborts on a tagged
// pointer in instrumented and debug builds.
TEST(MPSCQueueCell, NodeDoesNotReportEmbeddedItems) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(q);

    q->push(&ctx, ctx.fromInteger(42));
    const ProtoMPSCQueueNodeImplementation* top = impl->head.load();
    ASSERT_NE(top, nullptr);

    Collector c;
    top->processReferences(&ctx, &c, collect);
    EXPECT_TRUE(c.refs.empty()) << "an embedded item must not be reported as a cell";
}

// Every reported reference must be non-null and untagged, or the GC's
// pushReportedReference aborts.
TEST(MPSCQueueCell, EveryReportedReferenceIsAnAlignedCell) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(q);

    for (int i = 0; i < 40; ++i)
        q->push(&ctx, ctx.newList()->appendLast(&ctx, ctx.fromInteger(i))->asObject(&ctx));
    (void) q->takeAll(&ctx);
    for (int i = 0; i < 40; ++i)
        q->push(&ctx, ctx.newList()->appendLast(&ctx, ctx.fromInteger(i))->asObject(&ctx));

    std::vector<const Cell*> work;
    Collector c;
    impl->processReferences(&ctx, &c, collect);
    work = c.refs;
    size_t seen = 0;
    while (!work.empty()) {
        const Cell* cell = work.back();
        work.pop_back();
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(cell) & 0x3FUL, 0UL);
        ++seen;
        if (seen > 500) break;    // the chains are finite; guard a cycle bug
        Collector inner;
        cell->processReferences(&ctx, &inner, collect);
        work.insert(work.end(), inner.refs.begin(), inner.refs.end());
    }
    EXPECT_GT(seen, 80u);  // 80 nodes + 1 retain cell + the item lists
}

// D6: the internal cells are never exposed.  isObjectFast must reject
// them even though their implAsObject word carries tag 0.
TEST(MPSCQueueCell, InternalCellsAreNotObjectCells) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const auto* impl = toImpl<const ProtoMPSCQueueImplementation>(q);
    q->push(&ctx, ctx.fromInteger(1));

    const ProtoMPSCQueueNodeImplementation* node = impl->head.load();
    ASSERT_NE(node, nullptr);
    const ProtoObject* word = node->implAsObject(&ctx);
    EXPECT_EQ(tagOf(word), POINTER_TAG_OBJECT);
    EXPECT_FALSE(isObjectFast(word));
}
```

- [ ] **Step 3: Add the tag, the budget comment, the CellTypes and the union members**

In `headers/proto_internal.h`, after `class ProtoMapIteratorImplementation;` (`:57`):

```cpp
    class ProtoMPSCQueueImplementation;
    class ProtoMPSCQueueNodeImplementation;
    class ProtoMPSCQueueRetainImplementation;
```

In the `ProtoObjectPointer` union, after the `ProtoMap` members (`:110-112`):

```cpp
        const ProtoMPSCQueue *mpscQueue;
        const ProtoMPSCQueueImplementation *mpscQueueImplementation;
```

After `#define POINTER_TAG_MAP  27 ...`:

```cpp
#define POINTER_TAG_MPSC_QUEUE 28 // ProtoMPSCQueueImplementation — the lock-free MPSC queue handle
```

Replace the budget block's counts and append the note:

```cpp
// ---------------------------------------------------------------------
// Tagged-pointer budget (platform-wide, scarce).
//
//   Pointer tags (6 bits, 64 values):   used 0-28 (29), free 29-63 (35)
//   Embedded types (4 bits, 16 values): used 0, 2, 3, 4, 5 (5),
//                                       free 1, 6-15 (11)
//
// Rules (protoScala/docs/platform/PROTOMAP-SPEC.md §3):
//   1. A new pointer tag or embedded type requires the protoCore
//      maintainer's explicit approval.
//   2. A type takes at most ONE tag, for its public handle.  Internal cells
//      (tree nodes, inline forms, iterators never exposed as words) are told
//      apart by CellType through Cell::getType(), which is not scarce.
//   3. Language-level encodings reuse existing encodings (e.g. a
//      SmallInteger word carrying a hash) and never claim a tag.
//
// Tag 27 carries two cell types (ProtoMapImplementation and
// ProtoMapSmallImplementation); code that needs the form calls
// getType(), exactly as tag 0 does for its several cell types above.
//
// Tag 28 carries exactly one cell type (ProtoMPSCQueueImplementation).
// Its node and retain cells are internal: they are told apart by
// CellType, are never handed out as ProtoObject* words, and therefore
// take no tag (protoScala/docs/platform/PMQ-SPEC.md §4).
// ---------------------------------------------------------------------
```

In the `ExpectedTag` block, after the `ProtoMapIteratorImplementation` specialisations:

```cpp
    template<> struct ExpectedTag<const ProtoMPSCQueueImplementation> { static constexpr unsigned long value = POINTER_TAG_MPSC_QUEUE; };
    template<> struct ExpectedTag<ProtoMPSCQueueImplementation> { static constexpr unsigned long value = POINTER_TAG_MPSC_QUEUE; };

    // Internal cells of ProtoMPSCQueue (PMQ-SPEC §4, decision D6): the
    // handle is the raw cell address (tag 0, POINTER_TAG_OBJECT), never a
    // boxed ProtoObject word offered to the object model.  Same discipline
    // as ProtoMapIteratorImplementation above.
    template<> struct ExpectedTag<const ProtoMPSCQueueNodeImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
    template<> struct ExpectedTag<ProtoMPSCQueueNodeImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
    template<> struct ExpectedTag<const ProtoMPSCQueueRetainImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
    template<> struct ExpectedTag<ProtoMPSCQueueRetainImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
```

At the end of `enum class CellType`, after `MapIterator`:

```cpp
        MPSCQueue,
        MPSCQueueNode,
        MPSCQueueRetain
```

- [ ] **Step 4: Declare the three cell classes**

In `headers/proto_internal.h`, after `class ProtoMapIteratorImplementation { ... };` (`:1743`):

```cpp
    /**
     * @brief Lock-free multi-producer / single-consumer queue cell.
     *
     * Specification: protoScala/docs/platform/PMQ-SPEC.md.  The GC
     * correctness argument lives in core/ProtoMPSCQueue.cpp; read it
     * before touching any of the three fields below.
     *
     * This is the first protoCore cell with state that changes after
     * publication, which is why every such field is a std::atomic:
     *
     *   head          the LIFO chain of pushed nodes.  Producers
     *                 CAS-prepend; the chain is fully linked at every
     *                 instant, so the collector can always walk it.
     *   retained      chains that takeAll has detached but that the
     *                 running collector may still need.  A takeAll
     *                 publishes here BEFORE it detaches; a marker reads
     *                 `head` BEFORE this.  Both orderings are required
     *                 for correctness.
     *   retainedEpoch the GC cycle number `retained` belongs to.  The
     *                 single consumer releases `retained` when it sees
     *                 a different ProtoSpace::getGCCycleCount().
     *
     * The queue adds nothing to the stop-the-world pause and needs no
     * write barrier (PMQ-SPEC §3).
     */
    class ProtoMPSCQueueImplementation final : public Cell {
    public:
        mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> head;
        mutable std::atomic<const ProtoMPSCQueueRetainImplementation*> retained;
        mutable std::atomic<uint64_t> retainedEpoch;

        CellType getType() const override { return CellType::MPSCQueue; }

        explicit ProtoMPSCQueueImplementation(ProtoContext* context);

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };

    /**
     * @brief One queued item.  Internal: never exposed as a ProtoObject word.
     *
     * `item` is written in the constructor.  `next` is written only while
     * the node is still private to the pushing thread — the publishing CAS
     * on ProtoMPSCQueueImplementation::head is what makes it visible, and
     * after that CAS succeeds the field is never written again.  It is a
     * std::atomic because the collector's young-chain walk may read it
     * while a losing CAS retry rewrites it.
     */
    class ProtoMPSCQueueNodeImplementation final : public Cell {
    public:
        const ProtoObject* const item;
        mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> next;

        CellType getType() const override { return CellType::MPSCQueueNode; }

        ProtoMPSCQueueNodeImplementation(ProtoContext* context, const ProtoObject* item);

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };

    /**
     * @brief One chain detached by takeAll, kept reachable for the
     *        collector that was running when it was detached.
     *
     * Internal: never exposed as a ProtoObject word.  A FRESH cell is
     * allocated for every takeAll; the stack link must not be folded into
     * the detached chain's own head node, because that node may already
     * have been marked, and the marker never revisits a marked cell — the
     * link would then be invisible and the older chains would be lost.
     */
    class ProtoMPSCQueueRetainImplementation final : public Cell {
    public:
        mutable std::atomic<const ProtoMPSCQueueNodeImplementation*> chain;
        mutable std::atomic<const ProtoMPSCQueueRetainImplementation*> next;

        CellType getType() const override { return CellType::MPSCQueueRetain; }

        explicit ProtoMPSCQueueRetainImplementation(ProtoContext* context);

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };
```

In `BigCell`'s union, after `ProtoMapIteratorImplementation mapIteratorCell;`:

```cpp
            ProtoMPSCQueueImplementation mpscQueueCell;
            ProtoMPSCQueueNodeImplementation mpscQueueNodeCell;
            ProtoMPSCQueueRetainImplementation mpscQueueRetainCell;
```

After the `ProtoMapIteratorImplementation` size assertion:

```cpp
    static_assert(sizeof(ProtoMPSCQueueImplementation) <= 64, "ProtoMPSCQueueImplementation exceeds 64 bytes!");
    static_assert(sizeof(ProtoMPSCQueueNodeImplementation) <= 64, "ProtoMPSCQueueNodeImplementation exceeds 64 bytes!");
    static_assert(sizeof(ProtoMPSCQueueRetainImplementation) <= 64, "ProtoMPSCQueueRetainImplementation exceeds 64 bytes!");
```

- [ ] **Step 5: Implement the cells in `core/ProtoMPSCQueue.cpp`**

After the header comment from Step 1:

```cpp
#include "../headers/proto_internal.h"

#include <algorithm>
#include <vector>

namespace proto
{
    //=========================================================================
    // ProtoMPSCQueueImplementation
    //=========================================================================
    ProtoMPSCQueueImplementation::ProtoMPSCQueueImplementation(ProtoContext* context)
        : Cell(context), head(nullptr), retained(nullptr), retainedEpoch(0) {}

    const ProtoObject* ProtoMPSCQueueImplementation::implAsObject(ProtoContext*) const {
        ProtoObjectPointer p{};
        p.mpscQueueImplementation = this;
        p.op.pointer_tag = POINTER_TAG_MPSC_QUEUE;
        return p.oid;
    }

    void ProtoMPSCQueueImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        // ORDER IS LOAD-BEARING.  `head` first, `retained` second.  A takeAll
        // publishes into `retained` before it removes a chain from `head`, so
        // reading in this order makes the union a superset of the chain that
        // existed when the world resumed.  Reading them the other way round
        // loses every item consumed between the two loads.  See the proof at
        // the top of this file.
        if (const ProtoMPSCQueueNodeImplementation* h = head.load(std::memory_order_acquire))
            method(context, self, h);
        if (const ProtoMPSCQueueRetainImplementation* r = retained.load(std::memory_order_acquire))
            method(context, self, r);
    }

    //=========================================================================
    // ProtoMPSCQueueNodeImplementation
    //=========================================================================
    ProtoMPSCQueueNodeImplementation::ProtoMPSCQueueNodeImplementation(
        ProtoContext* context, const ProtoObject* i)
        : Cell(context), item(i), next(nullptr) {}

    const ProtoObject* ProtoMPSCQueueNodeImplementation::implAsObject(ProtoContext*) const {
        // Internal cell (PMQ-SPEC §4, decision D6): the raw, untagged
        // address.  It is never handed to newChild / addParent /
        // getAttribute, so it can never reach the attribute chain.
        return reinterpret_cast<const ProtoObject*>(this);
    }

    void ProtoMPSCQueueNodeImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        // An item whose only reference is this node must survive.  Embedded
        // items (SmallInteger, boolean, char, None, inline string) carry no
        // cell and are skipped — reporting one would abort the collector in
        // instrumented builds (pushReportedReference).
        if (const Cell* c = ProtoObject::asCellPointer(item)) method(context, self, c);
        // The chain is reported link by link rather than walked here: the
        // mark loop's work list is iterative, so a million-node chain costs
        // a million pushes and no stack depth.
        if (const ProtoMPSCQueueNodeImplementation* n = next.load(std::memory_order_acquire))
            method(context, self, n);
    }

    //=========================================================================
    // ProtoMPSCQueueRetainImplementation
    //=========================================================================
    ProtoMPSCQueueRetainImplementation::ProtoMPSCQueueRetainImplementation(ProtoContext* context)
        : Cell(context), chain(nullptr), next(nullptr) {}

    const ProtoObject* ProtoMPSCQueueRetainImplementation::implAsObject(ProtoContext*) const {
        return reinterpret_cast<const ProtoObject*>(this);
    }

    void ProtoMPSCQueueRetainImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        if (const ProtoMPSCQueueNodeImplementation* c = chain.load(std::memory_order_acquire))
            method(context, self, c);
        if (const ProtoMPSCQueueRetainImplementation* n = next.load(std::memory_order_acquire))
            method(context, self, n);
    }
}  // namespace proto
```

- [ ] **Step 6: Register the source file**

In `CMakeLists.txt`, after `core/ProtoMap.cpp`:

```cmake
    core/ProtoMPSCQueue.cpp
```

- [ ] **Step 7: Build. The cell tests still fail to link — that is expected**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4 --target protoCore
```
Expected: `libprotoCore.so` builds with no warning about a missing `CellType` enumerator in `cellTypeName` — **if the compiler warns, Task 4's `cellTypeName` cases are needed now**; add them in this step rather than leaving a warning. The test target does not build yet (`newMPSCQueue`, `push`, `takeAll` do not exist): that is the red state Task 3 turns green.

- [ ] **Step 8: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/proto_internal.h core/ProtoMPSCQueue.cpp CMakeLists.txt test/ProtoMPSCQueueCellTests.cpp
git commit -m "feat(mpsc): ProtoMPSCQueue cells, tag 28 and the GC correctness proof

Three cell classes (queue, node, retain), one pointer tag for the public
handle and three CellTypes for the internals.  processReferences loads
head before retained; the ordering argument that makes a lazy read of a
mutable word safe under concurrent marking is written out in
core/ProtoMPSCQueue.cpp.

Spec: protoScala/docs/platform/PMQ-SPEC.md §3, §4.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `push`, `takeAll`, `isEmpty`

**Files:**
- Modify: `headers/protoCore.h`
- Modify: `core/ProtoMPSCQueue.cpp`
- Modify: `core/ProtoContext.cpp`
- Test: `test/ProtoMPSCQueueTests.cpp`

**Interfaces:**
- Consumes: the cells of Task 2.
- Produces:
  - `void ProtoMPSCQueue::push(ProtoContext* context, const ProtoObject* item) const;`
  - `const ProtoList* ProtoMPSCQueue::takeAll(ProtoContext* context) const;`
  - `bool ProtoMPSCQueue::isEmpty(ProtoContext* context) const;`
  - `const ProtoObject* ProtoMPSCQueue::asObject(ProtoContext* context) const;`
  - `unsigned long ProtoMPSCQueue::getHash(ProtoContext* context) const;`
  - `const ProtoMPSCQueue* ProtoContext::newMPSCQueue();`

- [ ] **Step 1: Write the behavioural tests**

`test/ProtoMPSCQueueTests.cpp`:

```cpp
// ProtoMPSCQueueTests.cpp — single-threaded semantics of ProtoMPSCQueue.
// PMQ-SPEC §2 and §2.1.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <vector>

using namespace proto;

namespace {

const ProtoObject* boxed(ProtoContext* c, long v) {
    // A real cell, so the GC has something to trace (an item that is only a
    // SmallInteger would be embedded and prove nothing about tracing).
    return c->newList()->appendLast(c, c->fromInteger(v))->asObject(c);
}

long unboxed(ProtoContext* c, const ProtoObject* o) {
    return o->asList(c)->getAt(c, 0)->asLong(c);
}

}  // namespace

TEST(MPSCQueue, NewQueueIsEmptyAndTakeAllReturnsAnEmptyList) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    EXPECT_TRUE(q->isEmpty(&ctx));

    const ProtoList* got = q->takeAll(&ctx);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->getSize(&ctx), 0u);
    EXPECT_TRUE(q->isEmpty(&ctx));
}

TEST(MPSCQueue, OneProducerSeesFIFOOrder) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();

    constexpr long kN = 1000;
    for (long i = 0; i < kN; ++i) q->push(&ctx, boxed(&ctx, i));
    EXPECT_FALSE(q->isEmpty(&ctx));

    const ProtoList* got = q->takeAll(&ctx);
    ASSERT_EQ(got->getSize(&ctx), static_cast<unsigned long>(kN));
    for (long i = 0; i < kN; ++i)
        EXPECT_EQ(unboxed(&ctx, got->getAt(&ctx, static_cast<int>(i))), i) << "at " << i;

    EXPECT_TRUE(q->isEmpty(&ctx));
    EXPECT_EQ(q->takeAll(&ctx)->getSize(&ctx), 0u);
}

TEST(MPSCQueue, ItemsPushedAfterATakeAllBelongToTheNextBatch) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();

    for (long i = 0; i < 5; ++i) q->push(&ctx, boxed(&ctx, i));
    const ProtoList* first = q->takeAll(&ctx);
    for (long i = 5; i < 9; ++i) q->push(&ctx, boxed(&ctx, i));
    const ProtoList* second = q->takeAll(&ctx);

    ASSERT_EQ(first->getSize(&ctx), 5u);
    ASSERT_EQ(second->getSize(&ctx), 4u);
    EXPECT_EQ(unboxed(&ctx, first->getAt(&ctx, 0)), 0);
    EXPECT_EQ(unboxed(&ctx, first->getAt(&ctx, 4)), 4);
    EXPECT_EQ(unboxed(&ctx, second->getAt(&ctx, 0)), 5);
    EXPECT_EQ(unboxed(&ctx, second->getAt(&ctx, 3)), 8);
}

// PMQ-SPEC §2: items are never copied — the queue stores the pointer.
TEST(MPSCQueue, ItemsAreReturnedByIdentity) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();

    std::vector<const ProtoObject*> pushed;
    for (long i = 0; i < 20; ++i) {
        const ProtoObject* o = boxed(&ctx, i);
        pushed.push_back(o);
        q->push(&ctx, o);
    }
    const ProtoList* got = q->takeAll(&ctx);
    ASSERT_EQ(got->getSize(&ctx), pushed.size());
    for (size_t i = 0; i < pushed.size(); ++i)
        EXPECT_EQ(got->getAt(&ctx, static_cast<int>(i)), pushed[i]) << "at " << i;
}

// Embedded values are valid items and survive the round trip untouched.
TEST(MPSCQueue, EmbeddedItemsRoundTrip) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();

    q->push(&ctx, ctx.fromInteger(7));
    q->push(&ctx, ctx.fromBoolean(true));
    q->push(&ctx, PROTO_NONE);

    const ProtoList* got = q->takeAll(&ctx);
    ASSERT_EQ(got->getSize(&ctx), 3u);
    EXPECT_EQ(got->getAt(&ctx, 0)->asLong(&ctx), 7);
    EXPECT_EQ(got->getAt(&ctx, 1), ctx.fromBoolean(true));
    EXPECT_EQ(got->getAt(&ctx, 2), PROTO_NONE);
}

// A batch larger than the inline-list form, so the AVL builder runs.
TEST(MPSCQueue, LargeBatchKeepsOrder) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();

    constexpr long kN = 50000;
    for (long i = 0; i < kN; ++i) q->push(&ctx, ctx.fromInteger(i));
    const ProtoList* got = q->takeAll(&ctx);
    ASSERT_EQ(got->getSize(&ctx), static_cast<unsigned long>(kN));
    for (long i = 0; i < kN; i += 997)
        EXPECT_EQ(got->getAt(&ctx, static_cast<int>(i))->asLong(&ctx), i) << "at " << i;
    EXPECT_EQ(got->getAt(&ctx, kN - 1)->asLong(&ctx), kN - 1);
}
```

- [ ] **Step 2: Declare the public API**

In `headers/protoCore.h`, in the forward-declaration block near `:31`:

```cpp
    class ProtoMPSCQueue;
```

After the `class ProtoMap { ... };` declaration:

```cpp
    /**
     * @class ProtoMPSCQueue
     * @brief Mutable, lock-free, multi-producer / single-consumer FIFO of
     *        items the garbage collector traces.
     *
     * Specification: protoScala/docs/platform/PMQ-SPEC.md.  One queue per
     * actor and priority band is the intended use (protoScala DESIGN §8,
     * protoClojure, protoST).
     *
     * `push` may be called concurrently from any number of threads; it is
     * lock-free, allocates exactly one cell and does no work proportional
     * to the queue's length.  `takeAll` removes every item pushed so far
     * and returns them in push order as an immutable ProtoList; **only one
     * consumer may call it at a time**, which the caller guarantees (an
     * actor runtime already does, through its claimed flag).  Violating
     * that is a caller bug, not undefined memory behaviour: the
     * implementation stays memory-safe and only the partition of items
     * between the racing consumers is unspecified.
     *
     * Items are never copied — the queue stores the pointer.  An item
     * pushed before a takeAll begins is returned by that call or a later
     * one: never lost, never duplicated.
     *
     * The queue itself must stay reachable for the duration of a call: the
     * items of a batch being built into a list are reachable through it,
     * and through nothing else.  Hold it in an attribute, a root set or a
     * live context, exactly as for every other protoCore handle.
     */
    class ProtoMPSCQueue
    {
    public:
        /** Any thread.  Lock-free, O(1), one cell. */
        void push(ProtoContext* context, const ProtoObject* item) const;

        /** Single consumer.  Every queued item in FIFO order; an empty list
         *  when nothing is queued. */
        const ProtoList* takeAll(ProtoContext* context) const;

        /** Any thread; a snapshot that may be stale by the time it is used. */
        bool isEmpty(ProtoContext* context) const;

        const ProtoObject* asObject(ProtoContext* context) const;

        /** Identity hash: the queue is mutable, so its contents cannot
         *  contribute to it. */
        unsigned long getHash(ProtoContext* context) const;
    };
```

In `ProtoContext`, after `const ProtoMap* newMap();`:

```cpp
        /** Empty lock-free MPSC queue (PMQ-SPEC §2.1). */
        const ProtoMPSCQueue* newMPSCQueue();
```

- [ ] **Step 3: Implement the trampolines**

Append to `core/ProtoMPSCQueue.cpp`, inside `namespace proto`:

```cpp
    namespace {
        using QueueCell = ProtoMPSCQueueImplementation;
        using NodeCell  = ProtoMPSCQueueNodeImplementation;
        using RetainCell = ProtoMPSCQueueRetainImplementation;

        inline const QueueCell* implOf(const ProtoMPSCQueue* q) {
            return toImpl<const QueueCell>(q);
        }
    }

    //=========================================================================
    // Public trampolines
    //=========================================================================
    void ProtoMPSCQueue::push(ProtoContext* context, const ProtoObject* item) const
    {
        const QueueCell* q = implOf(this);

        // The cell is allocated INSIDE the critical section so the heap
        // ceiling is enforced at the outermost boundary (CriticalSection's
        // constructor runs heapLimitCheckpoint at depth 0) and so that no
        // stop-the-world can complete between the allocation and the
        // publishing CAS.  That is what makes ABA impossible on `head`:
        // reusing the address this loop loaded would require a sweep, a
        // sweep requires a pause, and a pause requires this thread to park.
        ProtoContext::CriticalSection cs(context);

        NodeCell* node = new(context) NodeCell(context, item);

        // No allocation and no protoCore call from here to the CAS.
        const NodeCell* h = q->head.load(std::memory_order_relaxed);
        do {
            node->next.store(h, std::memory_order_relaxed);
        } while (!q->head.compare_exchange_weak(
                     h, node, std::memory_order_release, std::memory_order_relaxed));
    }

    bool ProtoMPSCQueue::isEmpty(ProtoContext*) const
    {
        // `retained` is deliberately ignored: its chains have already been
        // consumed and are kept only for the collector.
        return implOf(this)->head.load(std::memory_order_acquire) == nullptr;
    }

    const ProtoList* ProtoMPSCQueue::takeAll(ProtoContext* context) const
    {
        const QueueCell* q = implOf(this);

        // Fast path: nothing queued, nothing allocated but the empty list.
        if (q->head.load(std::memory_order_acquire) == nullptr)
            return context->newList();

        const NodeCell* chain = nullptr;
        {
            // The retain cell is allocated before the window: allocating
            // inside it would be an allocation in a region that must contain
            // none (see push).
            RetainCell* retain = new(context) RetainCell(context);

            ProtoContext::CriticalSection cs(context);

            // --- publish window: no allocation, no safepoint, no protoCore
            // --- call.  No stop-the-world can complete inside it, so the
            // --- cycle number read here is still current at the publish.
            const uint64_t cycle = context->space->getGCCycleCount();
            if (q->retainedEpoch.load(std::memory_order_relaxed) != cycle) {
                // Everything currently retained was detached in an earlier
                // cycle, so the mark that had to see it has finished.
                q->retained.exchange(nullptr, std::memory_order_acq_rel);
                q->retainedEpoch.store(cycle, std::memory_order_relaxed);
            }

            const NodeCell* h = q->head.load(std::memory_order_acquire);
            if (h == nullptr) {
                // A racing consumer emptied the queue between the fast-path
                // probe and here (a caller bug, PMQ-SPEC §2 — but it must
                // stay memory-safe).  The retain cell becomes garbage.
                return context->newList();
            }

            retain->chain.store(h, std::memory_order_relaxed);
            const RetainCell* rh = q->retained.load(std::memory_order_relaxed);
            do {
                retain->next.store(rh, std::memory_order_relaxed);
            } while (!q->retained.compare_exchange_weak(
                         rh, retain, std::memory_order_release, std::memory_order_relaxed));

            // Only now may the chain leave `head`.  Producers may have
            // prepended since the load above; those nodes are taken too and
            // are not covered by `retain` — they were allocated inside this
            // window, hence after the pause, hence young cells of the pushing
            // context that this cycle's sweep cannot see.
            chain = q->head.exchange(nullptr, std::memory_order_acq_rel);
            // --- end of the publish window ---
        }

        // Outside the critical section on purpose: this is O(batch) and a
        // critical section here would delay a stop-the-world for the length
        // of the batch.  The items stay reachable through the retained chain
        // while the list is built.
        std::vector<const ProtoObject*> items;
        for (const NodeCell* n = chain; n; n = n->next.load(std::memory_order_acquire))
            items.push_back(n->item);
        std::reverse(items.begin(), items.end());   // the chain is LIFO; FIFO out

        return context->newList(static_cast<unsigned>(items.size()), items.data());
    }

    const ProtoObject* ProtoMPSCQueue::asObject(ProtoContext*) const {
        return reinterpret_cast<const ProtoObject*>(this);   // already tagged
    }

    unsigned long ProtoMPSCQueue::getHash(ProtoContext* context) const {
        // Identity: the contents change under the caller's feet, so they
        // cannot contribute.  Cell::getHash is the cell address.
        return implOf(this)->Cell::getHash(context);
    }
```

- [ ] **Step 4: The factory**

In `core/ProtoContext.cpp`, after `newMap()`:

```cpp
    const ProtoMPSCQueue* ProtoContext::newMPSCQueue()
    {
        return reinterpret_cast<const ProtoMPSCQueue*>(
            (new(this) ProtoMPSCQueueImplementation(this))->implAsObject(this));
    }
```

- [ ] **Step 5: Build and run the behavioural and cell tests**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
  --gtest_filter='MPSCQueue*:MPSCQueueCell*'
```
Expected: `[  PASSED  ] 13 tests.` (6 behavioural + 7 cell), no failures.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/protoCore.h core/ProtoMPSCQueue.cpp core/ProtoContext.cpp test/ProtoMPSCQueueTests.cpp
git commit -m "feat(mpsc): push, takeAll, isEmpty and the ProtoContext factory

push allocates one node and CAS-prepends inside a critical section, so no
pause can complete between the allocation and the publish (which is what
rules out ABA).  takeAll publishes its retain cell before detaching, and
builds the FIFO list outside the critical section so a large batch never
delays a pause.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Object-model integration — predicates, prototype, diagnostics, GC root

**Files:**
- Modify: `headers/protoCore.h`, `core/ProtoObject.cpp`, `core/ProtoSpace.cpp`
- Test: `test/ProtoMPSCQueueTests.cpp` (append)

**Interfaces:**
- Produces: `bool ProtoObject::isMPSCQueue(ProtoContext*) const;`, `const ProtoMPSCQueue* ProtoObject::asMPSCQueue(ProtoContext*) const;`, `ProtoObject* ProtoSpace::mpscQueuePrototype;`

- [ ] **Step 1: Append the integration tests**

```cpp
TEST(MPSCQueue, ObjectModelIntegration) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const ProtoObject* o = q->asObject(&ctx);

    EXPECT_TRUE(o->isMPSCQueue(&ctx));
    EXPECT_EQ(o->asMPSCQueue(&ctx), q);
    EXPECT_EQ(o->getPrototype(&ctx), space.mpscQueuePrototype);

    // It is not any other collection.
    EXPECT_FALSE(o->isMap(&ctx));
    EXPECT_EQ(o->asMap(&ctx), nullptr);
    EXPECT_EQ(o->asList(&ctx), nullptr);
    EXPECT_EQ(o->asSparseList(&ctx), nullptr);

    // Nothing else answers true to isMPSCQueue.
    EXPECT_FALSE(ctx.newMap()->asObject(&ctx)->isMPSCQueue(&ctx));
    EXPECT_FALSE(ctx.newList()->asObject(&ctx)->isMPSCQueue(&ctx));
    EXPECT_FALSE(ctx.fromInteger(3)->isMPSCQueue(&ctx));

    // Identity hash: stable across mutation, distinct per queue.
    const unsigned long h = q->getHash(&ctx);
    q->push(&ctx, ctx.fromInteger(1));
    EXPECT_EQ(q->getHash(&ctx), h);
    EXPECT_NE(ctx.newMPSCQueue()->getHash(&ctx), h);
}

// A queue stored as an attribute is an ordinary traced reference: this is
// how an actor holds its three bands.
TEST(MPSCQueue, SurvivesAsAnAttributeOfAMutableObject) {
    ProtoSpace space;
    ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const ProtoObject* actor = ctx.newObject(true);
    const ProtoString* key = ProtoString::createSymbol(&ctx, "__mailbox__");
    const ProtoMPSCQueue* q = ctx.newMPSCQueue();
    const ProtoObject* updated = actor->setAttribute(&ctx, key, q->asObject(&ctx));
    ASSERT_NE(updated, nullptr);

    const ProtoObject* back = actor->getAttribute(&ctx, key, false);
    ASSERT_NE(back, nullptr);
    ASSERT_TRUE(back->isMPSCQueue(&ctx));
    back->asMPSCQueue(&ctx)->push(&ctx, ctx.fromInteger(5));
    EXPECT_EQ(q->takeAll(&ctx)->getSize(&ctx), 1u);
}
```

- [ ] **Step 2: Predicates and accessors**

In `headers/protoCore.h`, next to `isMap` / `asMap`:

```cpp
        bool isMPSCQueue(ProtoContext* context) const;
```
```cpp
        const ProtoMPSCQueue* asMPSCQueue(ProtoContext* context) const;
```

In `core/ProtoObject.cpp`, after `asMap`, mirroring its `__data__` delegation so a language-level wrapper object works the same way:

```cpp
    bool ProtoObject::isMPSCQueue(ProtoContext* context) const {
        if (!this) return false;
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag == POINTER_TAG_MPSC_QUEUE) return true;
        if (pa.op.pointer_tag == POINTER_TAG_OBJECT) {
            const proto::ProtoString* dataName = context->space->literalData;
            const proto::ProtoObject* data = this->getAttribute(context, dataName, false);
            if (data && data != this) return data->isMPSCQueue(context);
        }
        return false;
    }

    const ProtoMPSCQueue* ProtoObject::asMPSCQueue(ProtoContext* context) const {
        if (!this) return nullptr;
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag == POINTER_TAG_MPSC_QUEUE)
            return reinterpret_cast<const ProtoMPSCQueue*>(this);
        if (pa.op.pointer_tag == POINTER_TAG_OBJECT) {
            const proto::ProtoString* dataName = context->space->literalData;
            const proto::ProtoObject* data = this->getAttribute(context, dataName, false);
            if (data && data != this) return data->asMPSCQueue(context);
        }
        return nullptr;
    }
```

In `getPrototype`'s tag switch, after `case POINTER_TAG_MAP:`:

```cpp
        case POINTER_TAG_MPSC_QUEUE: return context->space->mpscQueuePrototype;
```

- [ ] **Step 3: Prototype, diagnostics and the one new global root**

In `headers/protoCore.h`, next to `mapPrototype`:

```cpp
        ProtoObject* mpscQueuePrototype{};
```

In `core/ProtoSpace.cpp`, in `cellTypeName`, after `case CellType::MapIterator:`:

```cpp
                case CellType::MPSCQueue: return "MPSCQueue";
                case CellType::MPSCQueueNode: return "MPSCQueueNode";
                case CellType::MPSCQueueRetain: return "MPSCQueueRetain";
```

After `this->mapPrototype = ...` (`:1193`):

```cpp
        this->mpscQueuePrototype = const_cast<ProtoObject*>(this->rootContext->newObject(false));
```

In the STW global-root block, after `addRootObj(space->mapPrototype);`:

```cpp
                // O(1), a global-structure root: the only addition this type
                // makes to the stop-the-world window (PMQ-SPEC §3.1).
                addRootObj(space->mpscQueuePrototype);
```

Also initialise it in the `ProtoSpace` constructor's member-initialiser list next to `sparseListPrototype(nullptr),` if the field is not brace-initialised in the header (it is, via `{}` — verify and do not duplicate).

- [ ] **Step 4: Build and run**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
  --gtest_filter='MPSCQueue*'
```
Expected: `[  PASSED  ] 15 tests.`

- [ ] **Step 5: Run the whole suite — nothing else may move**

```bash
ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_release --output-on-failure 2>&1 | tail -25
```
Expected: the same pass/fail set as `protocore-tests-before.txt` plus the new cases. Any newly failing test stops the task.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/protoCore.h core/ProtoObject.cpp core/ProtoSpace.cpp test/ProtoMPSCQueueTests.cpp
git commit -m "feat(mpsc): object-model integration for ProtoMPSCQueue

isMPSCQueue / asMPSCQueue with the same __data__ delegation as isMap,
a dedicated prototype, the three cellTypeName entries and one O(1)
global root under stop-the-world.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Concurrency — 8 producers × 1M, no loss, no duplication, per-producer order

**Files:**
- Test: `test/ProtoMPSCQueueConcurrencyTests.cpp` (create)

**Interfaces:**
- Consumes: the full public API.
- Produces: the PMQ-SPEC §5 loss/duplication test.

- [ ] **Step 1: Write the test**

Items are `SmallInteger`s encoding `producer * kStride + sequence`, so the checker can verify both the multiset and each producer's order with no allocation per item beyond the node itself. Producer threads are created with `ProtoSpace::newThread` semantics via a `ProtoThread`-less `ProtoContext` chained to the space — **use the pattern of `test/SwarmTests.cpp`**; a raw `std::thread` with a context whose thread is null never joins the GC quorum and would stall the collector.

`test/ProtoMPSCQueueConcurrencyTests.cpp`:

```cpp
// ProtoMPSCQueueConcurrencyTests.cpp — PMQ-SPEC §5: no loss, no
// duplication, per-producer order, with 8 producers against one consumer.
//
// The heap is bounded with setHeapLimits so collections actually run during
// the stress (8 x 1M nodes would otherwise be ~512 MB of live cells, and
// this machine is OOM-sensitive).  That makes this test double as the
// "pushes during concurrent marking" stress of §5.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace proto;

namespace {

constexpr long kProducers = 8;
constexpr long kStride    = 10000000;   // room for any per-producer count

long pushesPerProducer() {
    if (const char* env = std::getenv("PMQ_STRESS_PUSHES")) {
        const long v = std::atol(env);
        if (v > 0) return v;
    }
    return 1000000;
}

}  // namespace

TEST(MPSCQueueConcurrency, EightProducersOneConsumerLoseNothingAndDuplicateNothing) {
    const long kPushes = pushesPerProducer();

    ProtoSpace space;
    ProtoContext consumerCtx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    // Keep the queue reachable for the whole run through a root set: the
    // consumer's list batches reference the items, but the queue itself must
    // stay rooted for the retain chain to be traced.
    ProtoRootSet* rs = space.createRootSet("mpsc-stress");
    ASSERT_NE(rs, nullptr);
    const ProtoMPSCQueue* q = consumerCtx.newMPSCQueue();
    const ProtoRootSet::Handle pinned = rs->addRoot(q->asObject(&consumerCtx));
    ASSERT_NE(pinned, ProtoRootSet::kNullHandle);

    // Bound the heap so the collector runs during the stress.
    space.setHeapLimits(/*soft=*/0, /*hard=*/space.heapSize + 400000);

    std::atomic<bool> producersDone{false};
    std::vector<std::thread> producers;
    for (long p = 0; p < kProducers; ++p) {
        producers.emplace_back([&space, q, p, kPushes] {
            ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
            for (long i = 0; i < kPushes; ++i)
                q->push(&ctx, ctx.fromInteger(p * kStride + i));
        });
    }

    std::vector<long> nextExpected(kProducers, 0);
    long consumed = 0;
    long outOfOrder = 0;
    const long total = kProducers * kPushes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);

    while (consumed < total) {
        const ProtoList* batch = q->takeAll(&consumerCtx);
        const unsigned long n = batch->getSize(&consumerCtx);
        for (unsigned long i = 0; i < n; ++i) {
            const long v = batch->getAt(&consumerCtx, static_cast<int>(i))->asLong(&consumerCtx);
            const long p = v / kStride;
            const long s = v % kStride;
            ASSERT_GE(p, 0);
            ASSERT_LT(p, kProducers);
            if (s != nextExpected[p]) ++outOfOrder;   // per-producer FIFO
            nextExpected[p] = s + 1;
            ++consumed;
        }
        if (n == 0) {
            if (producersDone.load() && consumed >= total) break;
            std::this_thread::yield();
        }
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "consumed " << consumed << " of " << total;
    }

    for (auto& t : producers) t.join();
    producersDone.store(true);

    // Drain anything pushed after the loop's last takeAll.
    for (;;) {
        const ProtoList* batch = q->takeAll(&consumerCtx);
        const unsigned long n = batch->getSize(&consumerCtx);
        if (n == 0) break;
        for (unsigned long i = 0; i < n; ++i) {
            const long v = batch->getAt(&consumerCtx, static_cast<int>(i))->asLong(&consumerCtx);
            const long p = v / kStride;
            const long s = v % kStride;
            if (s != nextExpected[p]) ++outOfOrder;
            nextExpected[p] = s + 1;
            ++consumed;
        }
    }

    space.setHeapLimits(0, 0);
    rs->removeRoot(pinned);

    EXPECT_EQ(consumed, total) << "items lost or duplicated";
    EXPECT_EQ(outOfOrder, 0) << "per-producer FIFO order broken";
    for (long p = 0; p < kProducers; ++p)
        EXPECT_EQ(nextExpected[p], kPushes) << "producer " << p;

    // Self-report, so a silent failure cannot read as a pass.
    std::cout << "MPSC stress: producers=" << kProducers
              << " pushes/producer=" << kPushes
              << " consumed=" << consumed
              << " gc cycles=" << space.getGCCycleCount() << std::endl;
}
```

> Check `ProtoRootSet`'s exact method names (`addRoot`/`removeRoot` vs `pin`/`unpin`) against `headers/protoCore.h:1783-1810` and `test/test_root_set.cpp` before compiling, and use whatever that header declares.

- [ ] **Step 2: Build and run**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
PMQ_STRESS_PUSHES=50000 /home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
  --gtest_filter='MPSCQueueConcurrency*'
```
Expected (smoke run): `MPSC stress: producers=8 pushes/producer=50000 consumed=400000 gc cycles=N` with `N > 0`, then `[  PASSED  ] 1 test.`

Then the full spec size:

```bash
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
  --gtest_filter='MPSCQueueConcurrency*' 2>&1 | tee \
  /home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/stress-8x1M.txt
```
Expected: `consumed=8000000`, `gc cycles` well above zero, `[  PASSED  ] 1 test.` Watch RSS with `ps` in another shell; if it approaches 2 GB, stop and report — the heap limit is not doing its job and the design or the limit needs revisiting before anything else.

- [ ] **Step 3: Run it ten times — a lock-free queue that passes once has proved nothing**

```bash
for i in $(seq 1 10); do
  PMQ_STRESS_PUSHES=200000 /home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
    --gtest_filter='MPSCQueueConcurrency*' > /dev/null 2>&1 || echo "FAILED on run $i"
done; echo done
```
Expected: `done` with no `FAILED` line.

- [ ] **Step 4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add test/ProtoMPSCQueueConcurrencyTests.cpp
git commit -m "test(mpsc): 8 producers x 1M against one consumer

No loss, no duplication, per-producer FIFO preserved, under a bounded heap
so collections run during the stress.  Self-reports the consumed count and
the GC cycle count.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: GC safety — items referenced only by the queue, and pushes during concurrent marking

**Files:**
- Test: `test/ProtoMPSCQueueGCTests.cpp` (create)

**Interfaces:**
- Consumes: the full public API.
- Produces: the PMQ-SPEC §5 GC-safety and `UnmanagedScope` tests.

- [ ] **Step 1: Write the tests, modelled on `test/ProtoMapGCTests.cpp`**

```cpp
// ProtoMPSCQueueGCTests.cpp — PMQ-SPEC §5 GC safety.
//
// Cycles are forced with a small hard heap limit (the pattern of
// ProtoMapGCTests.cpp / GCRootScopeTests.cpp).  Three properties:
//   1. an item whose only reference is the queue survives;
//   2. an item pushed WHILE the collector is marking survives, and so does
//      one that a takeAll detaches while the collector is marking;
//   3. a producer or consumer parked inside UnmanagedScope never delays a
//      pause.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace proto;

namespace {

constexpr int kHeadroomCells = 40000;
constexpr int kGarbagePerBatch = 5000;

uint64_t forceCycles(ProtoSpace& space, ProtoContext* parent, uint64_t minCycles) {
    const uint64_t start = space.getGCCycleCount();
    space.setHeapLimits(/*soft=*/0, /*hard=*/space.heapSize + kHeadroomCells);
    for (int batch = 0; batch < 400 && space.getGCCycleCount() - start < minCycles; ++batch) {
        ProtoContext garbage(&space, parent, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < kGarbagePerBatch; ++i) (void) garbage.newObject(false);
    }
    space.setHeapLimits(0, 0);
    return space.getGCCycleCount() - start;
}

// Builds an item whose identity is verifiable after any number of cycles.
const ProtoObject* probe(ProtoContext* c, long i) {
    return c->newList()->appendLast(c, c->fromInteger(i))
                       ->appendLast(c, c->fromInteger(~i))->asObject(c);
}

bool probeIntact(ProtoContext* c, const ProtoObject* o, long i) {
    const ProtoList* l = o->asList(c);
    return l && l->getSize(c) == 2 &&
           l->getAt(c, 0)->asLong(c) == i &&
           l->getAt(c, 1)->asLong(c) == ~i;
}

}  // namespace

// 1. The queue is the ONLY reference to its items, and the context that
//    created them is gone.  Several cycles later every item is intact.
TEST(MPSCQueueGC, ItemsReferencedOnlyByTheQueueSurvive) {
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoRootSet* rs = space.createRootSet("mpsc-gc");
    ASSERT_NE(rs, nullptr);

    const ProtoMPSCQueue* q = live.newMPSCQueue();
    const ProtoRootSet::Handle pinned = rs->addRoot(q->asObject(&live));
    ASSERT_NE(pinned, ProtoRootSet::kNullHandle);

    constexpr long kN = 2000;
    {
        ProtoContext producer(&space, &live, nullptr, nullptr, nullptr, nullptr);
        for (long i = 0; i < kN; ++i) q->push(&producer, probe(&producer, i));
    }   // the producer context is gone; only the queue holds the items

    const uint64_t cycles = forceCycles(space, &live, 3);
    EXPECT_GE(cycles, 3u) << "no collection ran; the test proves nothing";

    const ProtoList* got = q->takeAll(&live);
    ASSERT_EQ(got->getSize(&live), static_cast<unsigned long>(kN));
    for (long i = 0; i < kN; ++i)
        EXPECT_TRUE(probeIntact(&live, got->getAt(&live, static_cast<int>(i)), i)) << "item " << i;

    rs->removeRoot(pinned);
}

// 2. Pushes and takeAlls that run WHILE the collector marks.  A producer
//    thread pushes continuously and a consumer thread drains continuously
//    while a third thread forces cycles; every item must come out intact
//    and exactly once.
TEST(MPSCQueueGC, PushAndTakeAllDuringConcurrentMarking) {
    ProtoSpace space;
    ProtoContext main(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoRootSet* rs = space.createRootSet("mpsc-gc-mark");
    ASSERT_NE(rs, nullptr);

    const ProtoMPSCQueue* q = main.newMPSCQueue();
    const ProtoRootSet::Handle pinned = rs->addRoot(q->asObject(&main));
    ASSERT_NE(pinned, ProtoRootSet::kNullHandle);

    constexpr long kN = 200000;
    std::atomic<long> produced{0};
    std::atomic<bool> stop{false};
    std::atomic<long> bad{0};
    std::atomic<long> consumed{0};

    std::thread producer([&] {
        ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
        for (long i = 0; i < kN; ++i) {
            q->push(&ctx, probe(&ctx, i));
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
        long next = 0;
        while (!stop.load(std::memory_order_relaxed) || next < kN) {
            const ProtoList* batch = q->takeAll(&ctx);
            const unsigned long n = batch->getSize(&ctx);
            for (unsigned long i = 0; i < n; ++i) {
                const ProtoObject* o = batch->getAt(&ctx, static_cast<int>(i));
                if (!probeIntact(&ctx, o, next)) bad.fetch_add(1, std::memory_order_relaxed);
                ++next;
            }
            consumed.store(next, std::memory_order_relaxed);
            if (n == 0) std::this_thread::yield();
            if (next >= kN) break;
        }
    });

    // Force collections while both run.
    space.setHeapLimits(0, space.heapSize + kHeadroomCells);
    const uint64_t start = space.getGCCycleCount();
    while (consumed.load(std::memory_order_relaxed) < kN &&
           space.getGCCycleCount() - start < 200) {
        ProtoContext garbage(&space, &main, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < 2000; ++i) (void) garbage.newObject(false);
    }
    stop.store(true);
    producer.join();
    consumer.join();
    space.setHeapLimits(0, 0);
    rs->removeRoot(pinned);

    const uint64_t cycles = space.getGCCycleCount() - start;
    std::cout << "MPSC mark-race: produced=" << produced.load()
              << " consumed=" << consumed.load()
              << " corrupt=" << bad.load()
              << " gc cycles=" << cycles << std::endl;

    EXPECT_GE(cycles, 5u) << "no collection overlapped the traffic";
    EXPECT_EQ(bad.load(), 0) << "an item was collected, recycled or reordered";
    EXPECT_EQ(consumed.load(), kN);
}

// 3. A producer and a consumer parked inside UnmanagedScope must not delay
//    a pause: the collector completes cycles while they sit there.
TEST(MPSCQueueGC, ParkedProducerAndConsumerDoNotDelayAPause) {
    ProtoSpace space;
    ProtoContext main(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ProtoMPSCQueue* q = main.newMPSCQueue();
    ProtoRootSet* rs = space.createRootSet("mpsc-unmanaged");
    const ProtoRootSet::Handle pinned = rs->addRoot(q->asObject(&main));

    for (long i = 0; i < 100; ++i) q->push(&main, probe(&main, i));

    std::atomic<bool> release{false};
    std::atomic<bool> parked{0};
    std::vector<std::thread> sleepers;
    for (int t = 0; t < 2; ++t) {
        sleepers.emplace_back([&] {
            ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
            ProtoContext::UnmanagedScope u(&ctx);     // no ProtoObject* access in here
            parked.store(true);
            while (!release.load(std::memory_order_relaxed))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }
    while (!parked.load()) std::this_thread::yield();

    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t cycles = forceCycles(space, &main, 3);
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    release.store(true);
    for (auto& t : sleepers) t.join();

    EXPECT_GE(cycles, 3u) << "the parked threads blocked the collector";
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 30)
        << "collections took far longer with threads parked in UnmanagedScope";

    const ProtoList* got = q->takeAll(&main);
    EXPECT_EQ(got->getSize(&main), 100u);
    for (long i = 0; i < 100; ++i)
        EXPECT_TRUE(probeIntact(&main, got->getAt(&main, static_cast<int>(i)), i));
    rs->removeRoot(pinned);
}
```

- [ ] **Step 2: Run them**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests \
  --gtest_filter='MPSCQueueGC*' 2>&1 | tee \
  /home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/gc-safety.txt
```
Expected: `MPSC mark-race: produced=200000 consumed=200000 corrupt=0 gc cycles=N` with `N >= 5`, and `[  PASSED  ] 3 tests.`

**If `corrupt` is ever non-zero, the design is wrong, not the test.** Do not add a retry, do not widen a window, do not weaken the assertion: re-read the proof in `core/ProtoMPSCQueue.cpp`, find which of the two orderings the code violates, and stop to report if the proof itself is at fault.

- [ ] **Step 3: Negative control — prove the test can fail**

Temporarily swap the two loads in `ProtoMPSCQueueImplementation::processReferences` (`retained` before `head`) and re-run `MPSCQueueGC.PushAndTakeAllDuringConcurrentMarking` a few times. A test that cannot fail proves nothing; record whether it caught the inversion in `/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq/negative-control.txt`, then **revert the swap** and re-run to green before committing. If the inverted order does *not* fail within ten runs, say so in the report: the test needs a tighter race (more cycles, a smaller heap headroom, a larger item) before this task can be called done.

- [ ] **Step 4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add test/ProtoMPSCQueueGCTests.cpp
git commit -m "test(mpsc): GC safety under forced collections

Items referenced only by the queue survive; pushes and takeAlls that run
while the collector marks lose and corrupt nothing; a producer and a
consumer parked in UnmanagedScope do not delay a pause.  The mark-race
test is verified to fail when processReferences reads retained before head.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: ThreadSanitizer

**Files:** none created; uses `build_tsan` from Task 1.

- [ ] **Step 1: Rebuild the TSan tree and run the queue tests**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
cmake --build $P/build_tsan -j4
PMQ_STRESS_PUSHES=100000 $P/build_tsan/test/proto_tests \
  --gtest_filter='MPSCQueue*' 2>&1 | tee $S/tsan-mpsc.txt
grep -c "WARNING: ThreadSanitizer" $S/tsan-mpsc.txt
```
Expected: the tests pass (TSan slows them by ~10x, so the 100 K size is deliberate).

- [ ] **Step 2: Classify every report against the Task 1 baseline**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
grep -A 6 "WARNING: ThreadSanitizer" $S/tsan-mpsc.txt | grep -E "ProtoMPSCQueue|push|takeAll" | sort -u
```
Expected: **empty**. Required outcome: zero races whose stack touches `ProtoMPSCQueue::push`, `ProtoMPSCQueue::takeAll`, `ProtoMPSCQueueImplementation`, `...NodeImplementation` or `...RetainImplementation` **other than** the known constructor-publication pattern described in Task 0 ("Recorded, not asked"): `Cell(context)` links a cell into the young chain before the derived constructor writes `item`, and the collector's young-chain walk may read it. If that one appears, record it in `$S/tsan-mpsc.txt` with the note that it is the pre-existing protoCore pattern shared by every cell type (also visible in the Task 1 baseline for `ProtoMap`/`ProtoList`), and confirm the same shape appears in the baseline. **Any other race on a queue path stops the task.**

- [ ] **Step 3: ASan run for completeness**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
cmake -S $P -B $P/build_asan_p2 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build $P/build_asan_p2 -j4
PMQ_STRESS_PUSHES=100000 $P/build_asan_p2/test/proto_tests --gtest_filter='MPSCQueue*'
```
Expected: `[  PASSED  ] 15 tests` (the GC and concurrency ones included), no ASan or UBSan report.

- [ ] **Step 4: Commit the findings** (no source change; record them in the scratch directory and carry them into Task 11's CHANGELOG entry). If Step 2 forced a source change, commit it here with a message naming the race.

---

### Task 8: Self-reporting microbenchmark against the two mailboxes it replaces

**Files:**
- Create: `performance/mpsc_queue_benchmark.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `mpsc_queue_benchmark`, printing a table and `VERIFIED` / `MISMATCH`, exit code 0 only when every mode's consumed count matches the pushed count.

The two baselines are reimplemented in the benchmark rather than linked from the other repositories, so all three run in one process, on one machine, in one measurement — the only honest comparison:

- **protoClojure shape:** three `std::atomic<ActorMessage*>` CAS-prepend stacks on the C++ heap, exchanged and reversed by the consumer (`protoClojure/src/runtime/ActorScheduler.h:61-89`). Lock-free and O(1) — **and the payload is not rooted**, which is the defect PMQ-SPEC §1 records and this type closes. The benchmark states that in its output so the number is never quoted without the caveat.
- **protoST shape:** a mutable object whose `__mailbox__` attribute holds a `ProtoList`, updated with `setAttributeIfEqual` in a CAS-retry loop (`protoST/src/runtime/STRuntime.cpp:1374-1397`). GC-safe and O(log n) per message with a retry under contention.

- [ ] **Step 1: Write the benchmark**

```cpp
/*
 * mpsc_queue_benchmark.cpp
 *
 * ProtoMPSCQueue against the two mailboxes it replaces, in one process:
 *
 *   protoCore ProtoMPSCQueue  — lock-free, O(1), GC-traced
 *   protoClojure-shaped stack — std::atomic<Msg*> CAS stack on the C++
 *                               heap: lock-free and O(1), but the payload
 *                               is NOT a GC root (the defect PMQ-SPEC §1
 *                               records).  Its number is a floor, not a
 *                               target: it buys its speed by being unsafe.
 *   protoST-shaped mailbox    — ProtoList under a mutable attribute,
 *                               updated with setAttributeIfEqual: GC-safe
 *                               and O(log n) per message.
 *
 * Every mode verifies the consumed multiset against the pushed one and
 * prints what it did.  The program exits non-zero on any mismatch, so a
 * silent failure can never be read as throughput.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "../headers/protoCore.h"

using namespace proto;

namespace {

struct Result {
    const char* name;
    int producers;
    long pushed;
    long consumed;
    double pushSeconds;
    double drainSeconds;
    long batches;
    bool gcSafe;
};

double seconds(std::chrono::steady_clock::time_point a,
               std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

constexpr long kStride = 10000000;

// ---------------------------------------------------------------- protoCore
Result runProtoCore(ProtoSpace& space, ProtoContext* main, int producers, long perProducer) {
    const ProtoMPSCQueue* q = main->newMPSCQueue();
    ProtoRootSet* rs = space.createRootSet("bench-pmq");
    const ProtoRootSet::Handle pinned = rs->addRoot(q->asObject(main));

    std::atomic<long> consumed{0};
    std::atomic<long> batches{0};
    std::atomic<bool> done{false};
    const long total = static_cast<long>(producers) * perProducer;
    double drain = 0.0;

    const auto t0 = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
        const auto d0 = std::chrono::steady_clock::now();
        long got = 0;
        while (got < total) {
            const ProtoList* batch = q->takeAll(&ctx);
            const unsigned long n = batch->getSize(&ctx);
            if (n == 0) { std::this_thread::yield(); continue; }
            batches.fetch_add(1, std::memory_order_relaxed);
            for (unsigned long i = 0; i < n; ++i)
                got += (batch->getAt(&ctx, static_cast<int>(i)) != nullptr) ? 1 : 0;
        }
        consumed.store(got);
        drain = seconds(d0, std::chrono::steady_clock::now());
    });

    std::vector<std::thread> ps;
    for (int p = 0; p < producers; ++p) {
        ps.emplace_back([&, p] {
            ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
            for (long i = 0; i < perProducer; ++i)
                q->push(&ctx, ctx.fromInteger(p * kStride + i));
        });
    }
    for (auto& t : ps) t.join();
    const auto t1 = std::chrono::steady_clock::now();
    done.store(true);
    consumer.join();
    rs->removeRoot(pinned);

    return {"ProtoMPSCQueue", producers, total, consumed.load(),
            seconds(t0, t1), drain, batches.load(), true};
}

// ------------------------------------------------------- protoClojure shape
struct Msg { long value; Msg* next; };

Result runClojureShape(int producers, long perProducer) {
    std::atomic<Msg*> head{nullptr};
    std::atomic<long> consumed{0};
    std::atomic<long> batches{0};
    const long total = static_cast<long>(producers) * perProducer;
    double drain = 0.0;

    const auto t0 = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        const auto d0 = std::chrono::steady_clock::now();
        long got = 0;
        while (got < total) {
            Msg* chain = head.exchange(nullptr, std::memory_order_acq_rel);
            if (!chain) { std::this_thread::yield(); continue; }
            batches.fetch_add(1, std::memory_order_relaxed);
            while (chain) { Msg* n = chain->next; delete chain; chain = n; ++got; }
        }
        consumed.store(got);
        drain = seconds(d0, std::chrono::steady_clock::now());
    });

    std::vector<std::thread> ps;
    for (int p = 0; p < producers; ++p) {
        ps.emplace_back([&, p] {
            for (long i = 0; i < perProducer; ++i) {
                Msg* m = new Msg{p * kStride + i, nullptr};
                Msg* h = head.load(std::memory_order_relaxed);
                do { m->next = h; }
                while (!head.compare_exchange_weak(h, m, std::memory_order_release,
                                                   std::memory_order_relaxed));
            }
        });
    }
    for (auto& t : ps) t.join();
    const auto t1 = std::chrono::steady_clock::now();
    consumer.join();

    return {"protoClojure atomic stack (payload NOT rooted)", producers, total,
            consumed.load(), seconds(t0, t1), drain, batches.load(), false};
}

// ----------------------------------------------------------- protoST shape
Result runSTShape(ProtoSpace& space, ProtoContext* main, int producers, long perProducer) {
    const ProtoObject* actor = main->newObject(true);
    const ProtoString* key = ProtoString::createSymbol(main, "__mailbox__");
    actor->setAttribute(main, key, main->newList()->asObject(main));

    ProtoRootSet* rs = space.createRootSet("bench-st");
    const ProtoRootSet::Handle pinned = rs->addRoot(actor);

    std::atomic<long> consumed{0};
    std::atomic<long> batches{0};
    const long total = static_cast<long>(producers) * perProducer;
    double drain = 0.0;

    const auto t0 = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
        const auto d0 = std::chrono::steady_clock::now();
        long got = 0;
        while (got < total) {
            // The whole-batch equivalent of takeAll: swap the list for an
            // empty one, exactly as protoST's drain does per turn.
            const ProtoObject* old = actor->getOwnAttributeDirect(&ctx, key);
            const ProtoList* mb = old ? old->asList(&ctx) : nullptr;
            if (!mb || mb->getSize(&ctx) == 0) { std::this_thread::yield(); continue; }
            const ProtoObject* empty = ctx.newList()->asObject(&ctx);
            if (!const_cast<ProtoObject*>(actor)->setAttributeIfEqual(&ctx, key, old, empty))
                continue;
            batches.fetch_add(1, std::memory_order_relaxed);
            got += static_cast<long>(mb->getSize(&ctx));
        }
        consumed.store(got);
        drain = seconds(d0, std::chrono::steady_clock::now());
    });

    std::vector<std::thread> ps;
    for (int p = 0; p < producers; ++p) {
        ps.emplace_back([&, p] {
            ProtoContext ctx(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
            for (long i = 0; i < perProducer; ++i) {
                for (;;) {
                    const ProtoObject* old = actor->getOwnAttributeDirect(&ctx, key);
                    const ProtoList* mb = old ? old->asList(&ctx) : ctx.newList();
                    const ProtoObject* neu =
                        mb->appendLast(&ctx, ctx.fromInteger(p * kStride + i))->asObject(&ctx);
                    if (const_cast<ProtoObject*>(actor)->setAttributeIfEqual(&ctx, key, old, neu))
                        break;
                }
            }
        });
    }
    for (auto& t : ps) t.join();
    const auto t1 = std::chrono::steady_clock::now();
    consumer.join();
    rs->removeRoot(pinned);

    return {"protoST CAS-list mailbox", producers, total, consumed.load(),
            seconds(t0, t1), drain, batches.load(), true};
}

void report(const Result& r, bool& ok) {
    const bool good = r.consumed == r.pushed;
    ok = ok && good;
    std::cout << "  " << r.name
              << " | producers=" << r.producers
              << " | pushed=" << r.pushed
              << " | consumed=" << r.consumed
              << " | push=" << r.pushSeconds << " s"
              << " | drain=" << r.drainSeconds << " s"
              << " | batches=" << r.batches
              << " | msgs/s=" << (r.pushSeconds > 0 ? r.pushed / r.pushSeconds : 0)
              << " | gc-safe=" << (r.gcSafe ? "yes" : "NO")
              << (good ? "" : "  <-- MISMATCH") << std::endl;
}

}  // namespace

int main() {
    // protoST's mailbox is O(n) per send in list terms; keep the per-mode
    // count small enough that it finishes, and say so in the output.
    const long perProducerFast = 250000;
    const long perProducerST = 20000;

    std::cout << "--- ProtoMPSCQueue microbenchmark (PMQ-SPEC §5) ---" << std::endl;
    std::cout << "Each row states the work it did; the runner verifies consumed == pushed."
              << std::endl;

    bool ok = true;
    for (int producers : {1, 2, 4, 8}) {
        ProtoSpace space;
        ProtoContext main(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
        report(runProtoCore(space, &main, producers, perProducerFast), ok);
        report(runClojureShape(producers, perProducerFast), ok);
        report(runSTShape(space, &main, producers, perProducerST), ok);
    }

    std::cout << (ok ? "VERIFIED" : "MISMATCH") << std::endl;
    return ok ? 0 : 1;
}
```

> `getOwnAttributeDirect`'s exact signature and `ProtoString::createSymbol`'s argument list must be checked against `headers/protoCore.h` before compiling; the JSSymbols-identity lesson applies — create the symbol once, outside the loops, and reuse the pointer.

- [ ] **Step 2: Register and run it**

In `CMakeLists.txt`, next to the other benchmarks:

```cmake
add_executable(mpsc_queue_benchmark performance/mpsc_queue_benchmark.cpp)
target_link_libraries(mpsc_queue_benchmark PRIVATE protoCore)
message(STATUS "Configured benchmark: mpsc_queue_benchmark")
```

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
cmake -S $P -B $P/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/build_release -j4
$P/build_release/mpsc_queue_benchmark | tee $S/mpsc-bench.txt
```
Expected: twelve rows, every one with `consumed == pushed`, and a final `VERIFIED`; exit code 0 (`echo $?`).

- [ ] **Step 3: Back the headline number with cycles**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
perf stat -r 3 -e cycles,instructions $P/build_release/mpsc_queue_benchmark > $S/bench.out 2> $S/perf-bench.txt
grep -c VERIFIED $S/bench.out
```
Expected: `3`, and `perf-bench.txt` holds the mean cycles with its `+-` variance. Quote no improvement that is not backed by this file.

- [ ] **Step 4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add performance/mpsc_queue_benchmark.cpp CMakeLists.txt
git commit -m "perf(mpsc): self-reporting benchmark against protoST and protoClojure mailboxes

One process, one machine: ProtoMPSCQueue, a protoClojure-shaped atomic
stack (fast but with an unrooted payload, stated in the output) and a
protoST-shaped setAttributeIfEqual ProtoList mailbox.  Every row reports
what it did and the runner verifies consumed == pushed before any rate is
computed.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Full protoCore verification

**Files:** none modified unless a failure demands it.

- [ ] **Step 1: Release — the whole suite**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
cmake --build $P/build_release -j4
ctest --test-dir $P/build_release --output-on-failure 2>&1 | tail -30 | tee $S/protocore-tests-after.txt
diff <(grep -oE "^[0-9]+ - [A-Za-z0-9_./]+" $S/protocore-tests-before.txt | sort) \
     <(grep -oE "^[0-9]+ - [A-Za-z0-9_./]+" $S/protocore-tests-after.txt | sort) || true
```
Expected: `100% tests passed` or exactly the pre-existing failures from Task 1 — nothing new.

- [ ] **Step 2: Debug build — the `toImpl` assertions and `pushReportedReference`'s null check are live there**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
cmake -S $P -B $P/build-dbg-p2 -DCMAKE_BUILD_TYPE=Debug
cmake --build $P/build-dbg-p2 -j4
PMQ_STRESS_PUSHES=100000 ctest --test-dir $P/build-dbg-p2 --output-on-failure 2>&1 | tail -20
```
Expected: no `Type mismatch in toImpl conversion`, no `null reference reported by a cell of type`, no `CRITICAL TAGGED POINTER`.

- [ ] **Step 3: Instrumented GC build — the per-phase pause numbers must not move**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
cmake -S $P -B $P/build_stats_p2 -DCMAKE_BUILD_TYPE=Release -DPROTOCORE_GC_INSTRUMENT=ON
cmake --build $P/build_stats_p2 -j4
PROTOCORE_GC_PROFILE=1 PMQ_STRESS_PUSHES=100000 $P/build_stats_p2/test/proto_tests \
  --gtest_filter='MPSCQueue*' 2>&1 | grep -E "P1=|P2=" | tail -20 | tee $S/gc-phases-mpsc.txt
PROTOCORE_GC_PROFILE=1 $P/build_stats_p2/test/proto_tests \
  --gtest_filter='MapGC*' 2>&1 | grep -E "P1=|P2=" | tail -20 | tee $S/gc-phases-map.txt
```
Expected: the P1 + P2 (stop-the-world) totals per cycle are of the same order in both files. The queue adds one `addRootObj`; if the pause grows measurably with queue traffic, something in this plan was implemented as pause work and must be found before the task closes.

- [ ] **Step 4: Performance non-regression on the unrelated benchmarks**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p2-pmq
perf stat -r 3 -e cycles,instructions $P/build_release/object_access_benchmark > $S/object-after.out 2> $S/perf-object-after.txt
perf stat -r 3 -e cycles,instructions $P/build_release/sparse_list_benchmark > $S/sparse-after.out 2> $S/perf-sparse-after.txt
tail -5 $S/perf-object-after.txt $S/perf-sparse-after.txt
```
Expected: within noise of a run on the same machine with the pre-Task-2 library (this plan touches no code either benchmark executes; a change here means an accidental edit).

- [ ] **Step 5: Commit any fix this task produced**, with a message naming the failure it repairs. If nothing failed, this task commits nothing.

---

### Task 10: Documentation — GC design note

**Files:**
- Modify: `docs/GarbageCollector.md`

- [ ] **Step 1: Add the subsection**

After the "Concurrent Mark Without Barriers" section, add "Lock-free queues without barriers", stating in prose what `core/ProtoMPSCQueue.cpp`'s header comment states in detail: why a mutable word read lazily by the marker is normally unsound here (the consumed items land in a post-pause young `ProtoList` whose references this cycle never walks, while the items themselves are candidates), and the two orderings that make `ProtoMPSCQueue` sound without a barrier, a capture or a pause addition. Cross-reference `protoScala/docs/platform/PMQ-SPEC.md` and `core/ProtoMPSCQueue.cpp`.

- [ ] **Step 2: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add docs/GarbageCollector.md
git commit -m "docs(gc): record how ProtoMPSCQueue stays safe without a barrier

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 11: Version bump, changelog, spec status and decisions

**Files:**
- Modify: `CMakeLists.txt`, `CHANGELOG.md` (protoCore)
- Modify: `docs/platform/PMQ-SPEC.md` (protoScala)

- [ ] **Step 1: Version**

In `CMakeLists.txt`, `VERSION 2.0.0` → `VERSION 2.1.0`. `SOVERSION` stays `2` (D5).

- [ ] **Step 2: CHANGELOG**

Add above the `[2.0.0]` entry:

```markdown
## [2.1.0] - 2026-09-XX

### Added

- **`ProtoMPSCQueue`** — a mutable, lock-free, multi-producer /
  single-consumer FIFO of `ProtoObject*` items whose contents the collector
  traces (spec `protoScala/docs/platform/PMQ-SPEC.md`). `push` is lock-free,
  O(1) and allocates one cell; `takeAll` returns every queued item in push
  order as an immutable `ProtoList`; `isEmpty` is a snapshot. One pointer
  tag (28) for the handle, three `CellType`s, a dedicated prototype, and
  `ProtoContext::newMPSCQueue` / `ProtoObject::isMPSCQueue` /
  `ProtoObject::asMPSCQueue`.

  It is the shared actor mailbox of protoScala Phase 5, protoClojure and
  protoST, and it closes protoClojure's unrooted-payload defect: a queued
  message, its arguments and its reply future are now GC roots.

  **It adds nothing to the stop-the-world pause** beyond one O(1) global
  prototype root, needs no write barrier and changes no collector phase.
  Correctness under concurrent marking rests on two orderings, proved and
  documented in `core/ProtoMPSCQueue.cpp` and `docs/GarbageCollector.md`:
  `processReferences` loads `head` before `retained`, and `takeAll`
  publishes its retain cell before it detaches a chain.

### Changed

- `ProtoSpace` gains one field (`mpscQueuePrototype`). The soname stays
  `libprotoCore.so.2`, so **every embedder must still be rebuilt from
  clean**: a stale binary would use the old layout.
- Pointer-tag budget: used 0-28 (29), free 29-63 (35).
```

- [ ] **Step 3: PMQ-SPEC status and decisions**

In `protoScala/docs/platform/PMQ-SPEC.md`: change the status line to `implemented (protoCore 2.1.0, branch feature/pmq-p2)` and append `## 7. Decisions (P2)` with D1-D8 verbatim from Task 0, each marked *decided by the agent on 2026-09-23 under maintainer authorisation* — since **approved by the maintainer on 2026-09-23** — plus the "Recorded, not asked" list — in particular the §3.4 deviation (nothing is captured under the pause; the lazy read is made safe instead).

- [ ] **Step 4: Rebuild, re-run, commit both repositories separately**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
cmake -S $P -B $P/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/build_release -j4
ls -l $P/build_release/libprotoCore.so*
ctest --test-dir $P/build_release --output-on-failure 2>&1 | tail -5
```
Expected: `libprotoCore.so.2.1.0` with the `libprotoCore.so.2` soname link, suite green.

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: protoCore 2.1.0 — ProtoMPSCQueue

Minor bump, ABI addition (PMQ-SPEC §6.1).  SOVERSION stays 2; the
ProtoSpace layout changed, so every embedder is rebuilt from clean.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"

cd /home/gamarino/Documentos/proyectos/protoScala
git add docs/platform/PMQ-SPEC.md
git commit -m "docs(platform): PMQ-SPEC implemented; record the P2 decisions

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: Hand-off — what the maintainer must rebuild and re-run

**Files:** none. This task produces a report, and runs only what the maintainer approves.

protoCore's ABI changed (a new `ProtoSpace` field), so **every embedder must be rebuilt from clean before it is trusted**. The agent does not start this sweep on its own: protoJS builds and test262 runs have hung DEV12 before.

- [ ] **Step 1: Ask the user before running anything in this task.** Present the list below and the expected duration, and wait.

- [ ] **Step 2: Rebuild and test each embedder from clean, in this order, sequentially**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p2-pmq

# protoPython
cmake --build $P/protoPython/build_release --clean-first -j4
ctest --test-dir $P/protoPython/build_release --output-on-failure 2>&1 | tail -15 | tee $S/after-protoPython.txt

# protoST
cmake --build $P/protoST/build_release --clean-first -j4
ctest --test-dir $P/protoST/build_release --output-on-failure 2>&1 | tail -15 | tee $S/after-protoST.txt

# protoClojure
cmake --build $P/protoClojure/build_release --clean-first -j4
ctest --test-dir $P/protoClojure/build_release -j1 --output-on-failure 2>&1 | tail -15 | tee $S/after-protoClojure.txt

# protoScala
cmake --build $P/protoScala/build_release --clean-first -j4
ctest --test-dir $P/protoScala/build_release --output-on-failure 2>&1 | tail -15 | tee $S/after-protoScala.txt

# protoJS — NO -j, and test262 strictly sequential.  Ask again before this one.
cmake --build $P/protoJS/build_release --clean-first
TEST262_CONCURRENCY=1 ctest --test-dir $P/protoJS/build_release -j1 --output-on-failure 2>&1 | tail -15 | tee $S/after-protoJS.txt
```
Expected: each embedder's suite matches its own pre-change baseline. A newly failing embedder test is a P2 regression until proved otherwise — investigate before reporting success.

- [ ] **Step 3: Report to the maintainer**

The final report states, in this order:

1. **Branch:** `feature/pmq-p2` in protoCore (not pushed), plus one commit in protoScala on its current branch.
2. **The decision to review first:** D1 — the queue adds **nothing** to the stop-the-world pause; it makes a lazy read of its mutable state safe by ordering (`head` before `retained` in `processReferences`; retain-before-detach in `takeAll`) instead of capturing it under the pause as PMQ-SPEC §3.4 anticipated. Then D2 (the one relaxed `gcCycleCount` read per `takeAll` batch that releases the retain chain) and D5 (`SOVERSION` kept at 2).
3. **Evidence:** the 8 × 1M no-loss/no-duplication run, the mark-race run with its corrupt count of zero **and the negative control that proves the test can fail**, the TSan classification against the Task 1 baseline, the benchmark table with `VERIFIED`, and the instrumented-GC pause comparison.
4. **What must be rebuilt:** protoPython, protoJS, protoST, protoClojure, protoScala — from clean, with protoJS unparallelised and test262 sequential.
5. **What comes next, and is NOT in this plan:** protoScala Phase 5 builds its mailboxes on the new type; track C replaces protoClojure's three `std::atomic<ActorMessage*>` stacks (closing the unrooted-payload defect) and re-runs `actor-bench.sh` before and after, recording both tables; track S replaces protoST's `__mailbox__` CAS list and re-runs its actor benchmarks the same way (PMQ-SPEC §6.3-6.5).
6. **Confirmation** that nothing outside `/home/gamarino/Documentos/proyectos` was created, modified or deleted.

---

## Self-review against PMQ-SPEC (done while writing; gaps fixed inline)

- **§2 semantics.** `push` lock-free and O(1) — Task 3 (one cell, one CAS). `takeAll` removes everything pushed so far in push order as an immutable `ProtoList` — Task 3, tested in Task 3 Step 1 and Task 5. Single consumer guaranteed by the caller, and a violation stays memory-safe — the `h == nullptr` re-check inside the publish window handles a racing consumer without touching freed memory; the partition of items is then unspecified, as the spec allows. Items never copied — asserted by identity in `MPSCQueue.ItemsAreReturnedByIdentity`. Never lost, never duplicated — Task 5.
- **§2.1 API.** Every signature is reproduced verbatim, including the `const` qualifiers, `ProtoContext::newMPSCQueue`, `ProtoObject::isMPSCQueue` and `ProtoObject::asMPSCQueue`. *Gap found and fixed while writing:* the `const` methods force the three mutable words to be `mutable std::atomic<...>`, which is a first for protoCore — recorded in Task 0 so the maintainer sees it rather than discovering it in a diff.
- **§3.1 no new stop-the-world work.** Nothing is added except one `addRootObj` for the prototype (O(1), a global-structure root, the same addition `ProtoMap` made). Verified empirically in Task 9 Step 3 against the instrumented build.
- **§3.2 no write barriers.** `push` does no GC bookkeeping at all. `takeAll` reads `gcCycleCount` once per batch; Task 0 D2 argues, and the report repeats, why that is reclamation pacing and not a barrier — and lists the two barrier-free alternatives if the maintainer disagrees.
- **§3.3 no lost items under concurrent marking.** The proof is written into the source before the code (Task 2 Step 1), tested in Task 6, and the test is proved capable of failing by the negative control in Task 6 Step 3. Items pushed during marking are additionally covered by the spec's own escape hatch: the node is a young cell of the pushing context.
- **§3.4 capture of reachable state.** *Deviation, flagged.* Nothing is captured; the lazy read is made safe instead. Strictly less pause work than either option the spec anticipated, but the maintainer must confirm it.
- **§4 tag budget.** One tag (28) for the handle; node and retain cells take none; the budget comment is updated to "used 0-28 (29), free 29-63 (35)", exactly the count §4 predicts. No iterator.
- **§5 tests.** FIFO for one producer ✓; per-producer order with several producers ✓; `takeAll` on an empty queue ✓; 8 × 1M no loss/duplication ✓ (Task 5); GC safety under a low heap limit including pushes during concurrent marking ✓ (Task 6); parked producer/consumer inside `UnmanagedScope` ✓ (Task 6); TSan ✓ (Task 7, against a recorded baseline rather than an unachievable "zero races"); self-reporting microbenchmark against protoST's CAS-list mailbox and protoClojure's atomic stacks ✓ (Task 8, all three in one process on one machine, with the unrooted-payload caveat printed next to protoClojure's number).
- **§6 rollout.** Minor version bump ✓ (Task 11); full clean rebuild and test of every embedder ✓ (Task 12, gated on the user's approval, protoJS unparallelised and sequential); steps 3-5 (protoScala Phase 5, track C, track S) are explicitly named as out of scope and handed to the maintainer.
- **Gap found and fixed while writing:** PMQ-SPEC's §2 wording ("removes every item pushed so far") allows `takeAll` to return items pushed after it started. The implementation's `head.exchange` does exactly that, and the analysis in the source header shows why those extra nodes are safe without being retained. The behaviour is documented in the public class comment so an actor runtime does not assume a hard cut-off.
- **Gap found and fixed while writing:** the retain stack's link cannot be folded into the detached chain's own head node — that node may already be marked, and the marker never revisits a marked cell, so the link would be invisible and every older retained chain would be lost. Hence a *fresh* retain cell per `takeAll`, and a comment in the class declaration saying why.
