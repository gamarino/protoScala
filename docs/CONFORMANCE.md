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
| `gc.host_stress` | 3 | **PASS** | 20/20 rounds under a 400,000-cell ceiling, 34 cycles, 1,323,476 cells reclaimed |
| `symbol.fast_path_key_hits` | 4 | **PASS** | a 31-byte key is readable through both `getAttribute` and `getOwnAttributeDirect` |
| `external.finalizer_runs` | 7 | **PASS** | kernel characterisation: one call, right pointer |
| `module.root_survives_cycle` | 9b | **PASS** | kernel characterisation: module and contents survived 3 cycles |
| `module.alias_rejected` | 9c | **NEEDSREVIEW** | the kernel keeps provider+path+version distinct; protoScala's own keying is unchecked |
| `external.bytes_accounted` | 7 | **NEEDSREVIEW** | no external byte total — see C7 |
| `thread.registered` | 11 | **NOTAPPLICABLE** | `forEachThreadKind` not implemented — see below. **The rule is UNVERIFIED by this case** |
| `stw.quorum_completes` | 2 | **PASS** (isolated) | a cycle completed with an actor worker idle |
| `join.parks` | 2b | **PASS** (isolated) | a cycle completed while `Thread.start`/`t.join()` was blocked |
| **`heap.ceiling_progress`** | **8** | **FAIL** (isolated) | **see the finding below** |

Suite total: **1263** ctest cases, 1262 passing — 1248 pre-existing plus 15
conformance entries, with **no previously-passing test newly failing**. The one
failure is the rule-8 finding.

Static check: **0 unjustified findings**, 1 justified entry, 3 informational.

## The finding: rule 8

```
protoCore: heap hard limit 294912 cells reached; live set 280922 cells,
last cycle reclaimed 0 — out of memory
```

**Reproduction:**
`build_release/tests/unit/protoscala_conformance_isolate --case=heap.ceiling_progress`
(exit 134). The case configures a hard ceiling of `heapSize + 32768` cells —
which no protoScala code does, because protoScala never calls `setHeapLimits` —
and runs 200,000 messages through one actor.

**Severity: high, mechanism not yet established.** Three candidate mechanisms,
and distinguishing them is the first thing to do with this finding rather than
something to guess at now:

1. **An unbounded mailbox.** The producer is a tight Scala loop and the consumer
   is one actor worker. If the producer outruns it, the queued envelopes are
   genuinely live and the live set grows without limit. Under any configured
   ceiling that is an abort rather than backpressure.
2. **Processed messages retained.** `ActorState::pendingIdx` is a read cursor
   into `__pend<n>__`; if the list itself is not replaced as the cursor advances,
   already-processed envelopes stay reachable. That would explain
   `last cycle reclaimed 0` even while the consumer is draining, which mechanism
   (1) alone does not.
3. **The P2 topology** — every producer and the consumer parked in
   `waitForHeapHeadroom` with the collector idle.

`liveCellsLastCycle` = 280,922 against a 294,912-cell ceiling says the live set
really is that large, which points at (1) or (2) rather than (3).

**Not a regression.** protoScala never configures a ceiling, so
`maxHeapSize == 0`, `waitForHeapHeadroom` returns immediately and this code was
unreachable before this case existed. The finding was **latent, not new**.

**An earlier draft of the workload found a second thing** and it is worth
recording because it changed the adaptor: spawning a fresh actor per round of
2,000 messages reached the same abort with a live set of 237,536 cells, because
protoScala anchors every actor in the scheduler's registry for the whole session
(DESIGN D46). The live set grew with the **number of actors**. The workload now
reuses one actor, so the case measures the mailbox rather than the registry —
but an unbounded actor registry is a real property worth a maintainer's
attention on its own.

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
