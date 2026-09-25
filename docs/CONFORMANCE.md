# protoScala — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`. This file
records protoScala's **first-run result**, the judgement answers a script cannot
give, and what each capability of the adaptor does and does not prove.

- Adaptor: `tests/unit/ConformanceHost.h`
- Wiring: `tests/unit/ConformanceTests.cpp`, `tests/unit/ConformanceIsolate.cpp`
- Static ratchet: `conformance-allow.txt`
- Run: `ctest --test-dir build_release -R embedder-conformance < /dev/null`

## First run — 2026-09-25, protoCore at `feature/embedder-conformance-p4`

| Case | Rule | Result | Numbers the case reported |
|---|---|---|---|
| `gc.young_submitted` | 1 | **PASS** | grew in-use by 363,162 cells; 3 cycles reclaimed 360,082; residual 3,080 |
| `gc.transient_reclaimed` | 5 | **PASS** | grew 305,818; residual −264 (`List` is a `ProtoList`, not a `ProtoTuple`) |
| **`gc.host_stress`** | **3** | **FAIL** | live set 316,230 under a 400,000-cell ceiling, reclaimed 0 — **see the mailbox-retention finding below.** An earlier run of this case passed (34 cycles, 1,323,476 reclaimed) with a workload that spawned a fresh actor per call; that pass was an artefact of a shorter-lived actor |
| `symbol.fast_path_key_hits` | 4 | **PASS** | a 31-byte key is readable through both `getAttribute` and `getOwnAttributeDirect` |
| `external.finalizer_runs` | 7 | **PASS** | kernel characterisation: one call, right pointer |
| `module.root_survives_cycle` | 9b | **PASS** | kernel characterisation: module and contents survived 3 cycles |
| `module.alias_rejected` | 9c | **NEEDSREVIEW** | the kernel keeps provider+path+version distinct; protoScala's own keying is unchecked |
| `external.bytes_accounted` | 7 | **NEEDSREVIEW** | no external byte total — see C7 |
| `thread.registered` | 11 | **NOTAPPLICABLE** | `forEachThreadKind` not implemented — see below. **The rule is UNVERIFIED by this case** |
| `stw.quorum_completes` | 2 | **PASS** (isolated) | a cycle completed with an actor worker idle |
| `join.parks` | 2b | **PASS** (isolated) | a cycle completed while `Thread.start`/`t.join()` was blocked |
| **`heap.ceiling_progress`** | **8** | **FAIL** (isolated) | **see the finding below** |

Suite total: **1263** ctest cases, **1261 passing** — 1248 pre-existing plus 15
conformance entries, with **no previously-passing test newly failing**. The two
failures are both new conformance findings: the mailbox retention above and rule
8 below (which is very likely the same mechanism reached by a different route).

Static check: **0 unjustified findings**, 1 justified entry, 3 informational.

## The finding: the actor mailbox appears to retain every message ever delivered

This is the phase's most specific finding for protoScala and it reproduces in
**two independent cases**, so it is reported first.

`gc.host_stress` aborts:

```
protoCore: heap hard limit 400000 cells reached; live set 316230 cells,
last cycle reclaimed 0 — out of memory
```

**Reproduction:**
`build_release/tests/unit/protoscala_conformance --gtest_filter='*gc_host_stress*'`
(exit 134). Twenty rounds of the actor path under a 400,000-cell ceiling with a
forced cycle between rounds, **one actor for the whole run** and at most 2,000
messages in flight at a time.

**The arithmetic is the finding.** The run delivers 20 × 4,000 = **80,000
messages** through that one actor, and the live set at the abort is **316,230
cells**: **3.95 cells per message delivered**, with `reclaimedLastCycle` at zero.
A live set that grows by a small constant per message, while at most 2,000
messages are ever queued, is retention — not an unrooted local, and not the
actor registry.

**Named hypothesis, unproven:** `ActorState::pendingIdx` is a *read cursor* into
the actor's `__pend<n>__` list (`src/runtime/ActorScheduler.h:47-51`). If the list
itself is never truncated or replaced as the cursor advances, every envelope ever
delivered stays reachable from the actor, which is anchored in the registry for
the session. That would produce exactly this shape: a bounded queue depth, an
unbounded live set, and zero reclamation. **The first thing to do with this is to
measure cells-per-message at two different message counts** — if the ratio holds,
the hypothesis is confirmed without reading any more code.

Three alternatives were eliminated by measurement rather than by argument:

1. *Actor accumulation.* A fresh actor per round aborted at 237,536 cells and a
   fresh actor per call at 322,474; one actor for the whole run still aborts at
   316,230. So the registry is not the mechanism here (it is a real property —
   `Actor.spawn` retains for the session — but not this).
2. *An unbounded backlog.* At most 2,000 messages are in flight.
3. *A ceiling below the working set.* protoScala's settled live set is ~22,000
   cells; the ceiling here is 400,000.

**Severity: high.** A long-running protoScala program that sends messages to a
long-lived actor has a live set proportional to total messages sent. Without a
configured heap ceiling that is unbounded RSS; with one it is an abort.

## The finding: rule 8 — the same shape, reached by a different route

```
protoCore: heap hard limit 221865 cells reached; live set 215351 cells,
last cycle reclaimed 0 — out of memory
```

**Reproduction:**
`build_release/tests/unit/protoscala_conformance_isolate --case=heap.ceiling_progress`
(exit 134). The case settles the space, measures protoScala's own live set
(≈21,865 cells), and sets a hard ceiling **200,000 cells above it** — which no
protoScala code does, because protoScala never calls `setHeapLimits`. The workload
is one actor with a bounded 2,000-message backlog.

**This is very likely the mailbox retention above, reached through a different
case**, and the numbers say so: the live set climbs to the ceiling with zero
reclamation, exactly as in `gc.host_stress`. It is reported separately because the
two cases configure the ceiling differently and a single mechanism explaining both
is a conclusion, not an observation.

**Not a regression.** protoScala never configures a ceiling, so `maxHeapSize == 0`,
`waitForHeapHeadroom` returns immediately and this code was unreachable before
this case existed. The finding was **latent, not new** — which is the whole point
of a case that configures its own ceiling.

**Two earlier versions of this measurement were invalid, and both are recorded
because the abort surviving the fixes is what makes the number trustworthy.** The
first used a ceiling of `heapSize + 32768` — only 32,768 cells above what the
runtime already held — which cannot distinguish "cannot make progress" from "the
ceiling is below the working set". The second bounded the ceiling properly but
used a workload that enqueued all 200,000 messages before reading any, so the
live set was unboundedly live **by construction**. Both are fixed;
`Host::runProducerConsumer`'s contract now requires a bounded backlog.

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
