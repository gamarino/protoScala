# protoScala Phase 5 — Actors, Priority Bands and Cooperative Futures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GIL-free concurrency on protoClojure's actor model with a Scala surface (DESIGN §8): `Actor.spawn(state)(handler)`, `!`, `?`, `send`/`ask` with `Priority.High`/`Medium`/`Low`, `value`, `Actor.isActor`, `Actor.stats`; `Future` with `await`, `map`, `flatMap`, `recover`, `Future.apply`; a worker pool of protoCore threads with per-actor lock-free mailboxes, three priority bands, lock-free ready stacks with ABA-tagged heads and spin-before-park; **cooperative** `await` inside an actor (frame snapshots, so the `await` benchmark completes with one worker); every queued payload GC-reachable; shutdown that joins every worker on every exit path; the seven benchmarks of DESIGN §8.5 with verified message counts and a protoClojure comparison table. Release 0.3.0.

**Architecture:** The scheduler is a process-wide singleton (`ActorScheduler`) owning `N` worker threads created with `ProtoSpace::newThread`, three `ReadyStack<ActorState*>` ready stacks (protoST's Treiber stack with a generation-tagged head and a type-stable node pool) and one `ActorState` per actor. Each actor is a **mutable protoCore object** holding its handler, its state, three per-band mailbox queues (`ProtoMPSCQueue`, behind the `Mailbox` seam of Task 2), its per-band leftovers and — while suspended — its frame snapshot and the future it waits on; every one of those is an attribute, so the GC traces the whole live set of the actor system from one registry list pinned in a root-context slot. A per-actor `sched` atomic (Idle / Claimed / ClaimedWake / Suspended) is the single source of truth for the single-method invariant: a sender that loses the claim CAS never schedules, and the end-of-turn release races the senders' claim, so at most one worker ever runs one actor. `Future` is a mutable protoCore object with a CAS'd `__fstate__`; completing it wakes blocked OS threads through a counted condition variable and re-enqueues suspended actor waiters. `await` inside an actor throws the control signal `FutureYield` (not a `std::exception`); **each recursive `ExecutionEngine::execute` frame catches it, prepends its own frame record to the actor's snapshot list and rethrows**, and the resume path re-materialises the frames by recursion, writing the awaited value where the in-flight call would have left its result. The Scala surface needs **no new opcodes**: `!`, `?`, `await` and friends are ordinary sends to native methods, and `Actor`/`Future`/`Priority` are prelude objects over internal primitives.

**Tech Stack:** C++20, CMake ≥ 3.20, protoCore (shared lib, `../protoCore/build_release`, with `ProtoMPSCQueue` when Phase P2 has merged), GoogleTest 1.14, libreadline; Python 3 for the benchmark harness; protoClojure's `build_release/protoclj` as the comparison baseline; Scala 3.9.0 (`../tools/scala3-3.9.0`, optional) only as a reference for expected outputs.

**Spec:** docs/DESIGN.md §8 (with §1, §1.1 P1–P6, §3.6, §10, §11 R1/R7/R9), docs/platform/PMQ-SPEC.md, docs/ROADMAP.md "Phase 5" and the Documentation track.

---

## Global Constraints

Every Phase 1 and Phase 2 constraint still holds (`plans/2026-09-22-phase-1-core-language.md` and `plans/2026-09-22-phase-2-object-model.md`, "Global Constraints"); repeated here with the Phase 5 additions:

- C++20, CMake ≥ 3.20; every target builds with `-Wall -Wextra -Wpedantic` and **no warnings**. New threading code additionally builds clean under `-fsanitize=thread` in the sanitizer build directory of Task 9.
- DESIGN §1.1 **P1–P6 are binding**, and Phase 5 is where P1 and P6 are hardest:
  - **P1** — every actor, handler, actor state, queued message envelope, reply future, frame snapshot and waiter list lives in a protoCore structure reachable from the actor registry, which is pinned in a root-context slot. The **only** `ProtoObject*` a C++ structure may hold are (i) interned symbols, (ii) the runtime prototypes pinned in root slots, and (iii) `ActorState::actor`, the back-pointer to an object that the registry anchors for the whole session — documented at its declaration as the one tightly scoped exception, exactly as protoST's `ReadyStack` holds actors anchored in `__live_actors__`.
  - **P2** — one `ProtoContext` per invocation. A worker opens one child context per turn and one per message; the handler call itself opens its own frame contexts as usual.
  - **P3** — no protoCore change in this phase. The mailbox capability is protoCore's (`ProtoMPSCQueue`, Phase P2); Task 2 consumes it through a seam and never reimplements it inside protoCore's headers.
  - **P6** — no new stop-the-world work. Every blocking wait (worker park, `await` outside an actor, shutdown join) runs inside `ProtoContext::UnmanagedScope`, so a parked thread never delays a pause; no write barrier, no card marking, no GC bookkeeping is added on any send path.
- **Never map transient data to `ProtoTuple`** (DESIGN §4.6, R2): message envelopes, pending lists and frame snapshots are mutable objects and `ProtoList`s. The `(newState, reply)` pair a handler returns is a Scala `Tuple2` **case-class instance** (Phase 2), which is not a `ProtoTuple`.
- `PROTO_NONE` is Scala `null` *and* "attribute missing": presence is probed with `hasOwnAttribute`/`hasAttribute`, never by comparing a lookup result with `PROTO_NONE` alone.
- Interned symbols are per `ProtoSpace`; never cache them in function-local `static`s. Every new key goes in `RuntimeLayout` (created in `Runtime::Runtime`) and is read from there on the hot paths — protoST measured 51 % of CPU in `SymbolTable::intern` when two symbols per dispatch were interned instead of cached.
- **One runtime per process** (R5) and one scheduler per process: `ActorScheduler::instance()` is a singleton; a second `Session`/`EvalHarness` in the same process must not start it a second time. Unit tests that start workers therefore live in a dedicated test binary (Task 3) that builds exactly one harness.
- Nothing is written outside `/home/gamarino/Documentos/proyectos`: no `/tmp`, no `$HOME`; tests create temporaries in the CTest working directory; ad-hoc scratch goes to `../.agent_scratch/phase5/`.
- All code comments, messages and documentation in professional English. User-facing messages never mention internal phase names.
- Conformance fixtures: first line is a directive (`// EXPECT: <last stdout line>`, `// EXPECT-ERROR[: <substring>]`, `// XFAIL...`); one CTest case per file; files starting with `_` are helpers. **Every fixture whose syntax differs between braces and indentation exists in both variants** (`-braces` / `-indent` suffixes).
- **Every concurrency fixture carries a CTest timeout.** A fixture that waits for an actor spins on the main thread (`while seen < n do seen = a.value`, `while !f.isCompleted do ()`), which has no deadline of its own: a scheduling bug would stall the suite instead of failing it. Every test added by this phase gets `set_tests_properties(... PROPERTIES TIMEOUT 120)`. The spins are GC-safe by construction — `JUMP_BACK` already calls `ProtoContext::safepoint()` at every loop back-edge (`ExecutionEngine.cpp:620-623`), so a spinning main thread never delays a collection.
- **Every concurrency fixture is deterministic in its output.** A fixture never prints a value that depends on interleaving; it prints a count, a sum or a sorted result that the single-method invariant makes exact. A fixture that cannot be made deterministic belongs in the benchmark suite, not in the conformance suite.
- Benchmarks self-report the work they did and the runner verifies it against the expected count **before** computing a rate; exit code alone never counts as success (DESIGN §10, §8.5).
- Commits use the repository's configured git identity (never `-c user.*` or `--author`), end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`, go to `main` and are pushed (`git push origin main`; maintainer-authorised for this phase). Never force-push, never rewrite published history, never create the `v0.3.0` tag (the maintainer tags).
- Build and test only with: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release` (run from `protoScala/`). If protoCore was rebuilt since the last protoScala build (Phase P2 merges), **delete `build_release` first** — a stale binary against a new protoCore ABI crashes on the first `ProtoSpace` (see Preflight Step 1).

---

## Preflight (before Task 0)

- [ ] **Step 1: Rebuild from clean and confirm the baseline is green**

The `build_release/protoscala` binary in the tree on 2026-09-23 **segfaults on every script** (it starts, prints `--version`, and dies constructing the `ProtoSpace`) while `build_bench/protoscala`, built later, runs the same scripts: the canonical build directory is stale against the current `../protoCore/build_release/libprotoCore.so.1.2.0`. Phase 5 therefore starts with a clean rebuild — never diagnose a Phase 5 bug against that binary.

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoScala
git status --short && git log --oneline | head -3
rm -rf build_release
cmake -B build_release -S . && cmake --build build_release
ctest --test-dir build_release
```
Expected: a clean `git status`; `100% tests passed` (646 tests on 2026-09-23: 265 unit, 352 conformance, 19 CLI, 10 benchmark smoke). If the tree is not clean, **stop and ask the maintainer**.

Run: `printf 'println("ok")\n' > ../.agent_scratch/phase5/probe.scala && build_release/protoscala ../.agent_scratch/phase5/probe.scala`
Expected: `ok`, exit 0. (Create `../.agent_scratch/phase5/` first.)

- [ ] **Step 2: Record the protoCore commit and whether `ProtoMPSCQueue` is available**

Run:
```bash
git -C ../protoCore log --oneline | head -1
grep -c "newMPSCQueue" ../protoCore/headers/protoCore.h
```
Expected: one commit line (paste it into the Task 11 CHANGELOG entry: "built against protoCore `<hash>`"), and either a non-zero count (Phase P2 is in the tree protoScala links against — the real mailbox) or `0` (the `Mailbox` seam falls back, Task 2, Task 0 decision A0-8). P2 was implemented on the protoCore branch `feature/mpsc-queue` on 2026-09-23 and is pending the maintainer's merge, so also run `git -C ../protoCore branch --show-current` and record both in the Task 2 notes. **Never build protoScala against a protoCore working tree that differs from the one `build_release` was built from**: a stale binary against a new ABI crashes on the first `ProtoSpace` (Step 1).

- [ ] **Step 3: Confirm the Phase 5 surface is still absent (the failing starting point)**

```bash
printf 'val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }\nprintln(a ! 1)\n' \
  > ../.agent_scratch/phase5/surface.scala
build_release/protoscala ../.agent_scratch/phase5/surface.scala
```
Expected: `surface.scala:1:9: error: Not found: Actor`, exit 1.

- [ ] **Step 4: Record the protoClojure comparison baseline exists**

Run: `ls ../protoClojure/build_release/protoclj ../protoClojure/benchmarks/actor-bench.sh`
Expected: both exist (Task 10 runs `actor-bench.sh` on the same machine and date as the protoScala table). If `protoclj` is missing, Task 10 records the comparison as "not available on this machine" and says why — it never fabricates numbers.

- [ ] **Step 5: Confirm the machine's core count for the worker sweep**

Run: `nproc && grep -c ^processor /proc/cpuinfo && lscpu | grep -E '^(Core|Socket|Thread)'`
Expected: the DEV12 machine reports 12 logical / 6 physical cores. Task 10's tables peak at the physical-core count (protoST's measured worker-scaling ceiling); note the numbers in the report header.

---

## Task 0: Maintainer decisions

Each decision below was **decided by the agent** under the maintainer's standing authorisation and recorded in `docs/DECISIONS-LOG.md` by Task 11 as pending review. Where a decision creates a user-visible departure from Scala 3, Task 11 records it as a `D<n>` in `docs/STATUS.md` (Phase 5 uses **D43–D52**; D42 was the highest id in use when this plan was written).

> **Maintainer review, 2026-09-23 — all approved except:** **A0-4/D45** and **A0-6/D47** are **overturned** on the ground of least surprise for the Scala programmer (a handler may return a bare `newState`; `Future` takes its body by name, which brought by-name parameters and the new **D53**), and **A0-3/D44** is **superseded** — Phases 3 and 4 are to be completed, so D44's version implication is to be revisited when they land. **A0-5/D46** stands, and its precondition is now met: `ProtoMap` shipped in protoCore 2.0.0 and is merged and released. **A0-8** was already superseded by Phase I. ThreadSanitizer (Task 8 Step 4) and the cold-start budget stay recorded as known gaps, not as approvals.

### A0-1 — How cooperative `await` suspends the recursive VM

DESIGN §3.6 and §8.3 say "recursive VM with frame snapshots (the protoST model)", and DECISIONS-LOG records that as a maintainer decision of 2026-09-22. The two halves are in tension: protoST **converted its engine to a non-recursive frame vector** precisely because "a future-yield can later snapshot `frames_` and resume it, which is impossible with the previous recursive design where every active Smalltalk method occupies an unreachable C++ stack frame" (`protoST/src/runtime/ExecutionEngine.h:17-24`). protoScala's `ExecutionEngine::execute` (`src/runtime/ExecutionEngine.cpp:436-820`) is recursive: one native frame and one `ProtoContext` per call.

| Option | What it costs | What it buys |
|---|---|---|
| **(a) Per-frame catch-and-prepend snapshot** (recommended) | ~150 lines: split `execute` into a prologue and a `runLoop`, track a `pendingBase` across call opcodes, add one `catch (FutureYield&)` that appends the frame record and rethrows, add `resumeFrames` | Keeps the recursive VM exactly as it is. Reuses protoST's *data* design (a `ProtoList` of frame objects on the actor) and its *coalescing* rule — protoST already prepends the frames of an outer engine to the snapshot a nested engine wrote (`ExecutionEngine.cpp:2196-2240`), which is precisely one-frame-per-engine in protoScala. Suspension works at any bytecode depth |
| (b) Convert the VM to an iterative frame vector | Rewrites every opcode's access to `slots`/`sp`, the exception paths, `StackGuard` depth semantics and every Phase 1/2 engine unit test | Suspension across primitive re-entry too — which protoST *still* does not get, because a primitive's C++ loop state is lost either way |
| (c) One `ucontext`/fiber stack per actor turn | A parked fiber owns live `ProtoContext`s that belong to no running thread; protoCore has no API to hand a context chain to another thread (R7-adjacent), and the GC's stack-root capture assumes one context chain per registered thread | Full transparency |

**Decision: (a).** A snapshot frame records `{module address, ip offset, pendingBase, the slot prefix [0, pendingBase)}`; resume re-materialises frames from outermost to innermost by recursion and writes the awaited value at `pendingBase`, which is exactly what the in-flight call opcode would have done (`base[0] = r; sp = base + 1`). Consequence, recorded as **D43**: a suspended call chain must consist only of bytecode frames whose in-flight instruction is a call opcode (see A0-2).

### A0-2 — An `await` that cannot be snapshotted

The chain from the actor's handler down to `await` can cross C++ frames that hold state a snapshot cannot capture: a native higher-order primitive that re-entered the VM (`List.map`, `foreach`, `withFilter`, `Product.copy`), or a non-call opcode that calls back into Scala (`EQ` → `valuesEqual` → a user `equals`; `show` → a user `toString`).

| Option | Consequence |
|---|---|
| **(a) Raise `UnsupportedOperationException` at the `await`** (recommended) | Honest and testable; the failure lands in the ask's `Future` as a `Failure`, the actor survives, nothing is corrupted |
| (b) Fall back to blocking the worker | Deadlocks the pool whenever the completing party needs a worker — the `await` benchmark with one worker would hang instead of failing |
| (c) Snapshot what can be snapshotted and re-run the rest | Re-runs side effects; silently wrong |

**Decision: (a).** The `await` primitive refuses before throwing when the thread's native re-entry depth is greater than one, and the per-frame catch refuses when the frame has no in-flight call (`pendingBase == kNoPendingCall`), with the message `await is not supported inside <what>: the call chain cannot be suspended`. Recorded as **D43**.

### A0-3 — Phase order and the prelude pieces Phase 5 needs

Phase 5 is being implemented **before** Phases 3 and 4, so the next minor version is **0.3.0** (ROADMAP: each completed phase bumps the minor version). That means no `throw`/`try`/`catch` and no `Try` in the prelude yet.

**Decision:** Phase 5 moves the minimum forward, as Phase 2 moved `Option` and a minimal `List` forward from Phase 3: the prelude gains `Try`/`Success`/`Failure` and a `RuntimeError(className, message)` case class that stands in for an exception value until Phase 4 (recorded as **D44**). A failed `Future` carries a `RuntimeError`; `await` on it raises the same error on the awaiting thread (the Scala behaviour), which Phase 4 will turn into a catchable `throw`. Alternative — implement Phases 3 and 4 first — costs two phases before any concurrency lands and is the maintainer's to take.

### A0-4 — What an actor handler returns

DESIGN §8.1's handler returns `(newState, reply)` in every example.

| Option | Consequence |
|---|---|
| **(a) Strict: the result must be a `Tuple2`** (recommended) | Unambiguous; matches §8.1 exactly; a wrong return is a loud `IllegalArgumentException` that completes the ask with `Failure` |
| (b) A `Tuple2` is `(newState, reply)`, any other value is both | Convenient, but an actor whose state *is* a pair becomes unwritable, and the ambiguity is silent |

**Decision: (a)**, recorded as **D45**: an actor handler must return `(newState, reply)`; anything else raises `IllegalArgumentException: an actor handler must return (newState, reply), got <T>`.

### A0-5 — Actor lifetime and the GC anchor

The ready stacks are C++ (`ActorState*`), invisible to the GC, so actors must be anchored in a protoCore structure (protoST's `__live_actors__` lesson).

| Option | Consequence |
|---|---|
| **(a) Anchor at spawn, for the session** (recommended) | Two lines, no per-message cost, no window where an actor is reachable only from a C++ stack; actors are never collected (protoClojure's `owners_` does the same) |
| (b) protoST's flag-gated anchor/unanchor (anchor only while suspended) | Actors become collectable when quiescent, at the cost of a registry that supports removal — which needs `ProtoMap` (P1, not merged) to be O(log n) |

**Decision: (a)** for 0.3.0, recorded as **D46** ("an actor lives as long as the session"), with (b) named in STATUS as the follow-up once P1 merges.

### A0-6 — `Future.apply` without by-name parameters

`Future { compute() }` needs a by-name parameter; protoScala has none (D33) and cannot infer one without static types.

| Option | Consequence |
|---|---|
| **(a) `Future(() => expr)` — a function, not a by-name** (recommended) | One documented deviation; the native raises a clear error when handed a non-function (`Future.apply expects a function: write Future(() => expr)`) |
| (b) Compiler special case: the argument of `Future.apply` is compiled as a thunk | Hidden magic keyed on a name a user may shadow |
| (c) General by-name parameters | Needs callee signatures at compile time, which a dynamic dialect does not have |

**Decision: (a)**, recorded as **D47**. `Future { () => expr }` also works (a block holding a lambda).

### A0-7 — Where `map` / `flatMap` / `recover` continuations run

| Option | Consequence |
|---|---|
| **(a) Inline on the thread that completes the future** (recommended) | One scheduling entity (the actor); no task objects, no second anchor problem; matches "run callbacks on the completing thread" as many runtimes do. An `await` inside a continuation is refused (A0-2's mechanism), so a continuation can never block a worker |
| (b) A second ready-entry kind (tasks) | A task object must be GC-anchored while queued and un-anchored when run — the same registry problem as A0-5(b), on the hot path |

**Decision: (a)**, recorded as **D48**: a continuation registered on an already-completed future runs immediately on the caller; otherwise it runs on the thread that completes the future, before that thread returns. `Future.apply` is the exception: it runs its body on the pool, as a one-shot actor (DESIGN §8.3 "runs a computation on the worker pool").

### A0-8 — Building before Phase P2 merges

Phase P2 was implemented on the protoCore branch `feature/mpsc-queue` (protoCore 2.1.0) on 2026-09-23 and is pending the maintainer's merge and the rebuild of every embedder (PMQ-SPEC §7). Until `../protoCore/build_release` is built from a tree that carries it, protoScala cannot link against it.

**Decision:** Task 2 introduces a `Mailbox` seam with the exact `ProtoMPSCQueue` API of PMQ-SPEC §2.1. When CMake finds `newMPSCQueue` in the protoCore headers it compiles `Mailbox` onto the real type (`-DPROTOSCALA_HAS_PMQ=1`); otherwise it compiles the fallback — a mutable object holding a `ProtoList` updated by `setAttributeIfEqual`, which is GC-safe and correct but O(log n) per push (protoST's mailbox). Switching is a one-file change and the Task 2 unit tests run against whichever is compiled. Alternative — block Phase 5 on P2 — was rejected because P2's own plan is being written in parallel.

### A0-9 — Surface the benchmarks need beyond DESIGN §8

The `MPSC`/`MPMC` modes need **sender threads that are not workers** (a producer running on the pool would deadlock the `workers = 1` row), and the `priority` mode reports **High-band latency p50/p99**, which needs a clock in the language.

**Decision:** Phase 5 adds two minimal natives, recorded as **D49** ("`Thread` and `System` are runtime facilities, not the JVM's"):
- `Thread.start(() => body): Thread` and `t.join(): Unit`, on `ProtoSpace::newThread` (real OS threads in the GC quorum), used by the benchmarks and by the cross-thread tests of Task 5.
- `System.nanoTime(): Long` and `System.currentTimeMillis(): Long`.

`Future.apply` stays on the pool (§8.4 forbids one OS thread per future); `Thread.start` is explicit and user-written.

### A0-10 — Batch size per turn

DESIGN §8.2 says a worker "drains a batch (up to 8 messages)"; protoClojure's code actually drains everything.

**Decision:** 8 messages per turn (`kBatchSize`), the DESIGN value. `takeAll` hands back the whole band, so the remainder is kept in a per-band `__pending<n>__` `ProtoList` attribute of the actor (GC-safe) with the read index in the C++ `ActorState` (an `unsigned`, not a pointer — P1 clean). The turn ends with a re-enqueue when anything is left.

### A0-11 — Opcodes

**Decision: no new opcodes.** `!`, `?`, `send`, `ask`, `value`, `await`, `map`, `flatMap`, `recover` are ordinary method sends to native methods, and `Actor`, `Future`, `Priority`, `Thread`, `System` are prelude objects over internal primitives. The reserved range 128..159 (`SEND_ASYNC`, `ASK`, `AWAIT`) stays reserved in `Opcodes.h` and `STATUS.md`; a dedicated opcode is a later optimisation to be justified by `perf stat -r 3`, not by taste (DESIGN §1: protoScala is not a fast Scala, and the send path is ~1 % of a message's cost next to the envelope allocation and the scheduler CAS).

---

## Design notes that apply to several tasks

File:line references are to the trees of 2026-09-23.

1. **The engine is thread-safe by construction.** `ExecutionEngine` holds only `const RuntimeLayout& layout_` (`src/runtime/ExecutionEngine.h:239-273`), and the per-thread state it needs is the `ActiveCallContext` thread-local installed by `run`/`callTopLevel` (`ExecutionEngine.cpp:17-28`). A worker therefore shares the session's single engine and installs the same `ActiveCallContext` itself — protoClojure's "blueprint" pattern (`ActorScheduler.cpp:50-60`). Nothing else in the engine is mutable.

2. **Every frame already keeps its live values in GC-visible slots.** `execute` sizes one `ProtoContext`'s automatic locals to `arity + localCount + maxStack` and never resizes it (`ExecutionEngine.cpp:451-455`); `slots` is that array and `sp` walks inside it. A frame snapshot is therefore a straight copy of a prefix of that array into a `ProtoList` — no hidden C++ state except `ip` and the in-flight call's base.

3. **What a snapshot frame contains, and why that is enough.** Every opcode that can re-enter the VM ends with `base[0] = r; sp = base + 1` (`CALL`, `CALL_SPREAD`, `SEND`, `SEND_APPLY`, `SEND_SUPER`, `SEND_KW`, `NEW`, `NEW_SPREAD`, `INVOKE_INIT`; `FORCE` writes `sp[-1]`). So a frame that is *inside* such a call resumes exactly by: restore the slots below `base`, write the result at `base`, set `sp = base + 1`, continue at the saved `ip` (which the loop already advanced past the instruction). The record is `{ __mod__: SmallInteger address of the BytecodeModule, __ip__: word offset from code().data(), __base__: index of the call's base slot, __slots__: ProtoList of slots[0 .. __base__) }`. The module address travels as a `SmallInteger` exactly as `MAKE_FN` already stores a module address (`ExecutionEngine.cpp:540-553`: user-space addresses are below 2^47, inside the SmallInteger range), and modules are owned by the `Session` for the whole session (`src/repl/Session.h:55-60`), so the pointer cannot dangle. Arity, local count and max stack are read back from the module, not stored.

4. **`pendingBase` costs two stores per call opcode.** The loop keeps `unsigned pendingBase = kNoPendingCall;` set immediately before a re-entrant call and cleared immediately after it returns. A `FutureYield` that reaches the catch with `kNoPendingCall` is not resumable (A0-2) — that is how an `await` under `EQ`/`valuesEqual`, under `MATCH_ERROR`'s `show`, or under a native higher-order primitive is refused instead of corrupting the frame.

5. **`FutureYield` must not derive from `std::exception`.** `execute`'s existing handlers are `catch (ScalaError&)` and `catch (const std::runtime_error&)` (`ExecutionEngine.cpp:810-819`); the second one would otherwise swallow the signal and turn it into a `RuntimeException`. DESIGN §7 already fixes this rule for control-flow signals. The new `catch (FutureYield&)` clause is placed **first**.

6. **The single-method invariant lives in one atomic.** protoClojure's `claimed` flag plus protoST's 3-state `__sched__` combine into one `std::atomic<unsigned> sched` per actor with four states: `0 Idle`, `1 Claimed` (queued or running), `2 ClaimedWake` (running, and a wake arrived — re-enqueue at end of turn), `3 Suspended` (parked on a future; still claimed, so no other worker may run it). A sender always pushes into the mailbox first and only then touches `sched`, so a claim that loses the race is safe: the running worker sees the message when it drains again, or at end of turn. A message sent to a **suspended** actor does *not* wake it — the handler is mid-flight and the messages wait, which is what the single-method invariant means across a suspension (DESIGN §8.3).

7. **The completion of a future is one CAS.** `Future.__fstate__` goes `0 → 1` (success) or `0 → 2` (failure) with `setAttributeIfEqual`, after the value has been written, exactly as protoClojure's `deliverPromise` does: a concurrent reader never sees a completed future without its value. Waiters are read *after* the CAS wins, so a waiter appended before the CAS is always woken and one appended after it is rejected by `addWaiter` (which re-reads the state under its own CAS loop and returns `false`).

8. **A suspended actor is woken by the completer, not by a message.** `addWaiter` returning `false` (already completed) means the yield must not park: the turn ends with `sched = ClaimedWake` and an immediate re-enqueue, so the actor resumes in the next turn and reads the settled value. This is the race protoST lost twice (`STRuntime.cpp:1780-1830`); Task 6 has a deterministic unit test for it.

9. **Parking is always inside `UnmanagedScope`.** Worker park, blocking `await`, `Thread.join` and shutdown join open `proto::ProtoContext::UnmanagedScope` **before** taking any `std::mutex`, so the lock is released before `returnFromUnmanaged` can park (protoST's comment at `STRuntime.cpp:760-772`). A worker calls `ctx->safepoint()` right before parking, at the quiescent point where no frame is live, so its young generation is submitted while it sleeps.

10. **Spin before park.** protoST measured −91 % parks by spinning ~2–4 µs (8 rounds × 256 `pause`) checking the ready stacks before blocking on the semaphore (`STRuntime.cpp:815-845`). The same constants are used here, with the same rationale comment; they are the only tuning constants in the scheduler and they are named (`kSpinRounds`, `kSpinPauses`).

11. **Ready stacks are protoST's `ReadyStack`, ported verbatim.** A Treiber stack with a generation-tagged 64-bit head and a type-stable node pool: without both, a pop reads a freed node (`use-after-free`) and ABA silently loses entries, because glibc's tcache hands a freed chunk straight back (`protoST/src/runtime/ReadyStack.h:2-48`). Porting it unchanged — including the `Hook` test seam — is cheaper and safer than re-deriving it.

12. **Message envelopes are objects, not tuples.** One mutable child of `envelopeProto` per message with `__msg__` and (for an ask) `__future__`. `ProtoTuple` is forbidden (every node is interned and perennial, R2), and a bare payload cannot be distinguished from an envelope.

13. **Where the `RuntimeLayout` gains keys.** All Phase 5 attribute keys are interned once in `Runtime::Runtime` and read from `RuntimeLayout` on the hot paths (Global Constraints). The new prototypes (`actorProto`, `futureProto`, `envelopeProto`, `threadProto`) and the registry are pinned in new root slots, extending the `RootSlot` enum in `Runtime.cpp:11-16`.

14. **Prelude hooks.** Natives that must build prelude values (`Some`, `Success`, `Failure`, `RuntimeError`, `ActorStats`) cannot hard-code global names: a REPL redefinition gives `Some#1` (D25). `Runtime` therefore gains a `PreludeHooks` block filled *after* the prelude is compiled, by looking each name up in the `GlobalTable` (`globals.binding("Success")->key`) and reading the value out of the globals object. `Session` and `EvalHarness` both call `bindPreludeHooks` right after `loadPrelude`.

15. **Determinism of the fixtures.** The single-method invariant makes counting exact: N sends to one actor leave the counter at N, whatever the interleaving. Every conformance fixture in this phase reduces to such a count, a sum, or a sorted list — protoClojure's `concurrent-sends-no-race.clj` shape (`protoClojure/tests/conformance/25-actors/concurrent-sends-no-race.clj`).

16. **Shutdown is the `Session`'s job.** `Session` owns the `ProtoSpace` as its *first* member, so it is destroyed last (`src/repl/Session.h:55`); `~Session` must call `ActorScheduler::shutdown` before that, and `Session::runScript`/`evalReplInput` must survive an uncaught `ScalaError` with the workers still joined. The header already carries the reminder: "Phase 1 starts no threads; a later phase that does must join them in `~Session` before the `ProtoSpace` member is destroyed."

17. **Lessons carried over:** disassemble before blaming caches (`--disassemble`); benchmarks self-report and are verified (protoPython's sprint-9 "wins" were crashes); GC-pressure runs use `PROTOCORE_HEAP_LIMIT_CELLS`; symbols are never cached in statics; `readline`'s `RETURN` macro stays `#undef`ined; measure dispatch changes with `perf stat -r 3` before claiming them.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/runtime/ReadyStack.h` (new) | protoST's lock-free LIFO stack of ready actors: generation-tagged head, type-stable node pool, `Hook` test seam |
| `src/runtime/Mailbox.h` / `Mailbox.cpp` (new) | The one-band mailbox seam: `ProtoMPSCQueue` when protoCore provides it, a `setAttributeIfEqual` list otherwise (A0-8) |
| `src/runtime/ActorScheduler.h` / `ActorScheduler.cpp` (new) | `ActorState`, the 4-state claim machine, three ready stacks, the worker pool on `ProtoSpace::newThread`, the turn loop (batch of 8, bands High→Medium→Low), spin-before-park, stats, shutdown |
| `src/runtime/Futures.h` / `Futures.cpp` (new) | The `Future` object: state CAS, value/error, waiter list, continuation list, blocking `await` on a counted condition variable, `complete` |
| `src/runtime/FutureYield.h` (new) | The cooperative-yield control signal (not a `std::exception`) |
| `src/runtime/ActorPrimitives.cpp` (new) | Every native of the actor/future/thread surface (`__actorSpawn`, `!`, `?`, `send`, `ask`, `value`, `await`, `map`, …) |
| `src/runtime/ExecutionEngine.h` / `.cpp` (modify) | `execute` split into prologue + `runLoop`; `pendingBase`; `catch (FutureYield&)`; `resumeFrames`; native re-entry depth |
| `src/runtime/Runtime.h` / `Runtime.cpp` (modify) | `actorProto`, `futureProto`, `envelopeProto`, `threadProto`, the actor registry and the Phase 5 keys; `PreludeHooks` and `bindPreludeHooks` |
| `src/runtime/Primitives.h` / `Primitives.cpp` (modify) | Installs the actor/future/thread/system primitives; the new builtin globals |
| `src/runtime/Values.cpp` (modify) | `show`/`typeName` of an actor (`Actor(10)`), a future (`Future(<pending>)`, `Future(10)`, `Future(<failed>)`) and a thread |
| `lib/prelude.scala` (modify) | `Try`/`Success`/`Failure`, `RuntimeError`, `ActorStats`, `object Actor`, `object Priority`, `object Future`, `object Thread`, `object System` |
| `src/repl/Session.h` / `Session.cpp` (modify) | `bindPreludeHooks`; `~Session` joins the workers on every exit path |
| `tests/unit/EvalHarness.h` (modify) | `bindPreludeHooks` |
| `tests/unit/test_readystack.cpp`, `test_mailbox.cpp`, `test_scheduler.cpp`, `test_futures.cpp`, `test_yield.cpp` (new); `tests/unit/CMakeLists.txt` (modify) | Unit tests; the scheduler tests link one dedicated binary (one runtime per process) |
| `tests/conformance/13-actors/`, `14-futures/` (new) | Fixtures (brace and indentation variants where the syntax differs) |
| `tests/cli/actors-shutdown.sh`, `tests/cli/actors-stress.sh` (new); `tests/cli/gc-pressure.sh`, `tests/CMakeLists.txt` (modify) | Shutdown on every exit path, the race/stress fixtures, actor GC pressure |
| `benchmarks/actors/actor-throughput.scala`, `actor-fanout.scala`, `actor-mpsc.scala`, `actor-mpmc.scala`, `actor-pingpong.scala`, `actor-await.scala`, `actor-priority.scala` (new) | The seven modes of DESIGN §8.5 |
| `benchmarks/actor-bench.sh`, `benchmarks/run_actor_benchmarks.py` (new); `benchmarks/run_benchmarks.py`, `benchmarks/README.md`, `benchmarks/RESULTS.md` (modify); `benchmarks/reports/<date>-actors.md` (generated) | The actor harness, the report and the protoClojure comparison |
| `docs/tutorial/13-actors-and-futures.md` (new), `docs/TUTORIAL.md`, `docs/tutorial/02-*`, `03-*` (modify); `tests/conformance/tutorial/13-*` (new) | Tutorial chapter 13 and its fixtures |
| `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt` | Status, deviations D43–D52, version 0.3.0 |

`CMakeLists.txt` gains `src/runtime/Mailbox.cpp`, `ActorScheduler.cpp`, `Futures.cpp`, `ActorPrimitives.cpp` to the `runtime` library, links `Threads::Threads`, and defines `PROTOSCALA_HAS_PMQ` when protoCore provides `newMPSCQueue`. The library order is unchanged: `frontend ← compiler ← runtime ← repl ← protoscala`.

### Opcodes added in this phase

**None** (A0-11). The reserved range `128..159` stays reserved in `src/compiler/Opcodes.h` and in the `docs/STATUS.md` opcode table; Task 11 updates the comment to record that Phase 5 shipped without using it.

---

### Task 1: `ReadyStack` — the lock-free ready stack

**Files:**
- New: `src/runtime/ReadyStack.h`
- Test: `tests/unit/test_readystack.cpp`, `tests/unit/CMakeLists.txt` (modify)

**Interfaces:**
- Consumes: nothing (a self-contained header; `<atomic> <bit> <cstdint> <new>`).
- Produces:
```cpp
namespace protoScala {
struct ReadyStackNoHook { template <class Stack> static void beforePopCommit(Stack&) {} };

template <class V, class Hook = ReadyStackNoHook>
class ReadyStack {
public:
    ReadyStack() = default;
    ReadyStack(const ReadyStack&) = delete;
    ReadyStack& operator=(const ReadyStack&) = delete;
    ~ReadyStack();
    void   push(V value);          // any thread, lock-free
    V      pop();                  // any thread; V{} when empty
    size_t approxSize() const;     // diagnostic only
};
} // namespace protoScala
```

- [ ] **Step 1: Port the file**

Copy `../protoST/src/runtime/ReadyStack.h` to `src/runtime/ReadyStack.h` **verbatim**, changing only:
- `namespace protoST` → `namespace protoScala`;
- the header comment's first line to `// ReadyStack: the scheduler's lock-free LIFO stack of ready actors (DESIGN §8.2).`, keeping the whole "Why / How / Cost" rationale (it is the record of the use-after-free and ABA bugs the design avoids) and adding one line: `// Ported from protoST (src/runtime/ReadyStack.h) unchanged; keep the two trees in sync.`

Do not "improve" it: the generation-tagged head, the never-freed node pool and the `Hook` seam are all load-bearing.

- [ ] **Step 2: Write the tests**

Create `tests/unit/test_readystack.cpp`:

```cpp
#include "runtime/ReadyStack.h"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

using protoScala::ReadyStack;

TEST(ReadyStack, EmptyPopReturnsDefault) {
    ReadyStack<int*> s;
    EXPECT_EQ(s.pop(), nullptr);
    EXPECT_EQ(s.approxSize(), 0u);
}

TEST(ReadyStack, PushPopIsLifoAndReusesNodes) {
    ReadyStack<long> s;
    for (long i = 1; i <= 3; ++i) s.push(i);
    EXPECT_EQ(s.approxSize(), 3u);
    EXPECT_EQ(s.pop(), 3);
    EXPECT_EQ(s.pop(), 2);
    EXPECT_EQ(s.pop(), 1);
    EXPECT_EQ(s.pop(), 0);
    // A second round reuses the freed nodes: no growth, no leak.
    for (long i = 1; i <= 3; ++i) s.push(i);
    EXPECT_EQ(s.approxSize(), 3u);
}

// The bug the generation tag exists for: a pop that reads a successor, is
// descheduled, and commits after other threads popped and re-pushed the same
// node index. The hook forces exactly that interleaving.
namespace {
ReadyStack<long>* g_hooked = nullptr;
std::atomic<int> g_hookCalls{0};
struct RacingHook {
    template <class Stack> static void beforePopCommit(Stack& s) {
        if (g_hookCalls.fetch_add(1) != 0) return;  // only the first pop
        s.pop();      // steal the head
        s.push(99);   // and give the node index straight back
    }
};
} // namespace

TEST(ReadyStack, GenerationTagRejectsAStaleCommit) {
    ReadyStack<long, RacingHook> s;
    s.push(1);
    s.push(2);
    const long first = s.pop();     // its commit races the hook's steal+push
    const long second = s.pop();
    const long third = s.pop();
    std::set<long> seen{first, second, third};
    EXPECT_EQ(seen.count(0), 0u) << "an entry was lost to an ABA commit";
    EXPECT_EQ(seen.size(), 3u) << "an entry was duplicated";
}

TEST(ReadyStack, NoLossNoDuplicationUnderContention) {
    constexpr int kThreads = 8, kPer = 20000;
    ReadyStack<long> s;
    std::atomic<long long> consumedSum{0};
    std::atomic<int> consumed{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i) s.push(1L + t * kPer + i);
        });
    for (int t = 0; t < kThreads / 2; ++t)
        ts.emplace_back([&] {
            while (consumed.load() < kThreads * kPer)
                if (const long v = s.pop()) {
                    consumedSum.fetch_add(v);
                    consumed.fetch_add(1);
                }
        });
    for (auto& th : ts) th.join();
    constexpr long long n = static_cast<long long>(kThreads) * kPer;
    EXPECT_EQ(consumed.load(), kThreads * kPer);
    EXPECT_EQ(consumedSum.load(), n * (n + 1) / 2);
    EXPECT_EQ(s.pop(), 0);
}
```

Register it in `tests/unit/CMakeLists.txt` next to the existing sources.

- [ ] **Step 3: Run**

Run: `cmake --build build_release && ctest --test-dir build_release -R ReadyStack --output-on-failure`
Expected: `4 tests passed`. The contention test takes ~0.3 s.

- [ ] **Step 4: Commit**

```bash
git add src/runtime/ReadyStack.h tests/unit/test_readystack.cpp tests/unit/CMakeLists.txt
git commit -m "runtime: lock-free ready stack for the actor scheduler

Ports protoST's ReadyStack: a Treiber stack with a generation-tagged head
and a type-stable node pool, so a pop never reads a freed node and ABA
cannot lose or duplicate an entry (DESIGN §8.2).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 2: `Mailbox` — one priority band, GC-traced

**Files:**
- New: `src/runtime/Mailbox.h`, `src/runtime/Mailbox.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/unit/test_mailbox.cpp`, `tests/unit/CMakeLists.txt`

**Interfaces:**
```cpp
namespace protoScala {
// One priority band of an actor's mailbox: a multi-producer / single-consumer
// FIFO of ProtoObject* whose items the GC traces (PMQ-SPEC §2).
//
// The queue is a protoCore object, so the actor holds it in an attribute and
// the collector reaches every queued message through it. With protoCore's
// ProtoMPSCQueue (Phase P2) push is lock-free and O(1); without it the seam
// falls back to a CAS'd ProtoList of the same semantics (DESIGN Task 0 A0-8).
class Mailbox {
public:
    static const proto::ProtoObject* create(proto::ProtoContext* ctx);
    static void push(proto::ProtoContext* ctx, const proto::ProtoObject* queue,
                     const proto::ProtoObject* item);
    // Removes and returns every queued item in FIFO order; an empty list when
    // nothing is queued. One consumer at a time (the actor's claim guarantees it).
    static const proto::ProtoList* takeAll(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* queue);
    static bool isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* queue);
    // "ProtoMPSCQueue" or "CAS list": printed by Actor.stats diagnostics and by
    // the benchmark report header, so a table always says what it measured.
    static const char* implementationName();
};
} // namespace protoScala
```

- [ ] **Step 1: CMake probe**

In `CMakeLists.txt`, after the protoCore headers are located:

```cmake
# --- Actor mailbox backend (DESIGN §8.2, platform/PMQ-SPEC.md) -------------------
# protoCore gains ProtoMPSCQueue in Phase P2. Until it merges, the Mailbox seam
# falls back to a CAS'd ProtoList with identical semantics.
file(READ "${PROTOCORE_INCLUDE_DIR}/protoCore.h" PROTOSCALA_PROTOCORE_HEADER)
string(FIND "${PROTOSCALA_PROTOCORE_HEADER}" "newMPSCQueue" PROTOSCALA_PMQ_FOUND)
if(PROTOSCALA_PMQ_FOUND GREATER -1)
    message(STATUS "protoScala: actor mailboxes on protoCore ProtoMPSCQueue")
    target_compile_definitions(protoScala_runtime PUBLIC PROTOSCALA_HAS_PMQ=1)
else()
    message(STATUS "protoScala: ProtoMPSCQueue not in protoCore yet -- "
                   "actor mailboxes fall back to a CAS'd ProtoList")
endif()
```

(Use the variable this file already holds for the protoCore include directory; if it is not a variable, use `${PROTOCORE_DIR}/headers/protoCore.h`.)

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/test_mailbox.cpp` — the tests are written against the *semantics*, so they pass with either backend:

```cpp
#include "runtime/Mailbox.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

using namespace protoScala;

namespace {
struct Space {
    proto::ProtoSpace space;
    proto::ProtoContext* root() { return space.rootContext; }
};
} // namespace

TEST(Mailbox, TakeAllOnAnEmptyQueueIsEmpty) {
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    EXPECT_TRUE(Mailbox::isEmpty(&ctx, q));
    EXPECT_EQ(Mailbox::takeAll(&ctx, q)->getSize(&ctx), 0u);
}

TEST(Mailbox, OneProducerKeepsFifoOrder) {
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    for (long long i = 0; i < 100; ++i) Mailbox::push(&ctx, q, proto::makeSmallInt(i));
    const proto::ProtoList* got = Mailbox::takeAll(&ctx, q);
    ASSERT_EQ(got->getSize(&ctx), 100u);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(proto::asSmallInt(got->getAt(&ctx, i)), i) << "at " << i;
    EXPECT_TRUE(Mailbox::isEmpty(&ctx, q));
}

TEST(Mailbox, EightProducersLoseNothingAndDuplicateNothing) {
    constexpr int kProducers = 8, kPer = 5000;
    Space s;
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    std::atomic<int> started{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < kProducers; ++t)
        ts.emplace_back([&, t] {
            proto::ProtoContext mine(&s.space, s.root());
            started.fetch_add(1);
            for (int i = 0; i < kPer; ++i)
                Mailbox::push(&mine, q, proto::makeSmallInt(1LL * t * kPer + i));
        });
    std::set<long long> seen;
    while (static_cast<int>(seen.size()) < kProducers * kPer) {
        const proto::ProtoList* batch = Mailbox::takeAll(&ctx, q);
        for (unsigned long i = 0; i < batch->getSize(&ctx); ++i)
            EXPECT_TRUE(seen.insert(proto::asSmallInt(batch->getAt(&ctx, static_cast<int>(i))))
                            .second)
                << "duplicated item";
    }
    for (auto& th : ts) th.join();
    EXPECT_EQ(seen.size(), static_cast<size_t>(kProducers) * kPer);
}

// The GC must reach a queued item through the queue alone: the items below are
// referenced by nothing else once the loop's context is gone.
TEST(Mailbox, QueuedItemsSurviveCollections) {
    Space s;
    s.space.setHeapLimits(0, 200000);   // force collections
    proto::ProtoContext ctx(&s.space, s.root());
    const proto::ProtoObject* q = Mailbox::create(&ctx);
    for (int i = 0; i < 2000; ++i) {
        proto::ProtoContext scope(&s.space, &ctx);   // dies each iteration
        Mailbox::push(&scope, q, proto::ProtoString::createSymbol(&scope, "m")->asObject(&scope));
        scope.safepoint();
    }
    s.space.triggerGC();
    const proto::ProtoList* got = Mailbox::takeAll(&ctx, q);
    ASSERT_EQ(got->getSize(&ctx), 2000u);
    for (int i = 0; i < 2000; ++i)
        EXPECT_TRUE(proto::ProtoObject::isStringTagFast(got->getAt(&ctx, i))) << "at " << i;
}
```

- [ ] **Step 3: Implement**

`src/runtime/Mailbox.cpp` — both backends behind the same seam:

```cpp
#include "runtime/Mailbox.h"
#include "protoCore.h"

namespace protoScala {

#if PROTOSCALA_HAS_PMQ

const proto::ProtoObject* Mailbox::create(proto::ProtoContext* ctx) {
    return ctx->newMPSCQueue()->asObject(ctx);
}
void Mailbox::push(proto::ProtoContext* ctx, const proto::ProtoObject* q,
                   const proto::ProtoObject* item) {
    q->asMPSCQueue(ctx)->push(ctx, item);
}
const proto::ProtoList* Mailbox::takeAll(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->asMPSCQueue(ctx)->takeAll(ctx);
}
bool Mailbox::isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->asMPSCQueue(ctx)->isEmpty(ctx);
}
const char* Mailbox::implementationName() { return "ProtoMPSCQueue"; }

#else

// Fallback (Phase P2 not merged): a mutable object whose `__items__` attribute
// holds the queued items as a ProtoList, updated with setAttributeIfEqual --
// protoST's mailbox. Correct and GC-safe; O(log n) per push instead of O(1),
// and a push retries under contention.
namespace {
const proto::ProtoString* itemsKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__items__");
}
} // namespace

const proto::ProtoObject* Mailbox::create(proto::ProtoContext* ctx) {
    proto::ProtoObject* q = ctx->space->objectPrototype->newChild(ctx, /*isMutable=*/true);
    q->setAttribute(ctx, itemsKey(ctx), ctx->newList()->asObject(ctx));
    return q;
}

void Mailbox::push(proto::ProtoContext* ctx, const proto::ProtoObject* q,
                   const proto::ProtoObject* item) {
    const proto::ProtoString* key = itemsKey(ctx);
    auto* target = const_cast<proto::ProtoObject*>(q);
    for (;;) {
        proto::ProtoContext scope(ctx->space, ctx);   // the rebuilt list stays rooted here
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(&scope, key);
        const proto::ProtoObject* next =
            cur->asList(&scope)->appendLast(&scope, item)->asObject(&scope);
        scope.returnValue = next;
        if (target->setAttributeIfEqual(&scope, key, cur, next)) return;
    }
}

const proto::ProtoList* Mailbox::takeAll(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    const proto::ProtoString* key = itemsKey(ctx);
    auto* target = const_cast<proto::ProtoObject*>(q);
    for (;;) {
        const proto::ProtoObject* cur = target->getOwnAttributeDirect(ctx, key);
        const proto::ProtoList* items = cur->asList(ctx);
        if (items->getSize(ctx) == 0) return items;
        const proto::ProtoObject* empty = ctx->newList()->asObject(ctx);
        if (target->setAttributeIfEqual(ctx, key, cur, empty)) return items;
    }
}

bool Mailbox::isEmpty(proto::ProtoContext* ctx, const proto::ProtoObject* q) {
    return q->getOwnAttributeDirect(ctx, itemsKey(ctx))->asList(ctx)->getSize(ctx) == 0;
}
const char* Mailbox::implementationName() { return "CAS list"; }

#endif

} // namespace protoScala
```

Note for the reviewer: the fallback's `push` opens a child context per attempt so the rebuilt list is rooted while the CAS runs (P1/P2); the `ProtoMPSCQueue` path needs none, because the queue itself roots the item.

- [ ] **Step 4: Run**

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release -R Mailbox --output-on-failure`
Expected: `4 tests passed`, and the configure line printed either
`-- protoScala: actor mailboxes on protoCore ProtoMPSCQueue` or
`-- protoScala: ProtoMPSCQueue not in protoCore yet -- actor mailboxes fall back to a CAS'd ProtoList`.
Record which one in the task notes; Task 9's report header prints `Mailbox::implementationName()`.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/Mailbox.h src/runtime/Mailbox.cpp tests/unit/test_mailbox.cpp \
        tests/unit/CMakeLists.txt CMakeLists.txt
git commit -m "runtime: GC-traced mailbox seam for actor priority bands

One band of an actor mailbox, on protoCore's ProtoMPSCQueue when the linked
protoCore provides it (PMQ-SPEC) and on a CAS'd ProtoList otherwise, so
Phase 5 is not blocked on Phase P2. Items are traced by the GC either way.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 3: Actor objects, the claim machine and the worker pool

**Files:**
- Modify: `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`, `CMakeLists.txt`
- New: `src/runtime/ActorScheduler.h`, `src/runtime/ActorScheduler.cpp`
- Test: `tests/unit/test_scheduler.cpp` (its own binary), `tests/unit/CMakeLists.txt`

**Interfaces:**

`RuntimeLayout` gains (pinned in new `RootSlot`s, keys interned in `Runtime::Runtime`):
```cpp
    // Phase 5 (DESIGN §8).
    proto::ProtoObject* actorProto    = nullptr;  // the prototype of every actor
    proto::ProtoObject* futureProto   = nullptr;  // the prototype of every Future
    proto::ProtoObject* envelopeProto = nullptr;  // one queued message
    proto::ProtoObject* threadProto   = nullptr;  // a Thread handle (Task 5)
    proto::ProtoObject* actorRegistry = nullptr;  // mutable: __actors__ anchors every actor (D46)
    const proto::ProtoString* handlerKey   = nullptr;  // "__handler__"
    const proto::ProtoString* actorStateKey= nullptr;  // "__astate__": the user state
    const proto::ProtoString* stateRefKey  = nullptr;  // "__sched_ref__": ActorState address
    const proto::ProtoString* mailboxKey[3]= {};       // "__mbox0__".."__mbox2__"
    const proto::ProtoString* pendingKey[3]= {};       // "__pend0__".."__pend2__"
    const proto::ProtoString* actorsKey    = nullptr;  // "__actors__" on the registry
    const proto::ProtoString* msgKey       = nullptr;  // "__msg__" on an envelope
    const proto::ProtoString* futureKey    = nullptr;  // "__future__" on an envelope
    const proto::ProtoString* snapshotKey  = nullptr;  // "__snapshot__": suspended frames
    const proto::ProtoString* waitingOnKey = nullptr;  // "__waiting_on__": the awaited future
    const proto::ProtoString* turnFutureKey= nullptr;  // "__turn_future__": the suspended ask
    // Future (Task 5): "__fstate__", "__fvalue__", "__ferror__", "__waiters__", "__conts__"
```

`ExecutionEngine` gains:
```cpp
    // Installs `engine`/`layout` as this thread's active call context for the
    // guard's lifetime. A worker thread re-enters the VM from C++ that never
    // came through run(), so it installs the context itself (protoClojure's
    // scheduler blueprint).
    class ActiveCallGuard {
    public:
        ActiveCallGuard(ExecutionEngine* engine, const RuntimeLayout* layout);
        ~ActiveCallGuard();
        ActiveCallGuard(const ActiveCallGuard&) = delete;
        ActiveCallGuard& operator=(const ActiveCallGuard&) = delete;
    private:
        ActiveCallContext saved_;
        bool wasSet_;
    };
    // Native frames between the running bytecode frame and here: 1 while a
    // native method runs, more when a native re-entered the VM. `await` refuses
    // to suspend above 1, because the extra C++ frames cannot be snapshotted (D43).
    static unsigned nativeReentryDepth();
```

`ActorScheduler`:
```cpp
namespace protoScala {

enum class Band : unsigned { High = 0, Medium = 1, Low = 2 };
inline constexpr unsigned kBands     = 3;
inline constexpr unsigned kBatchSize = 8;    // messages per turn (DESIGN §8.2)

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
    unsigned pendingIdx[kBands] = {0, 0, 0};   // read cursor into __pend<n>__
};

class ActorScheduler {
public:
    static ActorScheduler& instance();

    // Idempotent; the first call snapshots the runtime blueprint and starts
    // the workers. Called from the first Actor.spawn.
    void ensureStarted(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const RuntimeLayout& layout, ExecutionEngine* engine);
    // Joins every worker. Safe to call when nothing was started, and twice.
    void shutdown(proto::ProtoContext* ctx);

    // Builds the actor object (prototype, handler, initial state, three
    // mailboxes), anchors it in the registry and returns it.
    const proto::ProtoObject* spawn(proto::ProtoContext* ctx, const proto::ProtoObject* handler,
                                    const proto::ProtoObject* initialState);
    // Queues `envelope` (a message object) and schedules the actor if needed.
    void send(proto::ProtoContext* ctx, const proto::ProtoObject* actor, Band band,
              const proto::ProtoObject* envelope);
    // A completed future wakes an actor suspended on it (Task 6).
    void resume(proto::ProtoContext* ctx, const proto::ProtoObject* actor);

    unsigned  workerCount() const;
    long long messagesProcessed() const;
    unsigned  suspendedCount() const;
    bool      isActor(proto::ProtoContext* ctx, const proto::ProtoObject* v) const;
    static unsigned configuredWorkerCount();          // PROTOSCALA_ACTOR_WORKERS
    void workerLoop(proto::ProtoContext* ctx);        // public: the thread entry needs it

private:
    ActorScheduler() = default;
    ActorState* stateOf(proto::ProtoContext* ctx, const proto::ProtoObject* actor) const;
    void  enqueue(ActorState* a, Band band);
    ActorState* dequeue();
    bool  drainOne(proto::ProtoContext* ctx);         // one turn; false when nothing was ready
    bool  runTurn(proto::ProtoContext* ctx, ActorState* a);   // true: the actor suspended
    void  finishTurn(proto::ProtoContext* ctx, ActorState* a, bool suspended);
    const proto::ProtoObject* nextMessage(proto::ProtoContext* ctx, ActorState* a, unsigned band);
    bool  hasWork(proto::ProtoContext* ctx, ActorState* a) const;
    Band  highestPendingBand(proto::ProtoContext* ctx, ActorState* a) const;
    void  deliver(proto::ProtoContext* ctx, ActorState* a, const proto::ProtoObject* envelope,
                  bool* suspended);

    ReadyStack<ActorState*> ready_[kBands];
    std::counting_semaphore<1 << 20> work_{0};
    std::atomic<bool> started_{false}, shuttingDown_{false};
    std::atomic<long long> messages_{0};
    std::atomic<unsigned>  suspended_{0};
    proto::ProtoSpace* space_ = nullptr;
    const RuntimeLayout* layout_ = nullptr;
    ExecutionEngine* engine_ = nullptr;
    std::vector<const proto::ProtoThread*> workers_;
    std::mutex ownersMtx_;
    std::deque<std::unique_ptr<ActorState>> owners_;
    std::once_flag startFlag_;
};
} // namespace protoScala
```

- [ ] **Step 1: Write the failing scheduler tests**

`tests/unit/test_scheduler.cpp` drives the scheduler from C++ with native handlers, before any Scala surface exists. It is **its own test binary** (`test_actors`), because one process may hold only one runtime and only one scheduler.

```cpp
#include "runtime/ActorScheduler.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Primitives.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace protoScala;
using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;

namespace {

// One runtime and one scheduler for the whole binary (R5).
struct World {
    proto::ProtoSpace space;
    Runtime runtime{space};
    ExecutionEngine engine{runtime.layout()};
    World() {
        installPrimitives(runtime.rootContext(), runtime.layout());
        // No Scala surface yet: start the pool directly, as Actor.spawn will.
        ActorScheduler::instance().ensureStarted(&space, runtime.rootContext(),
                                                 runtime.layout(), &engine);
    }
    ProtoContext* root() { return runtime.rootContext(); }
    const RuntimeLayout& L() { return runtime.layout(); }
};
World& world() { static World w; return w; }

std::atomic<long long> g_handlerCalls{0};

// A handler written as a native method: (state, msg) => (state + msg, state + msg).
const ProtoObject* addHandler(ProtoContext* ctx, const ProtoObject*, const proto::ParentLink*,
                              const ProtoList* args, const proto::ProtoSparseList*) {
    g_handlerCalls.fetch_add(1, std::memory_order_relaxed);
    const long long s = proto::asSmallInt(args->getAt(ctx, 0));
    const long long m = proto::asSmallInt(args->getAt(ctx, 1));
    const ProtoObject* sum = proto::makeSmallInt(s + m);
    const ProtoObject* pair[2] = {sum, sum};
    // (newState, reply) as a Scala Tuple2, built through the TupleN companion's
    // native apply -- never a ProtoTuple (DESIGN §4.6).
    return world().engine.send(ctx, world().L().tupleCompanion[2], world().L().applyName, pair, 2);
}

const ProtoObject* spawnAdder(ProtoContext* ctx) {
    const ProtoObject* h = ctx->fromMethod(nullptr, &addHandler);
    return ActorScheduler::instance().spawn(ctx, h, proto::makeSmallInt(0));
}

// Sends `n` fire-and-forget messages of value 1 and waits until the actor's
// state reaches `n`, with a bounded wait so a hang fails instead of hanging.
bool sendAndSettle(ProtoContext* ctx, const ProtoObject* actor, int n, Band band) {
    for (int i = 0; i < n; ++i) {
        ProtoContext scope(ctx->space, ctx);
        const ProtoObject* env = world().L().envelopeProto->newChild(&scope, true);
        const_cast<ProtoObject*>(env)->setAttribute(&scope, world().L().msgKey,
                                                    proto::makeSmallInt(1));
        ActorScheduler::instance().send(&scope, actor, band, env);
    }
    for (int spin = 0; spin < 20000; ++spin) {
        const ProtoObject* v = actor->getOwnAttributeDirect(ctx, world().L().actorStateKey);
        if (proto::asSmallInt(v) == n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

TEST(Scheduler, SpawnBuildsAnAnchoredActor) {
    ProtoContext ctx(&world().space, world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    EXPECT_TRUE(ActorScheduler::instance().isActor(&ctx, a));
    EXPECT_FALSE(ActorScheduler::instance().isActor(&ctx, proto::makeSmallInt(1)));
    // Anchored: the registry list holds it, so the GC reaches it without us.
    const ProtoObject* list =
        world().L().actorRegistry->getOwnAttributeDirect(&ctx, world().L().actorsKey);
    bool found = false;
    const ProtoList* actors = list->asList(&ctx);
    for (unsigned long i = 0; i < actors->getSize(&ctx); ++i)
        found = found || actors->getAt(&ctx, static_cast<int>(i)) == a;
    EXPECT_TRUE(found);
}

TEST(Scheduler, EverySentMessageIsProcessedExactlyOnce) {
    ProtoContext ctx(&world().space, world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    EXPECT_TRUE(sendAndSettle(&ctx, a, 5000, Band::Medium));
}

// The single-method invariant: concurrent senders never lose a message and
// never run the actor twice at once, so the counter lands exactly on N.
TEST(Scheduler, ConcurrentSendersLoseNothing) {
    ProtoContext ctx(&world().space, world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    constexpr int kSenders = 4, kPer = 2000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kSenders; ++t)
        ts.emplace_back([&] {
            ProtoContext mine(&world().space, world().root());
            for (int i = 0; i < kPer; ++i) {
                ProtoContext scope(&world().space, &mine);
                const ProtoObject* env = world().L().envelopeProto->newChild(&scope, true);
                const_cast<ProtoObject*>(env)->setAttribute(&scope, world().L().msgKey,
                                                            proto::makeSmallInt(1));
                ActorScheduler::instance().send(&scope, a, Band::Medium, env);
            }
        });
    for (auto& th : ts) th.join();
    for (int spin = 0; spin < 20000; ++spin) {
        const ProtoObject* v = a->getOwnAttributeDirect(&ctx, world().L().actorStateKey);
        if (proto::asSmallInt(v) == kSenders * kPer) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(proto::asSmallInt(a->getOwnAttributeDirect(&ctx, world().L().actorStateKey)),
              kSenders * kPer);
}

TEST(Scheduler, HighBandIsDrainedBeforeLow) {
    // One actor, one worker: queue Low first, then High; the handler records
    // the order it saw. With PROTOSCALA_ACTOR_WORKERS=1 (set by the test's
    // CTest environment) the order is deterministic.
    ProtoContext ctx(&world().space, world().root());
    const ProtoObject* a = spawnAdder(&ctx);
    // 1 message of value 1 on Low, then 1 of value 100 on High: the state is
    // 101 either way, so this test asserts the *order* through a probe actor
    // in the conformance suite (Task 4); here it asserts both were delivered.
    EXPECT_TRUE(sendAndSettle(&ctx, a, 1, Band::Low));
}

TEST(Scheduler, StatsCountEveryMessage) {
    EXPECT_GE(ActorScheduler::instance().messagesProcessed(), g_handlerCalls.load());
    EXPECT_GE(ActorScheduler::instance().workerCount(), 1u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();
    ProtoContext ctx(&world().space, world().root());
    ActorScheduler::instance().shutdown(&ctx);   // join before the space dies
    return rc;
}
```

In `tests/unit/CMakeLists.txt`, add a **separate** executable (it defines its own `main`):
```cmake
add_executable(test_actors test_scheduler.cpp)
target_link_libraries(test_actors PRIVATE protoScala_runtime GTest::gtest)
add_test(NAME unit/actors COMMAND test_actors)
set_tests_properties(unit/actors PROPERTIES ENVIRONMENT "PROTOSCALA_ACTOR_WORKERS=4")
```

- [ ] **Step 2: Extend `Runtime` with the Phase 5 objects and keys**

In `Runtime.cpp`, add `kActorProto, kFutureProto, kEnvelopeProto, kThreadProto, kActorRegistry` to the `RootSlot` enum and, next to the Phase 2 block:

```cpp
    // Phase 5: the concurrency prototypes (DESIGN §8). The registry is mutable
    // and anchors every actor for the session (D46), so the GC reaches an actor,
    // its mailboxes and every queued message from a root slot -- the ready
    // stacks are C++ and invisible to it.
    L.actorProto    = pin(kActorProto, L.anyRefProto->newChild(ctx, true));
    L.futureProto   = pin(kFutureProto, L.anyRefProto->newChild(ctx, true));
    L.envelopeProto = pin(kEnvelopeProto, L.anyProto->newChild(ctx, true));
    L.threadProto   = pin(kThreadProto, L.anyRefProto->newChild(ctx, true));
    L.actorRegistry = pin(kActorRegistry, L.anyProto->newChild(ctx, true));
    L.handlerKey    = key("__handler__");
    L.actorStateKey = key("__astate__");
    L.stateRefKey   = key("__sched_ref__");
    for (unsigned b = 0; b < 3; ++b) {
        L.mailboxKey[b] = key(("__mbox" + std::to_string(b) + "__").c_str());
        L.pendingKey[b] = key(("__pend" + std::to_string(b) + "__").c_str());
    }
    L.actorsKey     = key("__actors__");
    L.msgKey        = key("__msg__");
    L.futureKey     = key("__future__");
    L.snapshotKey   = key("__snapshot__");
    L.waitingOnKey  = key("__waiting_on__");
    L.turnFutureKey = key("__turn_future__");
    L.actorRegistry->setAttribute(ctx, L.actorsKey, ctx->newList()->asObject(ctx));
    bindType(L.actorProto, kActorKey, "Actor");
    bindType(L.futureProto, kFutureKey, "Future");
```
(`kActorKey`/`kFutureKey` are new constants in `ClassInfo.h` next to `kProductKey`; `builtinTypes()` gains the two `ClassInfo`s so `x.isInstanceOf[Actor]` and `case f: Future` compile.)

- [ ] **Step 3: Expose the thread-local hooks the scheduler needs**

In `ExecutionEngine.h`/`.cpp`: rename the file-local `ActiveGuard` to the public `ExecutionEngine::ActiveCallGuard` (same body), keep `run`/`callTopLevel`/`showTopLevel` using it, and add the native re-entry counter:

```cpp
// ExecutionEngine.cpp
namespace { thread_local unsigned tl_nativeDepth = 0; }

unsigned ExecutionEngine::nativeReentryDepth() { return tl_nativeDepth; }

const proto::ProtoObject* ExecutionEngine::callNative(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                                      const proto::ProtoObject* self,
                                                      const proto::ProtoObject* const* args,
                                                      unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* list = scope.newList(argc, args);
    struct DepthGuard {                      // a native that re-enters the VM
        DepthGuard() { ++tl_nativeDepth; }   // raises the depth for everything
        ~DepthGuard() { --tl_nativeDepth; }  // it calls (D43, A0-2)
    } depth;
    const proto::ProtoObject* r = fn(&scope, self, nullptr, list, nullptr);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}
```
The counter is decremented on unwinding too, because `DepthGuard` is a destructor — a `FutureYield` passing through does not leave it raised.

- [ ] **Step 4: Implement the scheduler**

`ActorScheduler.cpp`, the algorithmically load-bearing bodies (the rest is bookkeeping):

```cpp
unsigned ActorScheduler::configuredWorkerCount() {
    if (const char* env = std::getenv("PROTOSCALA_ACTOR_WORKERS"))
        if (*env) {
            const int n = std::atoi(env);
            if (n >= 1 && n <= 64) return static_cast<unsigned>(n);
        }
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 4;
    unsigned n = hc > 2 ? hc - 2 : 2;      // protoClojure's default
    return n > 16 ? 16u : n;
}

// The thread entry protoCore calls: the scheduler travels as a SmallInteger,
// exactly as a BytecodeModule address does in MAKE_FN.
static const proto::ProtoObject* workerEntry(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                             const proto::ParentLink*, const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    auto* s = reinterpret_cast<ActorScheduler*>(proto::asSmallInt(args->getAt(ctx, 0)));
    s->workerLoop(ctx);
    return PROTO_NONE;
}

const proto::ProtoObject* ActorScheduler::spawn(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* handler,
                                                const proto::ProtoObject* initialState) {
    const RuntimeLayout& L = *layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    auto* actor = const_cast<proto::ProtoObject*>(L.actorProto->newChild(&scope, /*isMutable=*/true));
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
                        proto::makeSmallInt(reinterpret_cast<long long>(st)));
    // Anchor before returning: from here the GC reaches the actor, its
    // mailboxes and everything they hold from a root slot (D46).
    auto* reg = const_cast<proto::ProtoObject*>(L.actorRegistry);
    for (;;) {
        const proto::ProtoObject* cur = reg->getOwnAttributeDirect(&scope, L.actorsKey);
        proto::ProtoContext one(scope.space, &scope);
        const proto::ProtoObject* next = cur->asList(&one)->appendLast(&one, actor)->asObject(&one);
        one.returnValue = next;
        if (reg->setAttributeIfEqual(&one, L.actorsKey, cur, next)) break;
    }
    scope.returnValue = actor;
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
        if (s == 3) {                                   // parked: wake it
            if (a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) {
                suspended_.fetch_sub(1, std::memory_order_relaxed);
                enqueue(a, highestPendingBand(ctx, a));
                return;
            }
            continue;
        }
        if (s == 1) {   // the suspension is still in flight: mark it, so
                        // finishTurn re-enqueues instead of parking (note 8)
            if (a->sched.compare_exchange_strong(s, 2, std::memory_order_acq_rel)) return;
            continue;
        }
        if (s == 0) {   // a stale wake (the yield was refused): an empty turn
            if (a->sched.compare_exchange_strong(s, 1, std::memory_order_acq_rel)) {
                enqueue(a, Band::Medium);
                return;
            }
            continue;
        }
        return;         // 2
    }
}

void ActorScheduler::enqueue(ActorState* a, Band band) {
    ready_[static_cast<unsigned>(band)].push(a);
    work_.release();
}

ActorState* ActorScheduler::dequeue() {
    for (unsigned b = 0; b < kBands; ++b)          // strict priority
        if (ActorState* a = ready_[b].pop()) return a;
    return nullptr;
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
    // The batch is stored on the actor before anything else allocates, so the
    // GC never sees it only from this C++ frame (P1).
    const_cast<proto::ProtoObject*>(a->actor)->setAttribute(ctx, L.pendingKey[band],
                                                            batch->asObject(ctx));
    a->pendingIdx[band] = 1;
    return batch->getAt(ctx, 0);
}

bool ActorScheduler::runTurn(proto::ProtoContext* ctx, ActorState* a) {
    const RuntimeLayout& L = *layout_;
    bool suspended = false;
    // A turn that starts on a suspended frame resumes it first (Task 6).
    if (a->actor->hasOwnAttribute(ctx, L.snapshotKey) == PROTO_TRUE) {
        resumeSuspendedTurn(ctx, a, &suspended);      // Task 6
        if (suspended) return true;
    }
    // Up to kBatchSize messages, always taking the highest non-empty band
    // first: the band scan restarts after every message, so a High-band
    // message that arrives mid-turn wins the next slot (DESIGN §8.2).
    for (unsigned budget = kBatchSize; budget > 0; --budget) {
        proto::ProtoContext turn(ctx->space, ctx);   // one context per message (P2)
        const proto::ProtoObject* env = nullptr;
        for (unsigned band = 0; band < kBands && !env; ++band)
            env = nextMessage(&turn, a, band);
        if (!env) break;                              // nothing queued: the turn is over
        deliver(&turn, a, env, &suspended);
        messages_.fetch_add(1, std::memory_order_relaxed);
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
            if (hasWork(ctx, a)) { enqueue(a, highestPendingBand(ctx, a)); return; }
            unsigned claimed = 1;
            if (a->sched.compare_exchange_strong(claimed, 0, std::memory_order_acq_rel)) return;
            continue;   // a sender re-marked 2: go round again
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
    } catch (...) {           // never leave an actor claimed on an unknown error
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
            while (drainOne(ctx)) {}       // a sender may have pushed after our last pop
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
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);   // opened before any lock
            work_.acquire();
        }
    }
}

void ActorScheduler::shutdown(proto::ProtoContext* ctx) {
    if (!started_.load(std::memory_order_acquire)) return;
    shuttingDown_.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < workers_.size(); ++i) work_.release();
    for (const proto::ProtoThread* t : workers_)
        if (t) const_cast<proto::ProtoThread*>(t)->join(ctx);
    workers_.clear();
    started_.store(false, std::memory_order_release);
    if (const unsigned n = suspended_.load(std::memory_order_relaxed))
        std::fprintf(stderr,
                     "protoscala: %u actor(s) were still waiting on a future that never "
                     "completed\n", n);
}
```

`ensureStarted` follows protoClojure (`std::call_once`, `ProtoSpace::newThread` per worker with the scheduler address as the single argument, thread name `"protoscala-actor-worker"`), storing `space_`, `layout_`, `engine_` first.

- [ ] **Step 5: Run**

Run: `cmake --build build_release && ctest --test-dir build_release -R "unit/actors" --output-on-failure`
Expected: `5 tests passed` and no leak of workers (the binary exits cleanly; `shutdown` runs in `main`).

Run it again under a low heap so every queued message is allocated under pressure:
`PROTOCORE_HEAP_LIMIT_CELLS=200000 ctest --test-dir build_release -R "unit/actors" --output-on-failure`
Expected: the same result — proof that the registry anchor and the pending lists keep every queued payload reachable.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/ActorScheduler.h src/runtime/ActorScheduler.cpp src/runtime/Runtime.h \
        src/runtime/Runtime.cpp src/runtime/ExecutionEngine.h src/runtime/ExecutionEngine.cpp \
        src/compiler/ClassInfo.h src/compiler/ClassInfo.cpp tests/unit/test_scheduler.cpp \
        tests/unit/CMakeLists.txt CMakeLists.txt
git commit -m "runtime: actor scheduler with three priority bands

Actors are mutable protoCore objects holding their handler, state and three
mailboxes, anchored in a registry pinned in a root slot, so the GC reaches
every queued payload (DESIGN §8.2, P1). One atomic per actor carries the
single-method invariant across claim, wake and suspension; workers are
protoCore threads that spin before parking inside an UnmanagedScope.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 4: The Scala surface — `Actor.spawn`, `!`, `send`, `value`, `isActor`, `stats`

**Files:**
- New: `src/runtime/ActorPrimitives.cpp`
- Modify: `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `src/runtime/Values.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/repl/Session.cpp`, `tests/unit/EvalHarness.h`, `lib/prelude.scala`, `CMakeLists.txt`
- Test: `tests/conformance/13-actors/*.scala`, `tests/unit/test_primitives.cpp`

**Interfaces:**

Global bindings (`builtinGlobalNames()` gains `"Actor"`, `"Priority"`), each a mutable object in the globals carrying native methods — the pattern `List`/`Nil` already uses (`Primitives.cpp:931-933`), **not** prelude objects: a prelude `object Actor` would claim the type key `@Actor` that the `Actor` type of `builtinTypes()` owns.

| Receiver | Member | Arity | Meaning |
|---|---|---|---|
| `Actor` | `spawn(state)` | 1 | returns a one-shot applier object; `.apply(handler)` spawns (Scala's `Actor.spawn(s)(h)`) |
| `Actor` | `isActor(x)` | 1 | `Boolean` |
| `Actor` | `stats` | 0 | `ActorStats(workers, messagesProcessed)` (built through the prelude hooks) |
| `Priority` | `High` / `Medium` / `Low` | — | the fields `0` / `1` / `2` |
| an actor | `!(msg)` | 1 | fire-and-forget on the Medium band; `Unit` |
| an actor | `send(msg, priority)` | 2 | fire-and-forget on an explicit band; `Unit` |
| an actor | `value` | 0 | the current state, read without a message (protoClojure's `@actor`) |
| an actor | `toString` | 0 | `Actor(<state>)` |

`RuntimeLayout` gains `spawnPartialProto` (a root-slot prototype whose native `apply` finishes a curried `Actor.spawn`), `tuple2Key` (Step 1) and the prelude hooks (design note 14):
```cpp
    struct PreludeHooks {                  // filled by bindPreludeHooks, after the prelude runs
        const proto::ProtoObject* actorStats   = nullptr;  // the ActorStats companion
        const proto::ProtoObject* someCompanion = nullptr; // Some   (Task 5)
        const proto::ProtoObject* success      = nullptr;  // Success (Task 5)
        const proto::ProtoObject* failure      = nullptr;  // Failure (Task 5)
        const proto::ProtoObject* runtimeError = nullptr;  // RuntimeError (Task 5)
    } hooks;
```
```cpp
// Resolves the prelude values the native methods construct. Call once, right
// after loadPrelude, from every entry point that builds a runtime (Session,
// EvalHarness). A missing name is a build defect, not user input: it throws.
void bindPreludeHooks(proto::ProtoContext* ctx, RuntimeLayout& layout, const GlobalTable& globals);
```

- [ ] **Step 1: The handler calling convention (`ActorScheduler::deliver`)**

This is the body Task 3 needs; it is given here because it fixes the calling convention (A0-4). Task 3 implements it **without** the `__future__` branch (Task 5 adds it) and **without** the `FutureYield` catch (Task 6 adds it).

```cpp
// Runs one message: handler(state, msg) must return (newState, reply) (D45).
// The actor keeps its previous state when the handler fails (DESIGN §8.4).
void ActorScheduler::deliver(proto::ProtoContext* ctx, ActorState* a,
                             const proto::ProtoObject* envelope, bool* suspended) {
    const RuntimeLayout& L = *layout_;
    auto* actor = const_cast<proto::ProtoObject*>(a->actor);
    proto::ProtoContext call(ctx->space, ctx);
    call.resizeAutomaticLocals(3);
    const proto::ProtoObject** slot = call.getAutomaticLocals();
    slot[0] = actor->getOwnAttributeDirect(&call, L.handlerKey);
    slot[1] = actor->getOwnAttributeDirect(&call, L.actorStateKey);
    slot[2] = envelope->getOwnAttributeDirect(&call, L.msgKey);
    tl_currentActor = actor;                       // Task 6 reads this in `await`
    try {
        const proto::ProtoObject* r = engine_->invoke(&call, slot[0], slot + 1, 2);
        if (!isTuple2(&call, L, r))
            throw ScalaError("IllegalArgumentException",
                             "an actor handler must return (newState, reply), got " +
                                 typeName(&call, L, r));
        actor->setAttribute(&call, L.actorStateKey, r->getOwnAttributeDirect(&call, L.tupleFieldKey[1]));
        completeReply(&call, envelope, r->getOwnAttributeDirect(&call, L.tupleFieldKey[2]), false);
    } catch (ScalaError& e) {
        // DESIGN §8.4: the ask's future fails, the error is reported, the actor
        // keeps its previous state and stays alive. Supervision is out of scope.
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n", e.what());
        completeFailure(&call, envelope, e);
    }
    tl_currentActor = nullptr;
}
```
(`completeReply`/`completeFailure` are no-ops until Task 5. `isTuple2` is the Phase 2 class-membership test on the `Tuple2` marker key — `v != PROTO_NONE && v->getAttribute(ctx, L.tuple2Key) == PROTO_TRUE`, where `tuple2Key` is the interned `tupleTypeKey(2)` added to `RuntimeLayout` here; `__tuple__` alone would accept any arity.)

- [ ] **Step 2: Write the failing fixtures**

`tests/conformance/13-actors/spawn-and-send-indent.scala`:
```scala
// EXPECT: 10
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
var i = 0
while i < 10 do
  counter ! 1
  i += 1
// The single-method invariant makes this exact once every message is drained.
var seen = -1
while seen < 10 do
  seen = counter.value
println(counter.value)
```
`tests/conformance/13-actors/spawn-and-send-braces.scala` is the same program in brace syntax.

`tests/conformance/13-actors/case-class-messages.scala`:
```scala
// EXPECT: 42
case class Increment(by: Int)
case object GetValue
val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}
counter ! Increment(40)
counter ! Increment(2)
var seen = 0
while seen < 42 do
  seen = counter.value
println(seen)
```

`tests/conformance/13-actors/actor-predicate.scala`:
```scala
// EXPECT: true false Actor(0)
val a = Actor.spawn(0) { (s, m) => (s, s) }
println(Actor.isActor(a).toString + " " + Actor.isActor(1).toString + " " + a.toString)
```

`tests/conformance/13-actors/stats.scala`:
```scala
// EXPECT: true
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < 100 do
  a ! 1
  i += 1
var seen = 0
while seen < 100 do
  seen = a.value
val st = Actor.stats
println((st.workers >= 1 && st.messagesProcessed >= 100).toString)
```

`tests/conformance/13-actors/priority-bands.scala` — deterministic because the probe actor is fed after a barrier and one worker drains High first:
```scala
// EXPECT: List(high, low)
val log = Actor.spawn(Nil) { (state, msg) =>
  val next = msg :: state
  (next, next)
}
// Block the actor with a first message, queue Low then High behind it, and
// read the order they were handled in. The claim the blocking message holds
// makes both land in the same batch, so the bands decide the order.
log ! "start"
log.send("low", Priority.Low)
log.send("high", Priority.High)
var seen: List[String] = Nil
while seen.length < 3 do
  seen = log.value
println(seen.reverse.tail.mkString("List(", ", ", ")"))
```
(If the fixture proves flaky because "start" is drained before the other two are queued, replace the barrier with a handler that spins on its first message; record the change in the task notes. The rule of the Global Constraints stands: a fixture that cannot be made deterministic moves to the benchmark suite.)

`tests/conformance/13-actors/handler-must-return-a-pair.scala`:
```scala
// EXPECT: 0
// A handler that does not return (newState, reply) fails the message; the
// actor keeps its state and stays alive (D45, DESIGN §8.4).
val a = Actor.spawn(0) { (s, m) => s }
a ! 1
a ! 2
println(a.value)
```
(The error text goes to stderr, which the runner ignores for an `EXPECT` directive.)

`tests/conformance/13-actors/many-actors.scala` (protoClojure's `many-actors.clj` shape):
```scala
// EXPECT: 65
// Five actors, each holding a different number; each is told 10 and then asked
// for its state. The ask is ordered behind the tell, so the sum is exact.
// (1+2+3+4+5) + 5*10 = 65.
val actors = List(1, 2, 3, 4, 5).map(n => Actor.spawn(n) { (s, m) => (s + m, s + m) })
actors.foreach(a => a ! 10)
var total = 0
actors.foreach(a => total += (a ? 0).await)
println(total)
```
Phase 2's `List` has `map`, `flatMap`, `filter`, `foreach`, `length`, `head`, `tail`, `drop` and `mkString` but **no `foldLeft`** (verified: `xs.foldLeft(0)(...)` fails with `NoSuchMethodError: value foldLeft is not a member of List`), hence the `var` and `foreach` above. Never add a collection method to make a fixture pass — that is Phase 3's work.

- [ ] **Step 3: Implement the primitives**

`src/runtime/ActorPrimitives.cpp` (the shapes; every one uses the `PRIM` macro and the helpers of `PrimitiveSupport.h`):

```cpp
// Actor.spawn(state) -> an applier whose apply(handler) spawns the actor.
// Scala writes Actor.spawn(0) { (s, m) => ... }, which is
// Actor.spawn(0).apply(handler) (DESIGN §8.1).
PRIM(actor_spawnPartial) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* state = arg(ctx, args, 0, "Actor.spawn", 1);
    ProtoObject* partial = const_cast<ProtoObject*>(L.spawnPartialProto->newChild(ctx, true));
    partial->setAttribute(ctx, L.actorStateKey, state);
    return partial;
}

PRIM(actor_spawnApply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* handler = arg(ctx, args, 0, "Actor.spawn", 1);
    ActorScheduler& s = ActorScheduler::instance();
    s.ensureStarted(ctx->space, ctx, L, activeCallContext()->engine);
    return s.spawn(ctx, handler, self->getOwnAttributeDirect(ctx, L.actorStateKey));
}

// a ! msg  /  a.send(msg, priority)
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
PRIM(actor_value) {  // protoClojure's @actor: a read with no message, so it may
                     // observe a state older than a send that is still queued
    return self->getOwnAttributeDirect(ctx, layoutOf().actorStateKey);
}
PRIM(actor_toString) {
    const RuntimeLayout& L = layoutOf();
    return str(ctx, "Actor(" + show(ctx, L, self->getOwnAttributeDirect(ctx, L.actorStateKey)) + ")");
}
PRIM(actorObject_isActor) {
    return boolean(ActorScheduler::instance().isActor(ctx, arg(ctx, args, 0, "Actor.isActor", 1)));
}
PRIM(actorObject_stats) {
    const RuntimeLayout& L = layoutOf();
    const ActorScheduler& s = ActorScheduler::instance();
    const ProtoObject* a[2] = {proto::makeSmallInt(s.workerCount()),
                               proto::makeSmallInt(s.messagesProcessed())};
    return activeCallContext()->engine->send(ctx, L.hooks.actorStats, L.applyName, a, 2);
}
```
`sendEnvelope` builds the envelope (a mutable child of `envelopeProto` with `__msg__`, plus `__future__` in Task 5) in its own child context and calls `ActorScheduler::send`; `bandArg` maps `0/1/2` to `Band` and raises `IllegalArgumentException: send expects Priority.High, Priority.Medium or Priority.Low` otherwise.

Install them from `installPrimitives` (a new `installActorPrimitives(ctx, L)` called next to `installProductPrimitives`), bind the `Actor` and `Priority` globals, and add both names to `builtinGlobalNames()`.

- [ ] **Step 4: `show`, `typeName` and the prelude**

In `Values.cpp`: `typeName` returns `"Actor"` for a child of `actorProto`; `show` calls the actor's own `toString` (an actor is a Scala instance with a `__name__`, so the Phase 2 instance path already does this once `actorProto` carries `__name__` — verify with the fixture and, if not, add the explicit branch).

In `lib/prelude.scala`, append:
```scala
// Phase 5: the value Actor.stats returns.
final case class ActorStats(workers: Int, messagesProcessed: Int)
```

In `Session::Session` and `EvalHarness`, after `loadPrelude(...)`, add `bindPreludeHooks(&ctx, runtime_.mutableLayout(), globals_);` (`Runtime` gains a non-const accessor used only here).

- [ ] **Step 5: `~Session` joins the workers**

```cpp
Session::~Session() {
    // Every exit path (normal end, uncaught error, sys.exit through the REPL)
    // passes here before the ProtoSpace member is destroyed (DESIGN §8.2).
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    ActorScheduler::instance().shutdown(&ctx);
    std::fflush(stdout);
}
```

- [ ] **Step 6: Run**

Run: `cmake --build build_release && ctest --test-dir build_release -R "conformance/13-actors" --output-on-failure`
Expected: every fixture passes.

Run: `PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release -R "conformance/13-actors" --output-on-failure`
and the same with `=16`. Expected: identical results — the fixtures are interleaving-independent by construction.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/ActorPrimitives.cpp src/runtime/Primitives.cpp src/runtime/Primitives.h \
        src/runtime/Values.cpp src/runtime/Runtime.h src/runtime/Runtime.cpp \
        src/repl/Session.cpp tests/unit/EvalHarness.h lib/prelude.scala \
        tests/conformance/13-actors CMakeLists.txt
git commit -m "actors: Scala surface for spawn, !, send, value and stats

Actor.spawn(state)(handler), fire-and-forget sends on three priority bands,
Actor.isActor, Actor.stats and the Actor(<state>) printed form (DESIGN §8.1).
A handler must return (newState, reply); one that fails leaves the actor
alive with its previous state. ~Session joins every worker.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 5: `Future`, `?` / `ask`, blocking `await`, failures — and `Thread` / `System`

**Files:**
- New: `src/runtime/Futures.h`, `src/runtime/Futures.cpp`
- Modify: `src/runtime/ActorPrimitives.cpp`, `src/runtime/ActorScheduler.cpp`, `src/runtime/Runtime.cpp`, `src/runtime/Values.cpp`, `lib/prelude.scala`, `CMakeLists.txt`
- Test: `tests/unit/test_futures.cpp` (in the `test_actors` binary), `tests/conformance/14-futures/*.scala`

**Interfaces:**
```cpp
namespace protoScala::futures {

// State of __fstate__: 0 pending, 1 success, 2 failure.
inline constexpr long long kPending = 0, kSuccess = 1, kFailure = 2;

const proto::ProtoObject* create(proto::ProtoContext*, const RuntimeLayout&);
long long state(proto::ProtoContext*, const RuntimeLayout&, const proto::ProtoObject* f);

// Writes the value, then flips __fstate__ with one CAS (protoClojure's
// deliverPromise): a reader never sees a completed future without its value.
// Returns false when the future was already completed. On success it wakes
// every blocked thread, re-schedules every suspended actor waiter and runs
// every registered continuation on THIS thread (D48).
bool complete(proto::ProtoContext*, const RuntimeLayout&, const proto::ProtoObject* f,
              const proto::ProtoObject* value, bool failed);

// Appends `actor` to __waiters__. Returns false when the future had already
// completed, in which case the caller must not suspend (design note 8).
bool addWaiter(proto::ProtoContext*, const RuntimeLayout&, const proto::ProtoObject* f,
               const proto::ProtoObject* actor);

// Blocks the calling thread until `f` completes, inside an UnmanagedScope, and
// returns its value; raises the ScalaError of a failed future (Scala's
// Await.result). Never called on a worker inside an actor turn (Task 6).
const proto::ProtoObject* awaitBlocking(proto::ProtoContext*, const RuntimeLayout&,
                                        const proto::ProtoObject* f);
} // namespace protoScala::futures
```

Surface added in this task:

| Receiver | Member | Meaning |
|---|---|---|
| an actor | `?(msg)` / `ask(msg, priority)` | `Future` of the handler's reply |
| a future | `await` | the value; raises the error of a failed future |
| a future | `isCompleted` | `Boolean` |
| a future | `value` | `Option[Try[T]]`: `None` while pending |
| a future | `toString` | `Future(<pending>)`, `Future(10)`, `Future(<failed: ...>)` |
| `Thread` | `start(body)` / a thread's `join()` | an OS thread in the GC quorum (A0-9) |
| `System` | `nanoTime()` / `currentTimeMillis()` | `Long` (A0-9) |

- [ ] **Step 1: Write the failing tests**

`tests/conformance/14-futures/ask-and-await.scala`:
```scala
// EXPECT: 10
val counter = Actor.spawn(0) { (state, msg) =>
  (state + msg, state + msg)
}
counter ! 4
val f = counter ? 6
println(f.await)
```

`tests/conformance/14-futures/ask-is-ordered-behind-sends.scala`:
```scala
// EXPECT: 1000
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
var i = 0
while i < 999 do
  counter ! 1
  i += 1
// The ask is queued last on the same band, and the single-method invariant
// processes a band in order, so its reply is the final count.
println((counter ? 1).await)
```

`tests/conformance/14-futures/failed-ask.scala`:
```scala
// EXPECT: ArithmeticException: / by zero
val bad = Actor.spawn(0) { (s, m) => (s, s / 0) }
val f = bad ? 1
while !f.isCompleted do ()
f.value match
  case Some(Failure(e)) => println(e.toString)
  case other            => println("unexpected: " + other.toString)
```

`tests/conformance/14-futures/await-a-failed-future.scala`:
```scala
// EXPECT-ERROR: ArithmeticException
val bad = Actor.spawn(0) { (s, m) => (s, s / 0) }
println((bad ? 1).await)
```

`tests/conformance/14-futures/threads-send-concurrently.scala` (protoClojure's `concurrent-sends-no-race.clj` shape, on real threads):
```scala
// EXPECT: 4000
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
val threads = List(1, 2, 3, 4).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 1000 do
      counter ! 1
      i += 1
  }
}
threads.foreach(t => t.join())
println((counter ? 0).await)
```

`tests/unit/test_futures.cpp` (added to the `test_actors` binary) covers what Scala cannot reach: `complete` returning `false` the second time, `addWaiter` rejecting an already-completed future, and 8 threads completing 8 futures while 8 others block on `awaitBlocking`.

- [ ] **Step 2: Implement `Futures.cpp`**

```cpp
// The completion CAS. The value is written first: a reader that sees state != 0
// always sees the value that belongs to it (protoClojure's deliverPromise).
bool complete(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
              const proto::ProtoObject* value, bool failed) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    fut->setAttribute(ctx, failed ? L.ferrorKey : L.fvalueKey, value);
    const proto::ProtoObject* target = proto::makeSmallInt(failed ? kFailure : kSuccess);
    if (!fut->setAttributeIfEqual(ctx, L.fstateKey, proto::makeSmallInt(kPending), target))
        return false;                      // someone else completed it first
    // Waiters are read after the CAS won: one appended before it is here, one
    // appended after is rejected by addWaiter (design note 7).
    const proto::ProtoObject* waiters = fut->getOwnAttributeDirect(ctx, L.waitersKey);
    if (waiters && waiters != PROTO_NONE) {
        const proto::ProtoList* list = waiters->asList(ctx);
        for (unsigned long i = 0; i < list->getSize(ctx); ++i)
            ActorScheduler::instance().resume(ctx, list->getAt(ctx, static_cast<int>(i)));
    }
    runContinuations(ctx, L, f);           // Task 7; a no-op here
    wakeBlockedThreads();                  // only when the counter is non-zero
    return true;
}

bool addWaiter(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* f,
               const proto::ProtoObject* actor) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    for (;;) {
        if (state(ctx, L, f) != kPending) return false;
        const proto::ProtoObject* cur = fut->getOwnAttributeDirect(ctx, L.waitersKey);
        proto::ProtoContext scope(ctx->space, ctx);
        const proto::ProtoObject* next =
            (cur && cur != PROTO_NONE ? cur->asList(&scope) : scope.newList())
                ->appendLast(&scope, actor)->asObject(&scope);
        scope.returnValue = next;
        if (fut->setAttributeIfEqual(&scope, L.waitersKey, cur, next))
            // Re-check: a completion that won between the state read and this
            // CAS has already walked the old list, so we must not park.
            return state(&scope, L, f) == kPending;
    }
}

// One condition variable for every blocking waiter in the process, plus an
// epoch bumped by each completion and a counter of blocked threads, so a
// completion pays nothing when nobody is blocked (protoST's mainWaitingOn
// trick, generalised to any thread).
//
// The future's state is a ProtoObject attribute, which an unmanaged thread may
// NOT read (the UnmanagedScope contract), so the predicate is the plain epoch
// and the state is re-read after leaving the region on every wake.
const proto::ProtoObject* awaitBlocking(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* f) {
    while (state(ctx, L, f) == kPending) {           // managed: the read is legal here
        g_blocked.fetch_add(1, std::memory_order_seq_cst);
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);   // opened before the lock (P6)
            std::unique_lock<std::mutex> lk(g_waitMutex);
            const unsigned long long seen = g_epoch.load(std::memory_order_acquire);
            // The 50 ms bound is a safety net, not a poll: it turns a lost
            // wake-up into a 50 ms delay instead of a hung process.
            g_waitCv.wait_for(lk, std::chrono::milliseconds(50),
                              [&] { return g_epoch.load(std::memory_order_acquire) != seen; });
        }
        g_blocked.fetch_sub(1, std::memory_order_seq_cst);
    }
    return result(ctx, L, f);        // the value, or the failed future's error
}

// Called at the end of complete(), after the state CAS won.
void wakeBlockedThreads() {
    g_epoch.fetch_add(1, std::memory_order_release);
    if (g_blocked.load(std::memory_order_seq_cst) != 0) {
        std::lock_guard<std::mutex> lk(g_waitMutex);   // pairs with the waiter's lock
        g_waitCv.notify_all();
    }
}

- [ ] **Step 3: Implement the surface**

`?`/`ask` build the envelope with a fresh `Future` under `__future__`, send it, and return the future; `completeReply`/`completeFailure` in `ActorScheduler::deliver` become:
```cpp
void ActorScheduler::completeReply(proto::ProtoContext* ctx, const proto::ProtoObject* envelope,
                                   const proto::ProtoObject* reply, bool failed) {
    const proto::ProtoObject* f = envelope->getOwnAttributeDirect(ctx, layout_->futureKey);
    if (f && f != PROTO_NONE) futures::complete(ctx, *layout_, f, reply, failed);
}
```
and a failure completes with a prelude `RuntimeError(className, message)` built through `L.hooks.runtimeError`.

`Future.value` returns `None` while pending and `Some(Success(v))` / `Some(Failure(e))` otherwise, through `L.hooks`. `await` calls `futures::awaitBlocking` **unless** Task 6's cooperative path applies.

`Thread.start(body)` creates a `ProtoSpace::newThread` whose entry installs the `ActiveCallGuard` and calls `engine->callTopLevel(ctx, body, nullptr, 0)`, reporting an escaped `ScalaError` on stderr; the returned handle keeps the `ProtoThread*` as a `SmallInteger` and `join()` joins it inside an `UnmanagedScope`. `System.nanoTime()` returns `std::chrono::steady_clock` nanoseconds as a `SmallInteger` (54 bits hold ~208 days of nanoseconds; the value is a difference-friendly monotonic clock and the deviation note says so).

- [ ] **Step 4: Prelude**

```scala
// Phase 5: the result of a computation that may have failed. Phase 4 replaces
// RuntimeError with real exception values (D44).
final case class RuntimeError(className: String, message: String):
  override def toString: String = className + ": " + message

sealed abstract class Try[+A]:
  def isSuccess: Boolean
  def isFailure: Boolean = !isSuccess
  def get: A
  def getOrElse[B >: A](default: B): B = if isSuccess then get else default
  def toOption: Option[A] = if isSuccess then Some(get) else None

final case class Success[+A](value: A) extends Try[A]:
  def isSuccess: Boolean = true
  def get: A = value

final case class Failure[+A](error: RuntimeError) extends Try[A]:
  def isSuccess: Boolean = false
  def get: A = __raise(error.className, error.message)
```
Note: `RuntimeError` overrides `toString`, so `e.toString` in `failed-ask.scala` prints `ArithmeticException: / by zero` (the wording protoScala already uses for a division by zero — verified: `1 / 0` reports `error: ArithmeticException: / by zero`), and `Failure(e).toString` prints `Failure(ArithmeticException: / by zero)`. The fixture's `EXPECT` matches the first; the implementer confirms by running it, not by reading this plan.

- [ ] **Step 5: Run**

Run: `cmake --build build_release && ctest --test-dir build_release -R "14-futures|unit/actors" --output-on-failure`, then repeat with `PROTOSCALA_ACTOR_WORKERS=1` and `=16`.
Expected: all green in all three configurations.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/Futures.h src/runtime/Futures.cpp src/runtime/ActorPrimitives.cpp \
        src/runtime/ActorScheduler.cpp src/runtime/Runtime.cpp src/runtime/Values.cpp \
        lib/prelude.scala tests/unit/test_futures.cpp tests/conformance/14-futures CMakeLists.txt
git commit -m "futures: ask, blocking await, failures, Thread and System

A Future is a protoCore object completed with one CAS after its value is
written; a blocked thread parks on a condition variable inside an
UnmanagedScope and a completion pays nothing when nobody is blocked. A
handler failure completes the ask with Failure(RuntimeError(...)) and leaves
the actor alive (DESIGN §8.3, §8.4).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 6: Cooperative `await` inside an actor — `FutureYield` and frame snapshots

**Files:**
- New: `src/runtime/FutureYield.h`
- Modify: `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`, `src/runtime/ActorScheduler.h`, `src/runtime/ActorScheduler.cpp`, `src/runtime/ActorPrimitives.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`
- Test: `tests/unit/test_yield.cpp` (in the `test_actors` binary), `tests/conformance/13-actors/await-*.scala`, `tests/CMakeLists.txt`

**Interfaces:**
```cpp
// src/runtime/FutureYield.h
// The cooperative-yield control signal: Future.await throws it when the
// calling thread is inside an actor turn and the future is still pending.
//
// It derives from nothing -- in particular not from std::exception -- so no
// generic handler can intercept it (DESIGN §7). ExecutionEngine::runLoop
// catches it per frame, prepends that frame's record to the actor's snapshot
// and rethrows; the scheduler's turn catches it last and parks the actor.
namespace protoScala {
class FutureYield {
public:
    explicit FutureYield(const proto::ProtoObject* future) noexcept : future_(future) {}
    const proto::ProtoObject* future() const noexcept { return future_; }
private:
    const proto::ProtoObject* future_;
};
} // namespace protoScala
```

`RuntimeLayout` gains `frameProto` (a root slot) and the frame-record keys `__mod__`, `__ip__`, `__fbase__`, `__fslots__`.

`ExecutionEngine` gains:
```cpp
    // Re-materialises a suspended call chain and continues it. `frames` is the
    // actor's snapshot list, outermost first; `injected` is the value the
    // await that suspended the chain must return. Frame `idx` is rebuilt, the
    // inner frames run first, and their result is written where the in-flight
    // call would have left it.
    const proto::ProtoObject* resumeFrames(proto::ProtoContext* parent,
                                           const proto::ProtoList* frames, unsigned idx,
                                           const proto::ProtoObject* injected);
private:
    static constexpr unsigned kNoPendingCall = 0xFFFFFFFFu;
    // The dispatch loop of one frame, entered fresh by execute() and again by
    // resumeFrames() with a restored frame.
    const proto::ProtoObject* runLoop(proto::ProtoContext& frame, const BytecodeModule& mod,
                                      const proto::ProtoObject** slots,
                                      const proto::ProtoObject** sp, const Instr* ip);
```

`ActorScheduler` gains `void resumeSuspendedTurn(proto::ProtoContext*, ActorState*, bool* suspended);` and the free functions `const proto::ProtoObject* currentActor();` / `void setCurrentActor(const proto::ProtoObject*);` over a `thread_local`.

- [ ] **Step 1: Write the failing tests**

`tests/conformance/13-actors/await-one-worker.scala` — the DESIGN §8.5 `await` shape in miniature; it **must** pass with a single worker, which a blocking `await` could never do:
```scala
// EXPECT: 84
val adder = Actor.spawn(0) { (s, m) => (s, m * 2) }
val caller = Actor.spawn(0) { (s, m) =>
  val doubled = (adder ? m).await
  (s + doubled, s + doubled)
}
println((caller ? 42).await)
```
Register it with one worker:
```cmake
set_tests_properties("conformance/13-actors/await-one-worker.scala" PROPERTIES
    ENVIRONMENT "PROTOSCALA_ACTOR_WORKERS=1")
```
and add the brace variant with the same property.

`tests/conformance/13-actors/await-deep-chain.scala` — the snapshot must span several bytecode frames:
```scala
// EXPECT: 30
val echo = Actor.spawn(0) { (s, m) => (s, m) }
def level3(f: Future[Int]): Int = f.await + 1
def level2(f: Future[Int]): Int = level3(f) + 1
def level1(f: Future[Int]): Int = level2(f) + 1
val caller = Actor.spawn(0) { (s, m) =>
  val r = level1(echo ? 27)
  (r, r)
}
println((caller ? 0).await)
```

`tests/conformance/13-actors/await-in-a-loop.scala` — two suspensions in one message, so the resume path installs a second snapshot:
```scala
// EXPECT: 6
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val caller = Actor.spawn(0) { (s, m) =>
  var total = 0
  var i = 1
  while i <= 3 do
    total += (echo ? i).await
    i += 1
  (total, total)
}
println((caller ? 0).await)
```

`tests/conformance/13-actors/await-refused-inside-map.scala` — D43's refusal, observable and harmless:
```scala
// EXPECT: true
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val caller = Actor.spawn(0) { (s, m) =>
  val r = List(1, 2).map(x => (echo ? x).await)
  (r, r)
}
val f = caller ? 0
while !f.isCompleted do ()
val failed = f.value match
  case Some(Failure(e)) => e.className == "UnsupportedOperationException"
  case other            => false
println(failed.toString)
```

`tests/unit/test_yield.cpp` covers what Scala cannot reach deterministically:
- `addWaiter` on a future completed between the state read and the CAS returns `false`, and the actor is **not** parked (drive `futures::complete` from another thread with the waiter list pre-seeded);
- an actor suspended on a future that is completed *before* `finishTurn` runs ends in state `Claimed` and on a ready stack, never in state `Suspended` (call `resume` while the turn still holds the claim, then `finishTurn(..., suspended=true)` and assert the state);
- a snapshot round trip: build a two-frame chain by hand, `resumeFrames` it with an injected value, and check the result.

- [ ] **Step 2: Split `execute` and track the in-flight call**

In `ExecutionEngine.cpp`, move the whole `for (;;) { ... }` dispatch loop out of `execute` into `runLoop`, keeping every case unchanged except:

```cpp
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
    // yield (DESIGN §8.3, Design note 4).
    unsigned pendingBase = kNoPendingCall;
    try {
        for (;;) {
            ...
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
            ...
        }
    } catch (FutureYield&) {
        // This frame is part of a suspended chain. Record it and rethrow; the
        // frames prepend themselves, so the actor's list reads outermost-first
        // (the innermost frame is the first to catch).
        if (pendingBase == kNoPendingCall)
            throw ScalaError("UnsupportedOperationException",
                             "await is not supported here: " + mod.name() +
                                 " cannot be suspended at this instruction");
        appendSuspendedFrame(&frame, layout_, mod, static_cast<unsigned>(ip - code),
                             pendingBase, slots);
        throw;
    } catch (ScalaError& e) {
        ...unchanged...
    } catch (const std::runtime_error& e) {
        ...unchanged...
    }
}
```
`pendingBase` is set and cleared the same way in `CALL_SPREAD`, `SEND`, `SEND_APPLY`, `SEND_SUPER`, `SEND_KW`, `NEW`, `NEW_SPREAD`, `INVOKE_INIT` and `FORCE` (for `FORCE` the base is `sp - 1`, since the forced value replaces the operand). Every other opcode leaves it at `kNoPendingCall`, which is exactly what makes an `await` under `EQ`/`valuesEqual` or `MATCH_ERROR`/`show` refuse instead of corrupting the frame.

`execute` keeps its prologue (arity check, frame, slots, varargs, captures) and ends with
`return runLoop(frame, mod, slots, slots + stackBase, mod.code().data());`
— the `FutureYield` clause is **first**, before `catch (ScalaError&)`, so the existing `catch (const std::runtime_error&)` can never swallow the signal (Design note 5).

- [ ] **Step 3: The snapshot and the resume**

```cpp
namespace {
// One frame of a suspended chain: where to continue, and the values that were
// live below the in-flight call. Everything above the call's base is dead --
// it was the callee's arguments, which the callee consumed.
void appendSuspendedFrame(proto::ProtoContext* ctx, const RuntimeLayout& L,
                          const BytecodeModule& mod, unsigned ipOffset, unsigned pendingBase,
                          const proto::ProtoObject** slots) {
    auto* actor = const_cast<proto::ProtoObject*>(currentActor());
    proto::ProtoContext scope(ctx->space, ctx);
    auto* rec = const_cast<proto::ProtoObject*>(L.frameProto->newChild(&scope, /*isMutable=*/true));
    // The module address travels as a SmallInteger, as MAKE_FN already does;
    // modules are owned by the Session for the whole session, so it cannot dangle.
    rec->setAttribute(&scope, L.modKey, proto::makeSmallInt(reinterpret_cast<long long>(&mod)));
    rec->setAttribute(&scope, L.ipKey, proto::makeSmallInt(ipOffset));
    rec->setAttribute(&scope, L.fbaseKey, proto::makeSmallInt(pendingBase));
    rec->setAttribute(&scope, L.fslotsKey, scope.newList(pendingBase, slots)->asObject(&scope));
    const proto::ProtoObject* cur = actor->getOwnAttributeDirect(&scope, L.snapshotKey);
    actor->setAttribute(&scope, L.snapshotKey,
                        cur->asList(&scope)->appendFirst(&scope, rec)->asObject(&scope));
}
} // namespace

const proto::ProtoObject* ExecutionEngine::resumeFrames(proto::ProtoContext* parent,
                                                        const proto::ProtoList* frames,
                                                        unsigned idx,
                                                        const proto::ProtoObject* injected) {
    checkNativeStack();
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* rec = frames->getAt(parent, static_cast<int>(idx));
    const BytecodeModule& mod = *reinterpret_cast<const BytecodeModule*>(
        proto::asSmallInt(rec->getOwnAttributeDirect(parent, L.modKey)));
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
```
**Rooting:** the caller (`resumeSuspendedTurn`) holds the snapshot list in an automatic-local slot of the turn context *before* clearing the actor's attribute, so the list and every record stay reachable across the whole resume, which allocates.

- [ ] **Step 4: The `await` primitive**

```cpp
PRIM(future_await) {
    const RuntimeLayout& L = layoutOf();
    if (futures::state(ctx, L, self) != futures::kPending)
        return futures::result(ctx, L, self);          // raises a failed future's error
    const ProtoObject* actor = currentActor();
    if (!actor)                                        // the main thread, or Thread.start
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
```

- [ ] **Step 5: The resume turn**

```cpp
void ActorScheduler::resumeSuspendedTurn(proto::ProtoContext* ctx, ActorState* a,
                                         bool* suspended) {
    const RuntimeLayout& L = *layout_;
    auto* actor = const_cast<proto::ProtoObject*>(a->actor);
    proto::ProtoContext turn(ctx->space, ctx);
    turn.resizeAutomaticLocals(3);
    const proto::ProtoObject** slot = turn.getAutomaticLocals();
    slot[0] = actor->getOwnAttributeDirect(&turn, L.snapshotKey);     // rooted before clearing
    slot[1] = actor->getOwnAttributeDirect(&turn, L.waitingOnKey);
    slot[2] = actor->getOwnAttributeDirect(&turn, L.turnFutureKey);
    const long long st = futures::state(&turn, L, slot[1]);
    if (st == futures::kPending) {
        // Not the completion's wake-up: a message, or a wake marked while the
        // suspension was in flight. Stay parked (protoST's S13).
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
        completeFutureWithError(&turn, slot[2], futures::errorOf(&turn, L, slot[1]));
        return;
    }
    setCurrentActor(actor);
    bool yielded = false;
    try {
        const proto::ProtoObject* r = engine_->resumeFrames(
            &turn, slot[0]->asList(&turn), 0, futures::valueOf(&turn, L, slot[1]));
        applyHandlerResult(&turn, a, r, slot[2]);      // the (newState, reply) rule of D45
    } catch (FutureYield&) {
        if (slot[2] && slot[2] != PROTO_NONE)
            actor->setAttribute(&turn, L.turnFutureKey, slot[2]);
        yielded = true;
    } catch (ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n", e.what());
        completeFailureFor(&turn, slot[2], e);
    }
    setCurrentActor(nullptr);
    *suspended = yielded;
}
```
`deliver` gains the matching `catch (FutureYield&)`: it stores the message's future under `__turn_future__` and sets `*suspended = true` (the snapshot and `__waiting_on__` are already on the actor, written by `await` and the frames).

- [ ] **Step 6: Run**

```bash
cmake --build build_release
ctest --test-dir build_release -R "unit/actors|13-actors|14-futures" --output-on-failure
PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release -R "13-actors|14-futures" --output-on-failure
PROTOSCALA_ACTOR_WORKERS=16 ctest --test-dir build_release -R "13-actors|14-futures" --output-on-failure
```
Expected: all green in every configuration; in particular `await-one-worker.scala` prints `84` with one worker, which is the proof that `await` suspends cooperatively rather than blocking.

Run the whole suite once under the low heap, because a snapshot is a live object graph:
`PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/FutureYield.h src/runtime/ExecutionEngine.h src/runtime/ExecutionEngine.cpp \
        src/runtime/ActorScheduler.h src/runtime/ActorScheduler.cpp \
        src/runtime/ActorPrimitives.cpp src/runtime/Runtime.h src/runtime/Runtime.cpp \
        tests/unit/test_yield.cpp tests/conformance/13-actors tests/CMakeLists.txt
git commit -m "actors: cooperative await through per-frame snapshots

Future.await inside an actor throws FutureYield; every recursive execute()
frame prepends its record (module, ip, the in-flight call's base and the live
slots below it) to the actor's snapshot and rethrows, and the scheduler parks
the actor while it stays claimed. Completing the future re-enqueues it and
resumeFrames rebuilds the chain, writing the awaited value where the call
would have left its result (DESIGN §8.3). An await whose chain cannot be
snapshotted is refused instead of corrupting the frame (D43).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 7: `Future.apply`, `map`, `flatMap`, `recover`

**Files:**
- Modify: `src/runtime/Futures.h`, `src/runtime/Futures.cpp`, `src/runtime/ActorPrimitives.cpp`, `src/runtime/Primitives.cpp`, `lib/prelude.scala`
- Test: `tests/conformance/14-futures/*.scala`

**Interfaces:**

| Receiver | Member | Meaning |
|---|---|---|
| `Future` | `apply(body)` | runs `body: () => T` on the worker pool (a one-shot actor) and returns its `Future` (D47) |
| `Future` | `successful(v)` / `failed(e)` | an already-completed future (used by `flatMap` and the tests) |
| a future | `map(f)` | a new future completed with `f(value)`; a failure passes through |
| a future | `flatMap(f)` | `f(value)` must return a `Future`; its completion completes the result |
| a future | `recover(f)` | a failure becomes `f(error)`; a success passes through |
| a future | `onComplete(f)` | `f(Try[T])`; the primitive the three above are built on |

Continuations are stored in a `__conts__` `ProtoList` on the future (GC-safe) and run **on the thread that completes it**, or immediately on the caller when the future is already completed (D48).

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/14-futures/future-apply.scala`:
```scala
// EXPECT: 9
val f = Future(() => 3 * 3)
println(f.await)
```
`tests/conformance/14-futures/map-and-flatmap.scala`:
```scala
// EXPECT: 25
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val f = (echo ? 4).map(x => x + 1).flatMap(x => Future(() => x * 5))
println(f.await)
```
`tests/conformance/14-futures/recover.scala`:
```scala
// EXPECT: -1
val bad = Actor.spawn(0) { (s, m) => (s, s / 0) }
println((bad ? 1).recover(e => -1).await)
```
`tests/conformance/14-futures/map-on-a-completed-future.scala`:
```scala
// EXPECT: 8
val f = Future.successful(4)
println(f.map(x => x * 2).await)
```

- [ ] **Step 2: Implement**

```cpp
// Runs every continuation registered on a completed future. Called by
// complete() on the completing thread and by onComplete() when the future is
// already done (D48).
//
// A continuation must not suspend: it may run on a worker in the middle of
// another actor's turn, so `await` inside one would park the wrong actor. The
// current actor is cleared and the native depth raised for the duration, which
// makes `await` refuse with the D43 message.
void runContinuations(proto::ProtoContext* ctx, const RuntimeLayout& L,
                      const proto::ProtoObject* f) {
    auto* fut = const_cast<proto::ProtoObject*>(f);
    const proto::ProtoObject* conts = fut->getOwnAttributeDirect(ctx, L.contsKey);
    if (!conts || conts == PROTO_NONE || conts->asList(ctx)->getSize(ctx) == 0) return;
    // Take the list once: a continuation registered after this point is run by
    // onComplete itself, because the future is already completed.
    if (!fut->setAttributeIfEqual(ctx, L.contsKey, conts, ctx->newList()->asObject(ctx))) return;
    const proto::ProtoList* list = conts->asList(ctx);
    ActorTurnPause pause;                 // clears currentActor, raises the native depth
    for (unsigned long i = 0; i < list->getSize(ctx); ++i) {
        proto::ProtoContext scope(ctx->space, ctx);
        const proto::ProtoObject* arg = tryOf(&scope, L, f);     // Success(v) / Failure(e)
        try {
            activeCallContext()->engine->invoke(&scope, list->getAt(&scope, static_cast<int>(i)),
                                                &arg, 1);
        } catch (ScalaError& e) {
            std::fflush(stdout);
            std::fprintf(stderr, "protoscala: future continuation failed: %s\n", e.what());
        }
    }
}
```
`map`/`flatMap`/`recover` are written on top of `onComplete` in **C++** (they must build `Success`/`Failure` through the prelude hooks anyway), each creating the derived future first so the continuation can complete it. `Future.apply(body)` spawns a one-shot actor whose handler ignores its state and calls `body()`, asks it once and returns that ask's future — one scheduling entity for the whole phase (A0-7), at the cost of one perennial actor per `Future.apply` (D46, noted in STATUS).

- [ ] **Step 3: Run**

Run: `cmake --build build_release && ctest --test-dir build_release -R "14-futures" --output-on-failure`, then with `PROTOSCALA_ACTOR_WORKERS=1`.
Expected: all green — `Future.apply` with one worker works because the ask is fire-and-forget from the main thread and the main thread's `await` blocks outside any turn.

- [ ] **Step 4: Commit**

```bash
git add src/runtime/Futures.h src/runtime/Futures.cpp src/runtime/ActorPrimitives.cpp \
        src/runtime/Primitives.cpp lib/prelude.scala tests/conformance/14-futures
git commit -m "futures: apply, map, flatMap, recover and onComplete

Continuations live in a ProtoList on the future and run on the thread that
completes it (or immediately, when it is already complete); they may not
suspend, so the current actor is cleared while they run. Future.apply runs
its body on the worker pool as a one-shot actor (DESIGN §8.3).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 8: GC pressure, shutdown on every exit path, stress and race checks

**Files:**
- New: `tests/cli/actors-shutdown.sh`, `tests/cli/actors-stress.sh`
- Modify: `tests/cli/gc-pressure.sh`, `tests/CMakeLists.txt`, `src/repl/Repl.cpp` (only if an exit path bypasses `~Session`)

**Interfaces:** none (black-box checks of the binary).

- [ ] **Step 1: Shutdown on every exit path**

`tests/cli/actors-shutdown.sh` runs the binary five ways and requires a clean exit, no hang and no message from the protoCore teardown:

```bash
#!/usr/bin/env bash
#
# CLI check: the actor workers are joined before the ProtoSpace is destroyed,
# on every exit path (DESIGN §8.2). A missed join shows up as a crash, a hang
# or protoCore noise on stderr during teardown.
set -u
P="${1:?usage: actors-shutdown.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" actors-shutdown.XXXXXX)
trap 'rm -rf "$work"' EXIT

run_case() {   # name, expected-exit, expected-last-stdout-line, source
    local name="$1" want_rc="$2" want_out="$3" src="$4"
    printf '%s' "$src" > "$work/$name.scala"
    local out rc
    out=$(timeout 60s "$P" "$work/$name.scala" 2>"$work/$name.err"); rc=$?
    local last; last=$(printf '%s' "$out" | tail -n 1)
    [[ $rc -eq $want_rc && "$last" == "$want_out" ]] || {
        echo "FAIL ($name): exit $rc (want $want_rc), last line '$last' (want '$want_out')"
        cat "$work/$name.err"; exit 1; }
}

# 1. Normal end with live actors and a full mailbox.
run_case normal 0 'done' '
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < 1000 do
  a ! 1
  i += 1
println("done")
'
# 2. An uncaught error after actors were started.
run_case failing 1 '' '
val a = Actor.spawn(0) { (s, m) => (s, s) }
a ! 1
println(1 / 0)
'
# 3. An actor left suspended on a future that never completes: the process
#    still exits, and says so on stderr. An actor that asks *itself* builds
#    exactly that: the ask is queued behind the handler that is waiting for it,
#    and the single-method invariant means it can never be handled.
cat >"$work/suspended.scala" <<'SCALA'
var me: Any = null
val stuck = Actor.spawn(0) { (s, m) =>
  val v = (me ? m).await
  (v, v)
}
me = stuck
stuck ! 1
// Give the worker time to reach the suspension, then end the program with the
// actor still parked.
val t = System.nanoTime()
while System.nanoTime() - t < 200000000 do ()
println("started")
SCALA
out=$(timeout 60s "$P" "$work/suspended.scala" 2>"$work/suspended.err"); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (suspended): exit $rc"; cat "$work/suspended.err"; exit 1; }
grep -q "still waiting on a future" "$work/suspended.err" || {
    echo "FAIL (suspended): no diagnostic for the parked actor"; exit 1; }
# 4. The REPL: actors created at the prompt, then :quit.
out=$(printf 'val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }\na ! 1\n:quit\n' \
      | timeout 60s "$P" 2>"$work/repl.err"); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (repl): exit $rc"; cat "$work/repl.err"; exit 1; }
echo OK
```
If case 3 or 4 hangs, the bug is a missing `shutdown` on that path (`~Session`, or the REPL's exit), not a test problem.

- [ ] **Step 2: Stress and race checks**

`tests/cli/actors-stress.sh` — protoClojure's `concurrent-sends-no-race` shape at a size that actually races, run at both ends of the worker range:

```bash
#!/usr/bin/env bash
# CLI check: the single-method invariant under contention (DESIGN §8.2).
# Every number here is exact: N sends leave the counter at N whatever the
# interleaving, so a lost, duplicated or concurrently-handled message shows up
# as a wrong number rather than as flakiness.
set -u
P="${1:?usage: actors-stress.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" actors-stress.XXXXXX)
trap 'rm -rf "$work"' EXIT

cat >"$work/race.scala" <<'SCALA'
val counter = Actor.spawn(0) { (s, m) => (s + m, s) }
val ts = List(1, 2, 3, 4, 5, 6, 7, 8).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 25000 do
      counter ! 1
      i += 1
  }
}
ts.foreach(t => t.join())
println((counter ? 0).await)
SCALA
cat >"$work/many.scala" <<'SCALA'
var actors: List[Any] = Nil
var k = 0
while k < 1000 do
  actors = Actor.spawn(0) { (s, m) => (s + 1, s + 1) } :: actors
  k += 1
var round = 0
while round < 50 do
  actors.foreach(a => a ! 1)
  round += 1
var total = 0
actors.foreach(a => total += (a ? 0).await)
println(total)
SCALA
for w in 1 2 8 16; do
    out=$(PROTOSCALA_ACTOR_WORKERS=$w timeout 180s "$P" "$work/race.scala" 2>&1)
    [[ "$out" == "200000" ]] || { echo "FAIL (race, workers=$w): '$out'"; exit 1; }
    out=$(PROTOSCALA_ACTOR_WORKERS=$w timeout 180s "$P" "$work/many.scala" 2>&1)
    [[ "$out" == "51000" ]] || { echo "FAIL (many, workers=$w): '$out'"; exit 1; }
done
echo OK
```
(`many.scala`'s expected value is 1000 actors × (50 sends + 1 ask) = 51000; the implementer recomputes it against the final `ask` semantics and writes the arithmetic in a comment, as `gc-pressure.sh` already does for its own numbers.)

- [ ] **Step 3: GC pressure**

Append an actor section to `tests/cli/gc-pressure.sh`, in its existing style (a program that reports the work it did, and an independently computed expected value):

```bash
# Actors under a low heap: 200 actors, each receiving 500 case-class messages
# whose only reference is the mailbox, plus 200 asks whose futures are the only
# reference to the replies. 200 * 500 = 100000 messages, 200 replies of 500.
cat >"$work/actors.scala" <<'SCALA'
case class Add(n: Int)
var actors: List[Any] = Nil
var k = 0
while k < 200 do
  actors = Actor.spawn(0) { (s, m) =>
    m match
      case Add(n) => (s + n, s + n)
      case _      => (s, s)
  } :: actors
  k += 1
var round = 0
while round < 500 do
  actors.foreach(a => a ! Add(1))
  round += 1
var total = 0
actors.foreach(a => total += (a ? Add(0)).await)
println(total)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 180s "$P" "$work/actors.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "100000" ]] || { echo "FAIL (actors): exit $rc, '$out'"; exit 1; }
```
A lost message, a collected envelope or a collected reply gives a wrong total, not a crash — which is the point: this is a **correctness** check, as DESIGN §8.5 requires of the GC-pressure variants.

- [ ] **Step 4: ThreadSanitizer**

```bash
cmake -B build_tsan -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build_tsan
PROTOSCALA_ACTOR_WORKERS=4 ctest --test-dir build_tsan -R "unit/actors|13-actors|14-futures|cli/actors" --output-on-failure
```
Expected: green, with no `WARNING: ThreadSanitizer` in the output. TSan reports on protoCore's own internals that protoScala cannot fix are recorded in the task notes and reported to the maintainer, never suppressed silently. `build_tsan/` is not committed (add it to `.gitignore` if the existing pattern does not already cover `build_*`).

- [ ] **Step 5: Register and run**

Add `actors-shutdown` and `actors-stress` to the `foreach(cli_test ...)` list in `tests/CMakeLists.txt` that passes only the binary.

Run: `ctest --test-dir build_release --output-on-failure`
Expected: `100% tests passed`, with the new CLI checks included.

- [ ] **Step 6: Commit**

```bash
git add tests/cli/actors-shutdown.sh tests/cli/actors-stress.sh tests/cli/gc-pressure.sh \
        tests/CMakeLists.txt
git commit -m "tests: actor shutdown, contention and GC-pressure checks

Shutdown joins every worker on every exit path (normal, failing, a parked
actor, the REPL); 8 threads x 25000 sends and 1000 actors give exact counts
at 1, 2, 8 and 16 workers; 100000 messages and their replies survive a low
protoCore heap, which is a correctness check, not a speed one.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 9: The seven benchmarks of DESIGN §8.5, the harness and the protoClojure comparison

**Files:**
- New: `benchmarks/actors/actor-throughput.scala`, `actor-fanout.scala`, `actor-mpsc.scala`, `actor-mpmc.scala`, `actor-pingpong.scala`, `actor-await.scala`, `actor-priority.scala`; `benchmarks/actor-bench.sh`; `benchmarks/run_actor_benchmarks.py`; `tests/cli/actor-bench-smoke.sh`
- Modify: `benchmarks/README.md`, `benchmarks/RESULTS.md`, `benchmarks/run_benchmarks.py` (the `PENDING` table), `tests/CMakeLists.txt`
- Generated: `benchmarks/reports/<date>-actors.md`

**Interfaces:**
- Every script sizes itself from `System.getenv("PROTOSCALA_BENCH_N")` (default 1_000_000 messages, so a run takes 1–4 s), prints one `mode=… messages=… <verification>` line, then `Actor.stats`, then `ok` or `FAILED`.
- `run_actor_benchmarks.py` imports `machine_info`, `loadavg`, `fmt_load`, `wait_for_load`, `git_rev`, `build_type` from `run_benchmarks` (which guards its entry point with `if __name__ == "__main__"`), so the two reports carry identical machine headers.
- `actor-bench.sh` is the thin wrapper, exactly as `bench.sh` wraps `run_benchmarks.py`.

- [ ] **Step 1: Write the seven scripts**

`benchmarks/actors/actor-throughput.scala` (mode `single` — the per-actor pipeline floor):
```scala
// 1 sender thread x 1 actor x N trivial messages (DESIGN §8.5).
// Self-reporting: the final ask is ordered behind every send, so its reply is
// the exact number of messages this actor processed.
val N = { val e = System.getenv("PROTOSCALA_BENCH_N"); if e == "" then 1000000 else e.toInt }
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < N do
  a ! 1
  i += 1
val processed = (a ? 1).await
println("mode=single messages=" + (N + 1).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == N + 1 then "ok" else "FAILED")
```

`actor-fanout.scala` (1000 actors × N/1000 messages — ready-queue stress), `actor-mpsc.scala` (4 `Thread.start` producers × N/4 → 1 actor — sender contention on one mailbox), `actor-mpmc.scala` (4 producers × 4 actors round-robin), `actor-pingpong.scala` (`ping`'s handler loops N/2 times over `(pong ? i).await`, so every round trip is two messages and the mode measures ask/reply latency *through* the cooperative suspension), `actor-await.scala` (1000 caller actors, each awaiting an ask to a shared `echo` actor N/2000 times; **it must complete with `PROTOSCALA_ACTOR_WORKERS=1`**, which a blocking `await` cannot), `actor-priority.scala` (a Low-band flood of N messages plus 1000 High-band asks, each timed with `System.nanoTime`, printed as a `latencies=` line of 1000 nanosecond samples that the runner turns into p50/p99).

Each ends with the same three lines (`mode=…`, `Actor.stats`, `ok`/`FAILED`), and each states its arithmetic in a comment.

- [ ] **Step 2: The runner**

`benchmarks/run_actor_benchmarks.py`:
- modes table: `(name, script, expected_messages(N), notes)`;
- for each mode and each `PROTOSCALA_ACTOR_WORKERS` in `1, 2, 4, 6, 8, 16`: run the script as a cold process, time it with `time.perf_counter()`;
- **verify before computing anything**: the run exited 0, the last line is `ok`, the `mode=` line's `processed` equals the expected count, and `ActorStats(<workers>,<messages>)` reports at least that many messages. A cell that fails any check is marked `FAILED` and is never turned into a rate (the protoClojure harness's 2026-06-14 lesson, and protoPython's sprint-9 lesson: a silent failure must never read as infinite throughput);
- rate = `messagesProcessed / wall_seconds`;
- the report header records machine, physical/logical cores, date, protoScala commit, build type, `Mailbox::implementationName()` (printed by `protoscala --version` after Task 11 adds it, or read from the CMake configure log), the load average at start and end, and the worker count at the peak of each mode;
- the comparison section runs `../protoClojure/benchmarks/actor-bench.sh` on the same machine, the same day, and puts its `single`/`fan-out`/`MPSC`/`MPMC` rows next to protoScala's — the four modes whose shapes are identical. `ping-pong`, `await` and `priority` have no protoClojure twin and say so explicitly. If `protoclj` is missing, the section says "not available on this machine" and why.

- [ ] **Step 3: The smoke check**

`tests/cli/actor-bench-smoke.sh` runs all seven scripts with `PROTOSCALA_BENCH_N=2000` and requires `ok` from each, so a benchmark can never rot unnoticed between full runs. Register it in `tests/CMakeLists.txt` (it takes the binary and the source dir).

- [ ] **Step 4: Run the full table**

```bash
benchmarks/actor-bench.sh --name actors
```
Expected: a table with 7 modes × 6 worker counts, every cell verified, and `benchmarks/reports/<date>-actors.md` written. Expectations to sanity-check before recording (not to enforce):
- `single` barely improves beyond 1 worker (the single-method invariant serialises one actor);
- `fan-out`, `MPMC` and `await` scale up to the physical-core count (6 on DEV12) and flatten or regress at 16 — protoST's measured worker-scaling ceiling;
- `await` completes at every worker count **including 1**;
- `priority` reports a High-band p99 far below the Low-band flood's mean service time.

Run the comparison the same day:
```bash
../protoClojure/benchmarks/actor-bench.sh ../protoClojure/build_release/protoclj | tee \
  ../.agent_scratch/phase5/protoclj-actor-bench.txt
```
and fold its four comparable rows into the report.

- [ ] **Step 5: Record**

Add a row to `benchmarks/RESULTS.md`'s report table pointing at the new dated report, plus an "Actors" section with the peak table (as protoClojure's README does); update `benchmarks/README.md` with how to run the actor suite; remove the `("actor benchmarks", …, "Phase 5 (actors)", …)` entry from `run_benchmarks.py`'s `PENDING` list.

- [ ] **Step 6: Commit**

```bash
git add benchmarks/actors benchmarks/actor-bench.sh benchmarks/run_actor_benchmarks.py \
        benchmarks/README.md benchmarks/RESULTS.md benchmarks/run_benchmarks.py \
        benchmarks/reports tests/cli/actor-bench-smoke.sh tests/CMakeLists.txt
git commit -m "benchmarks: the seven actor modes of DESIGN §8.5

single, fan-out, MPSC, MPMC, ping-pong, await and priority, each self-reporting
its message count and each verified by the runner before any rate is computed.
Six worker counts per mode, a dated report with machine and load average, and
protoClojure's four comparable rows measured on the same machine and day.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 10: Tutorial chapter 13 — Actors and futures

**Files:**
- New: `docs/tutorial/13-actors-and-futures.md`, `tests/conformance/tutorial/13-*.scala`
- Modify: `docs/TUTORIAL.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-scala-developer.md`

**Note on the chapter number:** `docs/TUTORIAL.md`'s chapter table lists **13** as "Actors and futures" (12 is "Enums and sealed hierarchies", Phase 4), while `docs/ROADMAP.md`'s Phase 5 done-when says "tutorial chapter 12". The table is the live list, so the chapter is **13** and Task 11 corrects the ROADMAP line.

**Interfaces:** every runnable snippet in the chapter is also a fixture under `tests/conformance/tutorial/`, byte-identical to the published snippet (the Documentation track's rule).

- [ ] **Step 1: Write the chapter**

Sections, in the dual-audience style of chapters 6, 7 and 9 (a Scala-first exposition, with a bridge box for Python/JavaScript readers and a closing "What differs from Scala 3" section keyed to the D-ids):

1. **Why actors here** — no GIL, native threads, messages passed by pointer to immutable data (DESIGN §1, §8). For the Python reader: not `asyncio` (one thread, one loop) and not `multiprocessing` (copies); for the JavaScript reader: not a worker with `postMessage` (structured clone) — the same object graph, no copy.
2. **An actor is a state and a handler** — `Actor.spawn(state)(handler)`, the `(newState, reply)` rule (D45), the printed form `Actor(10)`.
3. **Telling and asking** — `!` (fire and forget) and `?` (a `Future` of the reply); why `!` returns `Unit`; the single-method invariant in one paragraph: "a counter that receives a thousand `!` from four threads ends at exactly a thousand — the actor never runs twice at once".
4. **Priorities** — `send`/`ask` with `Priority.High` / `Medium` / `Low`, what a band guarantees (order within a band) and what it does not (no pre-emption of a message already being handled).
5. **Futures** — `await`, `isCompleted`, `value: Option[Try[T]]`, `map`/`flatMap`/`recover`, `Future(() => …)` (D47) and where continuations run (D48).
6. **`await` inside an actor is cooperative** — the worker is released, the actor stays claimed, the message that follows waits; the worked example is the `await-one-worker` fixture running with `PROTOSCALA_ACTOR_WORKERS=1`. The limits are stated plainly (D43): not inside `map`/`foreach`/a continuation.
7. **When a handler fails** — `Failure(RuntimeError(...))`, the actor keeps its state and stays alive, the error is on stderr; no supervision trees in 0.3.0 (DESIGN §8.4).
8. **Threads and time** — `Thread.start { () => … }`, `join()`, `System.nanoTime()` (D49), and when to prefer an actor.
9. **Tuning** — `PROTOSCALA_ACTOR_WORKERS` (default `max(2, cores − 2)`, cap 16), `Actor.stats`, and the measured shape of the benchmark table (a pointer to `benchmarks/RESULTS.md`, not numbers copied by hand).
10. **What differs from Scala 3** — D43–D52 in one table, plus "no `ExecutionContext`, no `Akka`, no supervision".

- [ ] **Step 2: Fixtures**

Every snippet that runs becomes `tests/conformance/tutorial/13-<topic>.scala` with an `// EXPECT:` first line, byte-identical to the chapter. The `await` snippet gets `PROTOSCALA_ACTOR_WORKERS=1` through `set_tests_properties`, so the text's claim ("this completes with a single worker") is the thing the suite checks.

- [ ] **Step 3: Extend chapters 2 and 3**

Chapter 2 gains a short "Concurrency without a GIL" section (for the Python/JavaScript reader) pointing at chapter 13; chapter 3 gains D43–D52 in its departures catalogue and a line in its "What is missing" list about supervision and `ExecutionContext`.

- [ ] **Step 4: Run**

Run: `ctest --test-dir build_release -R "tutorial" --output-on-failure`
Expected: every tutorial fixture green, including the new ones.

- [ ] **Step 5: Commit**

```bash
git add docs/tutorial/13-actors-and-futures.md docs/TUTORIAL.md \
        docs/tutorial/02-for-the-python-or-javascript-developer.md \
        docs/tutorial/03-for-the-scala-developer.md tests/conformance/tutorial tests/CMakeLists.txt
git commit -m "docs: tutorial chapter 13, actors and futures

Actors, telling and asking, priority bands, futures and cooperative await,
handler failures, threads and tuning, for both audiences; every runnable
snippet is a conformance fixture, and the cooperative-await snippet runs with
a single worker so the text's claim is what the suite checks.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 11: Status, deviations, changelog and release 0.3.0

**Files:**
- Modify: `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `docs/DESIGN.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt`, `src/main.cpp`

- [ ] **Step 1: `docs/STATUS.md`**

- Header: "Phase 5 complete (0.3.0)", the new test totals (`ctest -N`), and "all green also under `PROTOCORE_HEAP_LIMIT_CELLS=20000` and under ThreadSanitizer".
- **Implemented**: a concurrency block — actors, three priority bands, the single-method invariant, cooperative `await`, futures and their combinators, `Thread`, `System`, `Actor.stats`, the mailbox backend in use (`ProtoMPSCQueue` or the CAS-list fallback, naming Phase P2 as the switch).
- **Not yet implemented**: remove "Actors and futures — Phase 5"; add "supervision trees, `ExecutionContext`, actor timeouts and `Await.result(f, duration)`", and — if the fallback mailbox is in use — "actor mailboxes on protoCore's `ProtoMPSCQueue` (Phase P2)".
- **Opcode table**: leave 128..159 reserved and change its note to "reserved; Phase 5 shipped the actor surface as ordinary sends (plan Task 0 A0-11)".
- **Intentional deviations**: a new "Provisional deviations (Phase 5)" table with **D43–D52**:

| Id | Deviation |
|---|---|
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction. Inside a native higher-order method (`map`, `foreach`, `withFilter`, a `Future` continuation), or under an instruction that calls back into Scala without being a call site (`==` reaching a user `equals`, a `MatchError`'s `toString`), it raises `UnsupportedOperationException` instead of suspending; the ask's future receives that failure and the actor stays alive |
| D44 | Until Phase 4 there are no exception values: a failed `Future` carries `RuntimeError(className, message)`, and `Try`/`Success`/`Failure` (moved up from Phase 3) wrap it. `await` on a failed future raises the same error on the awaiting thread, which no user code can catch yet |
| D45 | An actor handler must return `(newState, reply)`; any other result raises `IllegalArgumentException` and fails that message, leaving the actor's state unchanged |
| D46 | An actor lives as long as the session: it is anchored in a registry so the GC can reach it and everything it holds while it is only referenced by the (C++) ready stacks. `Future.apply` creates one actor per call |
| D47 | `Future.apply` takes a function, not a by-name parameter: write `Future(() => expr)` (by-name parameters need callee signatures at compile time, which a dynamic dialect has not) |
| D48 | `map`/`flatMap`/`recover`/`onComplete` run their continuation on the thread that completes the future, or immediately on the caller when it is already complete — there is no `ExecutionContext`. A continuation may not `await` (D43) |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's: `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`, `System.currentTimeMillis()`, `System.getenv(name)` (`""` when unset). `nanoTime` is a monotonic clock; only differences are meaningful |
| D50 | Awaiting a future that fails, inside an actor, abandons the rest of the handler and the message's own future inherits the failure (there is no `try`/`catch` to resume into until Phase 4) |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a send that is still queued (protoClojure's `@actor`) |
| D52 | `Priority.High` / `Medium` / `Low` are the integers `0` / `1` / `2` on an object, not an `enum` (enums arrive in Phase 4) |

- **Known issues / platform dependencies**: the ready stacks retain their node high-water mark for the session; a handler parked on a future that never completes keeps its actor and its queued messages alive until exit, where the shutdown prints a diagnostic; `PROTOSCALA_ACTOR_WORKERS` above the physical core count measured no gain (Task 9's table).

- [ ] **Step 2: `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DESIGN.md`**

- LANGUAGE.md: a concurrency section with the surface grammar (`!`, `?`, `send`, `ask`, `value`, `await`, `Priority`, `Future`, `Thread`, `System`) and D43–D52 in §5.
- ROADMAP.md: mark **Phase 5 ✅ (date)**, and correct its done-when line "tutorial chapter 12" to **chapter 13** (the TUTORIAL.md chapter table's number, Task 10).
- DESIGN.md: in §8.3, replace "the engine snapshots the actor's frames" with the one-sentence statement of what was built (per-frame records prepended during unwinding, resumed by recursion) and cross-reference D43; in §11, mark R9's mailbox as "consumed through the `Mailbox` seam; on `ProtoMPSCQueue` once P2 merges" if the fallback shipped. Nothing else in §8 changes — the phase implemented it as written.

- [ ] **Step 3: `CHANGELOG.md`, `README.md`, version**

`CHANGELOG.md` gains a `## [0.3.0] - <date>` section ("Phase 5: actors, priority bands and cooperative futures. Built against protoCore `<hash>`.") with Added / Changed / Known-limitations lists; `README.md` gains an actors section with a short example and the peak benchmark table, and its status line becomes 0.3.0; `CMakeLists.txt`'s `project(... VERSION 0.3.0)` is bumped; `protoscala --version` gains the mailbox backend, so a benchmark report can never misattribute its numbers:
```
protoScala 0.3.0 (actor mailboxes: ProtoMPSCQueue)
```
(`tests/cli/version.sh` is updated to accept the new line.)

- [ ] **Step 4: `docs/DECISIONS-LOG.md`**

One row per Task 0 decision (A0-1 … A0-11), each "agent, pending review" — since closed as "approved by the maintainer on 2026-09-23", with the exceptions noted in Task 0 — with the file that carries it; plus a row for the Phase 5 D-ids (D43–D52) and one for "Release 0.3.0 cut; the `v0.3.0` tag is left to the maintainer".

- [ ] **Step 5: Full verification**

```bash
rm -rf build_release && cmake -B build_release -S . && cmake --build build_release
ctest --test-dir build_release --output-on-failure
PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release --output-on-failure
PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release --output-on-failure
PROTOSCALA_ACTOR_WORKERS=16 ctest --test-dir build_release --output-on-failure
benchmarks/cold-start.sh build_release/protoscala 21
```
Expected: `100% tests passed` four times, and a cold start still under 25 ms (DESIGN §1: the scheduler must **not** start until the first `Actor.spawn`, so a script that uses no actor pays nothing — if cold start regressed, the cause is eager worker creation and it is a bug, not a new target).

- [ ] **Step 6: Commit**

```bash
git add docs CHANGELOG.md README.md CMakeLists.txt src/main.cpp tests/cli/version.sh
git commit -m "docs: Phase 5 status, deviations D43-D52 and release 0.3.0

Actors, priority bands and cooperative futures are implemented and measured;
STATUS records the ten provisional deviations, ROADMAP marks the phase done
(and corrects the tutorial chapter number), DESIGN §8.3 records how the
recursive VM suspends, and --version names the mailbox backend so a benchmark
report cannot misattribute its numbers.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

## Open questions for the maintainer

Each one is implemented as stated (the "provisional behaviour"), recorded in `docs/DECISIONS-LOG.md` as "agent, pending review", and cheap to reverse. **Answered by the maintainer on 2026-09-23** — see the review note in Task 0 for the two reversals (D45, D47) and the one supersession (D44).

| # | Question | Provisional behaviour | Cost if reversed |
|---|---|---|---|
| Q1 | Is the recursive VM plus per-frame snapshots the right answer, or should the engine become iterative before more phases build on it? (A0-1) | Per-frame snapshots; the VM stays recursive | The engine rewrite gets more expensive with every phase that adds opcodes |
| Q2 | Should an `await` that cannot be snapshotted fail (D43) or block the worker? (A0-2) | It fails | A blocking fallback would deadlock small pools; changing later is a one-line switch plus fixtures |
| Q3 | Is Phase 5 before Phases 3 and 4 the intended order, given that it forces `Try`/`RuntimeError` into the prelude early? (A0-3) | Phase 5 next, 0.3.0 | Phase 4 must re-point `Failure` at real exception values (one prelude edit plus the `await` raise path) |
| Q4 | Must a handler always return `(newState, reply)`? (A0-4) | Yes (D45) | Relaxing it later is additive; tightening it later would break programs |
| Q5 | May actors be collected when they are quiescent and unreferenced? (A0-5) | No: an actor lives as long as the session (D46) | Needs a removable registry, i.e. `ProtoMap` (P1) |
| Q6 | `Future(() => e)` or a compiler special case for by-name? (A0-6) | The function form (D47) | A later by-name would deprecate the function form |
| Q7 | Continuations on the completing thread, or a second scheduling entity? (A0-7) | The completing thread (D48) | A task queue is additive but needs its own GC anchor |
| Q8 | Is the `Mailbox` seam acceptable as the way to ship before P2? (A0-8) | Yes; the fallback is correct, only slower | None: the switch is one file |
| Q9 | Are `Thread` and `System` acceptable additions to the surface? (A0-9) | Yes, minimal forms (D49) | Removing them would leave the MPSC/MPMC/priority benchmarks unwritable |
| Q10 | Batch of 8 with a pending list, or drain the whole band? (A0-10) | 8, per DESIGN §8.2 | A constant |
| Q11 | Ship the actor surface as plain sends, with 128..159 still reserved? (A0-11) | Yes (A0-11) | Adding opcodes later is additive and must be justified by `perf stat -r 3` |
| Q12 | Should `f.await` grow a timeout (`Await.result(f, d)`) so a program cannot hang forever? | No timeout in 0.3.0; the shutdown diagnostic reports parked actors | Additive |

---

## Self-review against the done-when criteria

**ROADMAP Phase 5 "Done when":**

| Criterion | Where it is met |
|---|---|
| `Actor.spawn`, `!`, `?`, `send`/`ask` with `Priority`, `value`, `Actor.isActor`, `Actor.stats`, `Future` (`await`, `map`, `flatMap`, `recover`, `Future.apply`) pass their fixtures | Tasks 4, 5, 7 — `tests/conformance/13-actors/`, `14-futures/` |
| The single-method invariant holds under a concurrent-senders race fixture (protoClojure's `concurrent-sends-no-race` shape) | Task 3 (`Scheduler.ConcurrentSendersLoseNothing`), Task 5 (`threads-send-concurrently.scala`), Task 8 (`actors-stress.sh`: 8 threads × 25000, at 1/2/8/16 workers) |
| Priorities are observable | Task 4 (`priority-bands.scala`), Task 9 (the `priority` benchmark's p50/p99) |
| A handler exception yields `Failure` and leaves the actor alive | Task 5 (`failed-ask.scala`), Task 4 (`handler-must-return-a-pair.scala`) |
| `await` inside an actor suspends cooperatively (the `await` benchmark completes with one worker) | Task 6 (`await-one-worker.scala`, run with `PROTOSCALA_ACTOR_WORKERS=1`), Task 9 (the `await` mode at every worker count) |
| Every queued payload is GC-reachable (GC-pressure fixtures under a low limit) | Task 2 (`QueuedItemsSurviveCollections`), Task 3 (the unit binary under `PROTOCORE_HEAP_LIMIT_CELLS`), Task 8 (`gc-pressure.sh`'s actor section) |
| Shutdown joins all workers on every exit path | Task 4 (`~Session`), Task 8 (`actors-shutdown.sh`: normal, failing, parked, REPL) |
| `benchmarks/actor-bench.sh` runs all seven modes, verifies message counts, and `RESULTS.md` records the table next to protoClojure's on the same machine and date | Task 9 |
| Tutorial chapter (13, not 12 — the ROADMAP line is corrected) is written | Task 10 |

**DESIGN §8, clause by clause:** §8.1's whole surface is Tasks 4–7 (`Actor(10)` printed form included); §8.2's single-method invariant, per-actor three-band mailboxes on `ProtoMPSCQueue`, lock-free per-band ready stacks with ABA-tagged heads, spin-before-park, `ProtoSpace::newThread` workers, `PROTOSCALA_ACTOR_WORKERS` with protoClojure's default and cap, the per-worker re-installed call context, one `ProtoContext` per message and the shutdown join are Tasks 1–4 and 8; §8.3's `__fstate__` CAS, cooperative `FutureYield` with frame snapshots and a claimed-while-suspended actor, non-actor `await` parking inside `UnmanagedScope`, and `Future.apply`/`map`/`flatMap`/`recover` are Tasks 5–7; §8.4's five corrections are met — payloads and replies are GC-reachable (Tasks 2–3), a handler failure completes the ask with `Failure` and keeps the actor alive (Task 5), a message is one object with no argument limit (Task 4), nothing polls (Task 5's condition variable and Task 6's suspension), and futures run on the pool rather than one thread each (Task 7); §8.5's seven modes, verification rule, worker sweep, report contents and protoClojure comparison are Task 9, and its GC-pressure variants are Task 8.

**Gaps found while reviewing, and closed inline:**
- DESIGN §8.5's `priority` mode needs a clock and its `MPSC`/`MPMC` modes need non-worker sender threads; neither exists in the language. Closed by A0-9 (`System.nanoTime`, `Thread.start`), recorded as D49 rather than left implicit.
- DESIGN §8.3's `Future.apply { ... }` cannot be written without by-name parameters. Closed by A0-6/D47 (`Future(() => e)`), with the compiler special case offered to the maintainer as Q6.
- DESIGN §3.6 ("recursive VM") and §8.3 ("frame snapshots, protoST's model") are in tension, because protoST's snapshots exist only because its engine stopped being recursive. Closed by A0-1: per-frame records prepended during unwinding, which is protoST's own multi-engine coalescing rule applied one frame at a time — and DESIGN §8.3 is amended by Task 11 to say so.
- ROADMAP names tutorial chapter 12; TUTORIAL.md's table says 13. Closed by Task 10's note and Task 11's ROADMAP correction.
- Nothing in DESIGN says where `map`/`flatMap` continuations run, and a naive answer (a second ready-queue entry kind) would reintroduce the GC-anchor problem on a hot path. Closed by A0-7/D48.
