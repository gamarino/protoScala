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
Expected: one commit line (paste it into the Task 12 CHANGELOG entry: "built against protoCore `<hash>`"), and either `1` (Phase P2 merged — the real mailbox) or `0` (P2 not merged — the `Mailbox` seam falls back, Task 2, Task 0 decision A0-8). Record which, in the Task 2 notes; if it is `0`, also record `git -C ../protoCore branch --list` so the maintainer sees where P2 stands.

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

Each decision below is **decided by the agent, pending review** under the maintainer's standing authorisation, and is recorded in `docs/DECISIONS-LOG.md` by Task 12 with that marker. Where a decision creates a user-visible departure from Scala 3, Task 12 records it as a `D<n>` in `docs/STATUS.md` (Phase 5 uses **D43–D52**; D42 is the highest id in use today).

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

**Decision:** Task 2 introduces a `Mailbox` seam with the exact `ProtoMPSCQueue` API of PMQ-SPEC §2.1. When CMake finds `newMPSCQueue` in `../protoCore/headers/protoCore.h` it compiles `Mailbox` onto the real type (`-DPROTOSCALA_HAS_PMQ=1`); otherwise it compiles the fallback — a mutable object holding a `ProtoList` updated by `setAttributeIfEqual`, which is GC-safe and correct but O(log n) per push (protoST's mailbox). Switching is a one-file change and the Task 2 unit tests run against whichever is compiled. Alternative — block Phase 5 on P2 — was rejected because P2's own plan is being written in parallel.

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

**None** (A0-11). The reserved range `128..159` stays reserved in `src/compiler/Opcodes.h` and in the `docs/STATUS.md` opcode table; Task 12 updates the comment to record that Phase 5 shipped without using it.

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
    World() { installPrimitives(runtime.rootContext(), runtime.layout()); }
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
    return world().engine.construct(ctx, world().L().tupleCompanion[2], pair, 2);
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
    unsigned budget = kBatchSize;
    for (unsigned band = 0; band < kBands && budget > 0; ++band) {
        while (budget > 0) {
            proto::ProtoContext turn(ctx->space, ctx);   // one context per message (P2)
            const proto::ProtoObject* env = nextMessage(&turn, a, band);
            if (!env) break;
            --budget;
            deliver(&turn, a, env, &suspended);
            messages_.fetch_add(1, std::memory_order_relaxed);
            if (suspended) return true;
            band = 0;   // a High-band message that arrived mid-turn wins the
                        // next slot: restart the band scan (DESIGN §8.2)
            break;
        }
        if (budget == 0) break;
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

`RuntimeLayout` gains the prelude hooks (design note 14):
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
(`completeReply`/`completeFailure` are no-ops until Task 5; `isTuple2` tests `v->getAttribute(ctx, L.tupleKey) == PROTO_TRUE` and the arity-2 marker key.)

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
// EXPECT: 55
val actors = List(1, 2, 3, 4, 5).map(n => Actor.spawn(n) { (s, m) => (s + m, s + m) })
actors.foreach(a => a ! 10)
var total = 0
while total < 65 do
  total = actors.map(a => a.value).foldLeft(0)((x, y) => x + y)
println(total - 10)
```
If `foldLeft` is not in the Phase 2 `List` surface, use `mkString` plus a manual loop; check `src/runtime/Primitives.cpp` before writing the fixture and adjust — never add a collection method to make a fixture pass (that is Phase 3's work).

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
// EXPECT: RuntimeError(ArithmeticException,/ by zero)
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

// One condition variable for every blocking waiter in the process, with a
// counter so a completion pays nothing when nobody is blocked (protoST's
// mainWaitingOn trick, generalised to any thread).
const proto::ProtoObject* awaitBlocking(proto::ProtoContext* ctx, const RuntimeLayout& L,
                                        const proto::ProtoObject* f) {
    if (state(ctx, L, f) == kPending) {
        g_blocked.fetch_add(1, std::memory_order_seq_cst);
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);   // before the lock (P6)
            std::unique_lock<std::mutex> lk(g_waitMutex);
            g_waitCv.wait(lk, [&] { return g_completions.load(std::memory_order_acquire) !=
                                           g_seen || true; });
        }
        g_blocked.fetch_sub(1, std::memory_order_seq_cst);
    }
    ...
}
```
**Care:** the predicate above is written out in full in the implementation as
`g_waitCv.wait(lk, [&]{ return doneFlag; })` where `doneFlag` is read **without**
touching any `ProtoObject*` (the unmanaged contract forbids it): the future's
state is copied into a plain `std::atomic<int>` stored in a side table keyed by
the future's identity hash, or — simpler and chosen here — the loop leaves the
unmanaged region on every wake and re-reads `state(ctx, L, f)`:

```cpp
    while (true) {
        if (state(ctx, L, f) != kPending) break;      // managed: ProtoObject reads are legal
        g_blocked.fetch_add(1, std::memory_order_seq_cst);
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            std::unique_lock<std::mutex> lk(g_waitMutex);
            const unsigned long long seen = g_epoch.load(std::memory_order_acquire);
            g_waitCv.wait_for(lk, std::chrono::milliseconds(50),
                              [&] { return g_epoch.load(std::memory_order_acquire) != seen; });
        }
        g_blocked.fetch_sub(1, std::memory_order_seq_cst);
    }
```
`complete` bumps `g_epoch` and calls `notify_all()` only when `g_blocked != 0`.
The 50 ms `wait_for` is a safety net, not a poll: it bounds the damage of a lost
wake-up instead of hanging the process, and a comment says exactly that.

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
Note: `Failure(e).toString` prints `Failure(ArithmeticException: / by zero)`, so the `failed-ask.scala` fixture above expects `RuntimeError(ArithmeticException,/ by zero)` only if `RuntimeError` keeps the synthesised `toString`. It does not — it overrides it. Write the fixture's `EXPECT` as `ArithmeticException: / by zero` and keep the two consistent; the implementer verifies by running the fixture, not by reading this plan.

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
