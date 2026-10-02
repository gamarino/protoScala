# protoScala — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`. This file
records protoScala's **first-run result**, the judgement answers a script cannot
give, and what each capability of the adaptor does and does not prove.

- Adaptor: `tests/unit/ConformanceHost.h`
- Wiring: `tests/unit/ConformanceTests.cpp`, `tests/unit/ConformanceIsolate.cpp`
- Static ratchet: `conformance-allow.txt`
- Run: `ctest --test-dir build_release -R embedder-conformance < /dev/null`

**The other conformance measurement** — protoScala against the Scala 3 compiler's
own `tests/run` corpus, the source of the in-scope 31.8 % and the whole-corpus
13.1 % — lives in [`tools/corpus/`](../tools/corpus/) with its scoring rules, its
caveats and the summary of the run behind those figures;
`python3 tools/corpus/score.py --csv --both` re-derives them. Until 2026-09-27 that
harness was outside the repository and the rate could not be reproduced from a
clone, which is the same defect as quoting a total with no command. The corpus
itself is third-party and is fetched rather than vendored (pinned commit, clone
recipe in `tools/corpus/README.md`), so reproducing a *fresh* measurement needs
network access; re-deriving the published one does not.
[docs/STATUS.md](STATUS.md) carries the per-track numbers.

## First run — 2026-09-25, protoCore at `feature/embedder-conformance-p4`

| Case | Rule | Result | Numbers the case reported |
|---|---|---|---|
| `gc.young_submitted` | 1 | **PASS** | grew in-use by 363,162 cells; 3 cycles reclaimed 360,082; residual 3,080 |
| `gc.transient_reclaimed` | 5 | **PASS** | grew 305,818; residual −264 (`List` is a `ProtoList`, not a `ProtoTuple`) |
| **`gc.host_stress`** | **3** | **PASS** (was FAIL) | pre-fix it aborted under its 400,000-cell ceiling having reclaimed 0. **Fixed** — see the resolved retention finding below. The pre-fix live sets that are actually *measured* are the mutation experiment's, **325,030 / 313,358 / 307,125 cells, 3 runs of 3** ("Mutation", below); the single figure this row used to quote ("316,230") matched no run on record and is **withdrawn rather than replaced**, because the case reports no live set once it passes. Green now, and the numbers it does report are run-variant: 20/20 stress rounds, `cycles` **23 / 24 / 22** and `reclaimedSum` **4,318,490 / 3,835,270 / 4,502,298** over three consecutive runs, 2026-09-25, protoCore 2.5.0 |
| `symbol.fast_path_key_hits` | 4 | **PASS** | a 31-byte key is readable through both `getAttribute` and `getOwnAttributeDirect` |
| `external.finalizer_runs` | 7 | **PASS** | kernel characterisation: one call, right pointer |
| `module.root_survives_cycle` | 9b | **PASS** | kernel characterisation: module and contents survived 3 cycles |
| `module.alias_rejected` | 9c | **NEEDSREVIEW** | the kernel keeps provider+path+version distinct; protoScala's own keying is unchecked |
| `external.bytes_accounted` | 7 | **NEEDSREVIEW** | no external byte total — see C7 |
| `thread.registered` | 11 | **NOTAPPLICABLE** | `forEachThreadKind` not implemented — see below. **The rule is UNVERIFIED by this case** |
| `stw.quorum_completes` | 2 | **PASS** (isolated) | a cycle completed with an actor worker idle |
| `join.parks` | 2b | **PASS** (isolated) | a cycle completed while `Thread.start`/`t.join()` was blocked |
| **`heap.ceiling_progress`** | **8** | **PASS** (isolated, was FAIL) | same cause, same fix. **Intermittent: about 2 runs in 10 alone**, for a second defect the retention had been masking — a protoCore `ProtoMPSCQueue` finding, diagnosed and not fixed here. See below |

**Observation added 2026-09-27, on CI rather than on this machine: `join.parks` is
intermittent on a 4-vCPU GitHub runner.** The isolate job (informational, never
gates) ran the three isolated cases on three consecutive pushes: `join.parks`
**failed at 10.0 s in two of the three** (runs `36361202315` and `36362507915`) and
passed in the other (`36360213564`), while `heap.ceiling_progress` — the case
documented as the ~2-in-10 flake — passed in all three. The failure is the case's
own verdict, not a ctest timeout: `protoCore/conformance/CaseThreads.cpp` gives the
collector **8 seconds** to complete a cycle while a `Thread.start`/`t.join()` is
blocked, and on a 4-vCPU runner it sometimes does not. Whether that is a rule-2b
weakness under CPU scarcity or a deadline too tight for a small host is **not
diagnosed here**, and the row above (PASS, isolated, on a 12-CPU host) stands as
measured. It is recorded because nothing in this file prepared a reader for seeing
that case red.

**Diagnosed 2026-10-02: the host's thread finished too early, not the join.** The
case demands its collection 150 ms after it calls `joinBlockingThread`. protoScala's
host ran a fixed 300,000-iteration loop, about 245 ms on the 12-CPU host but under
150 ms on a GitHub runner, so the join had already returned when the collection was
demanded; the case's main thread then waited in an unbracketed `std::thread::join`
of its own 10 s timer, no cycle could complete, and the case reported a rule-2b
failure (`runningThreads=1`, `gcCycleCount 0->0`, 10.0 s: exactly the CI log of runs
2026-09-30 to 2026-10-02, against protoCore 2.7.0 and again 2.9.4). Reproduced
locally by cutting the loop to 30,000 iterations: FAIL 3 of 3. The host now spins
for two seconds of wall clock instead, so the join is blocked when the collection
is demanded: PASS 20 of 20 locally against protoCore 2.9.4 and 3 of 3 against
2.7.0. protoScala's join was never at fault: `t.join()` reaches `ProtoThread::join`,
which parks. That a case whose runtime thread ends early reports a rule-2b failure
rather than NotApplicable is a protoCore conformance-case issue, reported there.

First run: **two cases red, both the same defect**, with **no previously-passing
test newly failing**. After the fix the whole suite was green, and the retention
the two cases measured is gone rather than reduced. Read the rule-8 section before
trusting that: the case is intermittent for a reason that is not protoScala's.

The policy on the suite total: it is **never quoted bare**, because it moves with
every fixture added and with protoCore's own rule list — the total recorded at the
first run (1263) had already drifted twice by the next day. Every total in this
repository therefore carries its date, the protoCore version behind it and its
composition, and the primary instruction is the command:

```bash
ctest --test-dir build_release -N | tail -1        # the total
ctest --test-dir build_release < /dev/null         # the result
```

**As of 2026-09-27, against protoCore 2.5.0 (`df8406a3`): 2313 registered cases.**
On the current `main`, CI — which builds protoCore, protoST and protoScala from
clean on `ubuntu-24.04` — registered the same 2313, ran 2310 (the three
clock-dependent cases are excluded there and run in a second job) and reported
**0 failed, 7 skipped** (the seven embedder-conformance rules that need process
isolation and are skipped in a shared run). Composition, so that a later count that
moves is attributable: **922** `tests/conformance/**/*.scala` fixtures and **922**
`transpiled/` differential twins of them — one per fixture, registered because
`PROTOSCALA_TRANSPILED_TESTS` is `ON` by default (Phase 7) — **396** individually
registered GoogleTest cases plus the five whole-binary cases `unit/actors`,
`unit/modules`, `unit/generated_support`, `umd/protost-interop` and
`interop/foreign-call`, **28** `cli/*`, **16** `embedder-conformance/*`, **12**
`benchmarks/*` and **12** `benchmarks-transpiled/*`.

This file quoted **1344** until 2026-09-27. That number is the same suite with the
transpiled harness absent — it predates Phase 7 — and its sub-composition had also
gone stale (919 fixtures and 24 `cli/*`, against 922 and 28 in the tree). The step
from 1343 to 1344 recorded there was the embedder-conformance group going 15 → 16:
protoCore 2.5.0 adds one rule, `mutable.graph_cycles`, and this suite is
parameterised over protoCore's rule list.

Static check: **0 unjustified findings**, 1 justified entry, 3 informational.

## RESOLVED — the actor worker retained every message it ever delivered

Both of the phase's failing cases, `gc.host_stress` (rule 3) and
`heap.ceiling_progress` (rule 8), had **one cause**, and it is fixed
(`src/runtime/ActorScheduler.cpp`, `ActorScheduler::drainOne`). Both are green.

### The named hypothesis was WRONG

The first report named `ActorState::pendingIdx` advancing over an untruncated
`__pend<n>__` list. **Measured and refuted:** with one actor and a 2,000-message
backlog, `__pend1__` holds between 26 and 1,424 entries across 80,000 messages —
bounded, exactly as the batch protocol intends. The cursor is not the mechanism.
A fourth guess is not evidence, so the mechanism below was measured, not argued.

### The actual cause

A `ProtoContext` destroyed while its `returnValue` resolves to a cell anchors that
cell in its **parent's** young chain, through a `ReturnReference`
(`protoCore/core/ProtoContext.cpp`, `~ProtoContext`). A young chain reaches the
collector only on the context's destruction or when `ProtoContext::safepoint()`
finds the context above `ProtoSpace::maxAllocatedCellsPerContext`
(`CONTEXT_GC_THRESHOLD_DEFAULT`, 10,000 cells) — **never merely because a cycle
ran**.

`runTurn` opened one context per message (correct, P2) whose parent was the
**worker's session-long context**, and set `turn.returnValue = env`. The worker's
context is never destroyed while the pool lives, and each message added exactly
one cell to it, so those anchors accumulated for 10,000 messages per worker and
each one held a whole envelope live.

**Measured, one persistent actor, 2,000-message backlog, a forced collection at
every checkpoint** (`liveCellsLastCycle`, the collector's own reachable count):

| messages delivered | before the fix | after the fix |
|---|---|---|
| 2,000 | 23,193 | 24,106 |
| 20,000 | 130,769 | 22,920 |
| 80,000 | 491,145 | — |

Before: **6.0 cells retained per message delivered**, linear, never falling.
After: **flat**, and the residual is not "less than before" but a real working
set — about 23,000 cells at every message count, which is protoScala's settled
live set (≈22,000) plus the in-flight batch the case deliberately allows. The
per-message term is **zero**: 22,920 at 20,000 messages against 24,106 at 2,000.

### The fix

`drainOne` opens **one context per turn**, between the worker's context and the
per-message ones. It is destroyed at the end of every turn, so the anchors a turn
leaves behind are submitted then, unconditionally; it carries no `returnValue` of
its own, so it anchors nothing in the worker's context.

### Mutation

Remove that context and pass the worker's `ctx` straight to `runTurn` /
`finishTurn`. `gc.host_stress` aborts 3 runs out of 3 with live sets of 325,030,
313,358 and 307,125 cells against its 400,000-cell ceiling. Restored, it passes
3 out of 3.

## Rule 8 is green, and it is INTERMITTENT — a protoCore finding, not a protoScala one

`heap.ceiling_progress` passes with the fix, including in a full suite run (green
in 5.59 s at the time; the total that run reported is no longer quoted here — see
the command above). **It fails about 2 runs in 10 when run alone**, and the
failure is a different defect that the retention had been masking — which is what
the case's own text warns about ("the third was unreachable until the first was
fixed").

```
protoscala: actor handler failed: ClassCastException: + expects a number, got Null
```

**Diagnosed, in protoCore, and NOT fixed here** — a kernel change affects five
runtimes and is the maintainer's call.

The envelope being delivered has lost **every own attribute**: its mutable handle
was swept and GC phase 5b removed its mutables-tree entry, while the batch list
still referenced it. Instrumented, the pattern is unambiguous — of a 182-message
batch, **exactly one element is lost, always the last one**:

```
DIAG-C env=0x7087e89ab280 cursor=182 batch=182 lost=1 first=181 last=181
```

The last element of a batch is the most recently pushed, i.e. the node most likely
to have been prepended **between `takeAll`'s `head` load and its detaching
exchange**. `ProtoMPSCQueue::takeAll` publishes its retain cell with the chain it
*loaded* (`retain->chain.store(h)`) and then detaches with
`q->head.exchange(nullptr)`, which may return a **longer** chain. The nodes in the
difference are covered by nothing the collector reads, and `takeAll` then calls
`parkForStopTheWorld` every 64 nodes of its walk — so a whole stop-the-world can
complete while those items are reachable only from a `std::vector` of C++ locals.
The file's own comment states the premise that fails: "nothing this loop still
needs lives only in a C++ local. The nodes hang off the retain cell this takeAll
published onto `retained`."

**Proposed one-line kernel fix, for the maintainer, and it is MEASURED rather than
argued:** after the detaching exchange, and still inside the same critical section,
widen the retain cell to the chain actually detached
(`retain->chain.store(chain, std::memory_order_release)`). The
published-before-detach ordering the soundness proof needs is untouched — the cell
was already published with `h` before the detach; widening it afterwards only adds
coverage.

Validated against a **scratch clone** of protoCore, never against the repository:
the patched library was loaded into protoScala's own already-built isolate binary
through `LD_LIBRARY_PATH` (the change is internal to one `.cpp`, so the ABI is
identical), with 24 of the runs interleaved control/patched so machine state hits
both equally.

| protoCore | runs | `heap.ceiling_progress` failures |
|---|---|---|
| as committed | 40 | **4** (10%) |
| retain widened | 40 | **0** |

The patch is saved at
`../.agent_scratch/p4-fixes/protoCore-pmq-retain-widening.patch`. **0 of 40 against
4 of 40 is suggestive, not conclusive** (Fisher one-sided p ≈ 0.12): it is worth
what the mechanism reading is worth, and no more. The kernel change is still the
maintainer's, and nothing in this repository depends on it.

## What the adaptor does not prove

**`forEachThreadKind` is deliberately not implemented**, so `thread.registered`
reports `NOTAPPLICABLE` and **rule 11 is unverified for protoScala by that
case.** protoScala creates exactly one kind of OS thread beyond the main one —
the actor-scheduler worker, through `ProtoSpace::newThread` — and offers no hook
to run arbitrary C++ on a live worker. Spawning a thread here through
`newThread` ourselves would audit protoCore's thread registration rather than
protoScala's, and would report a **pass for a property nobody checked**. What is
known instead, and by a different method: the static census finds **zero raw
`std::thread` in `src/`**, and the only two thread-creation sites go through
`ProtoSpace::newThread`. That is a reading, not a measurement, and it is
recorded as such.

`loadSamePathTwoProviders` is likewise not implemented, so rule 9c's runtime half
is unchecked and the case says so.

## Judgement items

### C3 — Is any `ProtoObject*` held across an allocation only in a C++ local?

**Mechanised:** `gc.host_stress`, 20 rounds of the actor path under a
400,000-cell ceiling with a forced cycle between rounds — 34 cycles,
1,323,476 cells reclaimed. **Not yet run under AddressSanitizer**, and until it
is, that pass is not evidence: the one bug of this class that this project met
in protoScala (`Mailbox::push` holding a CAS snapshot across `appendLast`) was
invisible without a sanitizer.

**Not mechanised:** the general case — whole-program escape analysis.

**Answer:** the paths that hold a `ProtoObject*` across an allocation are the
mailbox and the ready stack. `src/runtime/Mailbox.cpp` roots its CAS snapshot in
a root-context automatic-local slot (`scope.setAutomaticLocal(0, cur)`) before
`appendLast`, which is the model answer and the fix for the met bug.
`ActorState::actor` is a raw `ProtoObject*` in a C++ structure and is safe
because the actor is anchored in the registry list for the whole session (D46)
and an `ActorState` is never freed before the scheduler — the comment in
`ActorScheduler.h` states this. Every other value lives in a traced frame slot.

**Reviewed:** agent, 2026-09-25, `feature/p4-conformance` — **pending maintainer
review**. The ASan run is outstanding and is the part of this answer that is not
yet evidence.

### C5 — Which structure is rule 5 measured on, and why?

**`List`.** protoScala's user-visible sequences are `List` and `Vector`, and both
are a protoCore `ProtoList` held in a box; neither is a `ProtoTuple`, by the rule
in `CLAUDE.md` and the enforcing comments in `CollectionPrimitives.cpp`. A
`TupleN` is a case-class instance, not a `ProtoTuple`, so measuring one would not
reach rule 5's subject at all. There is therefore no choice here that could
flatter the runtime: protoScala has no perennial sequence representation to pick
instead.

**Reviewed:** agent, 2026-09-25 — pending maintainer review.

### C7 — Is external memory correctly accounted and correctly released?

**N/A, in one line: protoScala uses no external wrappers.** `fromExternalPointer`
and `newExternalBuffer` do not appear in `src/` at all; where protoScala needs to
carry a native pointer it encodes it as a `SmallInteger`
(`ActorPrimitives.cpp`, `ActorScheduler.cpp`). So there is no external byte total
to keep, no finalizer to get wrong, and `externalBytesAccounted()` correctly
returns −1. protoScala does allocate outside protoCore — `BytecodeModule` holds
plain PODs in `std::vector` — and that memory is owned by C++ lifetimes rather
than by anything protoCore could finalize.

**Reviewed:** agent, 2026-09-25 — pending maintainer review.

## Informational findings

- **protoScala never calls `setHeapLimits` and installs no
  `outOfMemoryCallback`.** So it has no bounded heap and no recovery hook, and —
  the part that matters for every GC measurement in this repository — **no
  collection cycle ever starts by itself**, because a limit is what makes a
  thread need cells the heap cannot supply. Whether a runtime should bound its
  heap is a maintainer decision, so this is reported, not failed.
- `src/runtime/StackGuard.cpp:145` performs a bare `pthread_join`. It is correct
  **by construction only** — the `ProtoSpace` is built inside the joined thread,
  so the joining thread is registered with no space and holds no quorum. The
  allowlist entry records the condition under which that stops being true.
