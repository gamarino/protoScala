# Decisions log

Decisions taken during implementation, with who took them. Entries were marked
**[agent, pending review]** when taken by the implementing agent under the
maintainer's authorisation of 2026-09-22 ("decide what is missing and correct
it tomorrow").

**Review of 2026-09-23.** The maintainer has now ruled on every one of them.
The original wording of each decision is kept as it was written; the ruling is
recorded beside it. Four outcomes are not a plain approval:

- **D47 overturned** — `Future.apply` now takes its body **by name**,
  `Future(expr)`. Ruling: *least surprise for the Scala programmer*. By-name
  parameters were implemented as a general language feature to make it possible,
  with the new deviation **D53** for the call sites the compiler cannot resolve.
- **D45 overturned** — an actor handler may return a bare `newState` as well as
  `(newState, reply)`; with no reply the ask's future completes with `()`. Same
  ruling: forcing a handler that only updates state to invent a reply is the
  surprise to remove.
- **D44 superseded** — rather than leaving Phases 3 and 4 unbuilt, the maintainer
  directed that both be completed. The version implication of D44 (Phase 5
  shipping as 0.3.0 ahead of them) is to be revisited when they land.
- **D46 stands as approved**, with its stated precondition now met: it deferred
  unanchoring "until `ProtoMap` lands", and `ProtoMap` shipped in protoCore
  2.0.0 and is merged and released. It is ready to revisit; the behaviour is
  unchanged for now.

Two entries are **acknowledged, not approved as done**: ThreadSanitizer was
never run against the actors, and the cold-start budget is not claimed as met.
They stay visible as known gaps.

Two items remain **open for the maintainer** — see "Still open" at the end of
this file.

| Date | Decision | Taken by | Where |
|---|---|---|---|
| 2026-09-22 | Scala 3 complete syntax (braces + indentation); types parsed and erased | maintainer | DESIGN §2 |
| 2026-09-22 | `List` on `ProtoList` (AVL) | maintainer | DESIGN §6 |
| 2026-09-22 | `super` in stackable traits resolved in protoScala (O(n) walk) | maintainer | DESIGN §4.4 |
| 2026-09-22 | Recursive VM with frame snapshots for cooperative `await` | maintainer | DESIGN §3.6, §8.3 |
| 2026-09-22 | Map/Set on a new protoCore type `ProtoMap` (GC-traced keys) | maintainer | PROTOMAP-SPEC |
| 2026-09-22 | Actor model on protoClojure's design; mailboxes on a new protoCore type `ProtoMPSCQueue` | maintainer | DESIGN §8, PMQ-SPEC |
| 2026-09-22 | P1: iterator option (a); hashed-collection helper in protoCore | maintainer (accepted recommendation) | PROTOMAP-SPEC §3.3, §4 |
| 2026-09-22 | Overnight scope: Phases 1+2 on protoScala `main`; P1 on a protoCore branch, not merged, embedders not rebuilt | maintainer | — |
| 2026-09-22 | P1 D2a–D7 (bucket encoding, hashed key word, keep key, nullptr keys, isEqual identity, own prototype, CellType discrimination, SOVERSION 1) | agent; approved by the maintainer on 2026-09-23 | PROTOMAP-SPEC §7 |
| 2026-09-22 | Phase 1 Q1–Q22: the plan's provisional behaviours adopted as written (notably Q21: `ProtoContext::safepoint()` at loop back-edges, touching DESIGN R1; Q4: captured `var`s boxed in mutable cells; Q2: script mode with eager top-level vals); recorded as provisional D9–D18 in STATUS.md by Task 13 | agent; approved by the maintainer on 2026-09-23 | plans/2026-09-22-phase-1-core-language.md, "Open questions for the maintainer" |
| 2026-09-22 | Rename `ProtoSparseListObject` → **`ProtoMap`** (and iterator, tag, CellTypes, accessors `newMap`/`isMap`/`asMap`, prototype `mapPrototype`, files, spec) | maintainer | protoCore feature/pslo-p1, docs |
| 2026-09-23 | Phase 2 plan Q1–Q14: the plan's provisional behaviours adopted as written; D28–D34 recorded as provisional | agent; approved by the maintainer on 2026-09-23 | plans/2026-09-22-phase-2-object-model.md |
| 2026-09-23 | D35–D42 added to the provisional Phase 2 list; they were collected while implementing the phase and answer no numbered question: a refutable generator without `case` filters (D35), for-comprehension desugaring differences that leave the result unchanged (D36), intersection types cannot be tested at run time (D37), the default `toString` shape (D38), `List` hash codes (D39), the `override`-on-`var` diagnostic wording (D40), no universal apply for a plain class (D41), `MatchError` names the Scala class (D42). D41 and D42 were described in tutorial chapter 3 without an id; D41 also qualified the STATUS "universal apply" row, which had read as an over-claim | agent; approved by the maintainer on 2026-09-23 | docs/STATUS.md, docs/LANGUAGE.md §5, docs/tutorial/03-*.md |
| 2026-09-23 | The ledger's "compound-type pattern" deviation re-derived and **withdrawn**. The original note claimed protoScala accepts `case x: A & B` where scalac rejects it; measured against scalac 3.9, *both* reject the unparenthesised form, and the real divergence is the opposite one — scalac accepts `case x: (A & B)` and `x.isInstanceOf[A & B]` and tests both components, protoScala rejects them at compile time. Recorded as D37 with fixtures | agent; approved by the maintainer on 2026-09-23 | docs/STATUS.md D37, tests/conformance/11-pattern-matching/intersection-type-pattern*.scala |
| 2026-09-23 | Brace/indentation fixture pairing closed for the Phase 2 directories (`07-classes` … `12-for-comprehensions`): every fixture whose syntax differs now has a `-braces` twin (101 new fixtures, all green). The Phase 1 directories (`01-lexical` … `06-recursion`) and `tutorial/` keep representative pairs only — a tutorial fixture must mirror a published snippet verbatim, so a braces twin would have no snippet to mirror | agent; approved by the maintainer on 2026-09-23 | tests/conformance/, .superpowers/.../constraints.md |
| 2026-09-23 | Release 0.2.0 cut; the `v0.2.0` tag is left to the maintainer | agent (per plan) | CMakeLists.txt, CHANGELOG.md |
| 2026-09-23 | Phase 5 A0-1: cooperative `await` suspends the recursive VM through **per-frame catch-and-prepend snapshots** (option (a)); the VM stays recursive | agent; approved by the maintainer on 2026-09-23 | plans/2026-09-23-phase-5-actors.md A0-1, src/runtime/ExecutionEngine.cpp |
| 2026-09-23 | Phase 5 A0-2: an `await` whose call chain cannot be snapshotted raises `UnsupportedOperationException` instead of blocking a worker or re-running side effects; recorded as **D43** | agent; approved by the maintainer on 2026-09-23 | A0-2, docs/STATUS.md D43 |
| 2026-09-23 | Phase 5 A0-3: Phase 5 ships **before** Phases 3 and 4, so the release is **0.3.0** and the prelude gains `Try`/`Success`/`Failure` and `RuntimeError(className, message)` early; recorded as **D44** | agent; **superseded by the maintainer on 2026-09-23**: Phases 3 and 4 are to be completed rather than left unbuilt, and D44's version implication is to be revisited when they land | A0-3, lib/prelude.scala |
| 2026-09-23 | Phase 5 A0-4: an actor handler must return **`(newState, reply)`** strictly; anything else raises `IllegalArgumentException` and fails that message; recorded as **D45** | agent; **overturned by the maintainer on 2026-09-23** (least surprise for the Scala programmer): a handler may also return a bare `newState`, and the ask's future then completes with `()`. A `Tuple2` is still read as the pair form; only a handler that produces no value at all is rejected | A0-4, src/runtime/ActorScheduler.cpp |
| 2026-09-23 | Phase 5 A0-5: an actor is **anchored for the session** in a registry pinned in a root slot (no unanchoring until `ProtoMap` lands); recorded as **D46** | agent; approved by the maintainer on 2026-09-23. For the record, its stated precondition is met: `ProtoMap` shipped in protoCore 2.0.0 and is merged and released, so unanchoring is ready to revisit. The behaviour is unchanged | A0-5, src/runtime/Runtime.cpp |
| 2026-09-23 | Phase 5 A0-6: `Future.apply` takes a **function**, not a by-name parameter — `Future(() => expr)`; recorded as **D47** | agent; **overturned by the maintainer on 2026-09-23** (least surprise for the Scala programmer): `Future.apply` takes its body by name — `Future(expr)` and `Future { … }`. By-name parameters were implemented as a general feature to make it possible; the call sites that cannot be resolved are the new deviation **D53** | A0-6, src/compiler/Compiler.cpp, src/runtime/ActorPrimitives.cpp |
| 2026-09-23 | Phase 5 A0-7: `map`/`flatMap`/`recover`/`onComplete` continuations run **on the thread that completes the future** (or immediately on the caller when it is already complete); recorded as **D48** | agent; approved by the maintainer on 2026-09-23 | A0-7, src/runtime/Futures.cpp |
| 2026-09-23 | Phase 5 A0-8: the actor mailbox is consumed through a **`Mailbox` seam** — `ProtoMPSCQueue` when the linked protoCore provides it, a CAS'd `ProtoList` otherwise; protoCore 2.0.0 has no `newMPSCQueue`, so 0.3.0 ships the fallback | agent; approved by the maintainer on 2026-09-23 | A0-8, src/runtime/Mailbox.cpp, CMakeLists.txt |
| 2026-09-23 | Phase 5 A0-9: the benchmarks need a clock and non-worker sender threads, so `Thread.start`/`join` and `System.nanoTime`/`currentTimeMillis`/`getenv` are added as runtime facilities; recorded as **D49** | agent; approved by the maintainer on 2026-09-23 | A0-9, src/runtime/ActorPrimitives.cpp |
| 2026-09-23 | Phase 5 A0-10: a turn drains a **batch of 8** messages (the DESIGN §8.2 value), with the remainder of a `takeAll` kept in a per-band `__pend<n>__` list on the actor | agent; approved by the maintainer on 2026-09-23 | A0-10, src/runtime/ActorScheduler.cpp |
| 2026-09-23 | Phase 5 A0-11: **no new opcodes**; `!`, `?`, `await` and friends are ordinary sends to native methods and the 128..159 range stays reserved | agent; approved by the maintainer on 2026-09-23 | A0-11, src/compiler/Opcodes.h |
| 2026-09-23 | Phase 5 deviations **D43–D52** recorded as provisional in STATUS.md, LANGUAGE.md §5 and tutorial chapter 3 | agent; approved by the maintainer on 2026-09-23, with D45 and D47 replaced by the rulings above and **D53** added | docs/STATUS.md, docs/LANGUAGE.md, docs/tutorial/03-*.md |
| 2026-09-23 | Plan Tasks 3–7 were committed as **one** commit rather than five: the handler calling convention, the ask future and the `FutureYield` catch are the same code path, and splitting them would have produced intermediate commits that do not build | agent; approved by the maintainer on 2026-09-23 | commit "actors: scheduler, Scala surface, futures and cooperative await" |
| 2026-09-23 | Two bugs found by the tests and fixed against the plan's own pseudocode: (a) `Futures::complete` wrote the value **before** the state CAS, so a losing completer overwrote the winner's value — replaced by a claiming CAS (`kClaiming`, still read as pending) followed by the write and the final store; (b) `awaitBlocking` read the wake epoch *inside* the lock, after the state check, losing every wake-up and charging the 50 ms safety net to every blocking ask (a `priority` run went from 7.4 s to 0.11 s once the epoch was read before the state) | agent; approved by the maintainer on 2026-09-23 | src/runtime/Futures.cpp |
| 2026-09-23 | ThreadSanitizer (plan Task 8 Step 4) **not run**: the host was shared with three other builds all night, and a TSan run would have invalidated the benchmark table measured on it. Recorded in STATUS as not claimed | agent; **acknowledged by the maintainer on 2026-09-23 as a known gap, not approved as done**: the TSan run is still owed | docs/STATUS.md "Open bugs" |
| 2026-09-23 | A **P1 rooting bug in the plan's own `Mailbox::push` pseudocode** was found and fixed: the `__items__` snapshot was held only in a C++ local across `appendLast`, which allocates, so a producer that lost the compare-and-swap kept walking a list the collector could already reclaim (SIGSEGV in `implInsertAt` under a heap limit with 8 senders). Every compare-and-swap snapshot in the phase is now rooted in an automatic-local slot before the allocation: `Mailbox::push`, `Mailbox::takeAll`, the actor registry, the `Thread.start` registry, `futures::addWaiter`, `futures::onComplete`, `runContinuations`, and the batch in `nextMessage` | agent; approved by the maintainer on 2026-09-23 | src/runtime/Mailbox.cpp, ActorScheduler.cpp, Futures.cpp, ActorPrimitives.cpp |
| 2026-09-23 | `Actor.stats` now counts a message **before** the handler runs, not after: the ask's future is completed inside `deliver`, so a caller holding a reply could otherwise still read `messagesProcessed == 0` (the tutorial `13-actors-stats` fixture failed at 16 workers) | agent; approved by the maintainer on 2026-09-23 | src/runtime/ActorScheduler.cpp `runTurn` |
| 2026-09-23 | `tests/cli/actors-stress.sh` pins its own `PROTOCORE_HEAP_LIMIT_CELLS=8000000`, as `benchmarks/object_tree.scala` already does. Measured: its 200000-message backlog on one actor needs between 1000000 and 1500000 cells with the CAS-list mailbox, so the ambient 20000-cell sweep would report an honest out-of-memory, not a rooting defect | agent; approved by the maintainer on 2026-09-23 | tests/CMakeLists.txt |
| 2026-09-23 | The **cold-start budget (< 25 ms) is not claimed as met**: 24.20 / 23.58 ms at load ~4 but 25.11 / 26.57 ms at load ~8.7 on a shared host. Not eager worker creation (the pool starts at the first `Actor.spawn`; 2 vs 12 `clone3` calls), but the prelude grew with D44's `Try`/`RuntimeError`/`ActorStats`. Re-measure on a quiet host | agent; **acknowledged by the maintainer on 2026-09-23 as a known gap, not approved as met**: the budget is still owed a measurement on a quiet host | docs/STATUS.md |
| 2026-09-23 | Release 0.3.0 cut; the `v0.3.0` tag is left to the maintainer | agent (per plan) | CMakeLists.txt, CHANGELOG.md |

## Overnight run 2026-09-22 — approved by the maintainer on 2026-09-23

Every ruling below is approved as recorded. The two rows that describe work
*not* done (the P1 perf gate and ASan findings, and P1 Task 12) are outstanding
work, not decisions: they stay on the list until they are run.

| Decision | Cost if wrong |
|---|---|
| Phase 1 plan open questions Q1–Q22 adopted as the plan's provisional behaviours (incl. Q21: `safepoint()` at loop back-edges, touching DESIGN R1; Q4: captured vars boxed in mutable cells) | each reversible; Q21 is a GC-interaction choice to confirm with protoCore |
| Layout: a same-line closer (`else`, `catch`, `do`, `yield`) pairs with a pending partner inside the current region before popping it (Scala semantics over the plan's simpler rule) | extra layout complexity |
| Forward references follow SLS 4.4 (interval includes the referenced definition): `{ def f = y; val y = 1 }` is rejected, as in scalac | a program scalac also rejects |
| REPL redefinition uses per-definition keys `x#N` (Scala REPL shadowing); a throwing `val` leaves the name undefined | compiler/global-table change |
| Primitives: negative shift, `Int.toChar` out of range → IllegalArgumentException; `toInt`/`toLong`/`round` of ±Infinity → ArithmeticException; Char predicates ASCII-only (D19–D22) | small behaviour changes |
| `@main` accepts only no parameters or `args: String*` (typed parameters recorded as a deviation) | missing Scala 3 feature |
| P1: perf gate parked (retired instructions flat, cycles noisy under host load) and ASan pre-existing findings (3 timing/limit tests, ProtoSpace teardown leaks) not fixed | a real slowdown could slip; re-run `perf stat -r 3` on a quiet host before merge |
| P1: Task 12 (rebuild of every embedder) not run; branch `feature/pslo-p1` not merged, not pushed | embedder validation pending |

## Phase 2 open questions (2026-09-22)

| Decision | Taken by |
|---|---|
| Q2: class/trait membership needs a new protoCore API (allocation-free walk of the flattened parent chain, no arbitrary step limit), shared by the family; protoScala uses a per-class marker attribute until it lands | maintainer |
| Q1: instances under construction are rebuilt field by field; an early-escaped `this` sees an older version — accepted as deviation D28 | maintainer |
| Q14: Phase 2 writes tutorial chapters 6, 7 and 9 (index numbering kept; 8 = collections in Phase 3) | maintainer |
| Q3–Q13: plan recommendations accepted (immutable class prototypes, compile-time companion links, class-qualified private keys, plain `super` in Phase 2 / `super[T]` in Phase 4, top-level classes only, embedded Scala `Option` prelude, minimal `List` moved up from Phase 3, `SEND_KW` for native named args, representation-based type tests D29, D30/D31, lazy members D34, REPL class shadowing) | maintainer (accepted recommendations) |
| 2026-09-22 | Q2 refined: no new protoCore API; correct `isInstanceOf`/`hasParent` implementations (linear walk of the flattened chain, no step cap, no allocation, same contracts) — docs/platform/ISINSTANCEOF-FIX.md | maintainer |
| 2026-09-22 | `setParents` flattens the chain (explicit parents in order, then missing ancestors appended); `isInstanceOf` becomes a pure linear walk; two pre-existing `isInstanceOf` bugs fixed (non-first parents, mutable snapshots) — embedder suites to run before merge | maintainer |
| 2026-09-22 | protoCore merge (ProtoMap + isInstanceOf/setParents fixes) is versioned **2.0.0 with SOVERSION 2** (ABI and behaviour change); then rebuild, check and fix protoPython, protoJS, protoST, protoClojure and protoScala, and push everything | maintainer |
| 2026-09-22 | protoCore C1: fix the root cause in `newChild` (take the chain from a mutable prototype's current snapshot) rather than patching isInstanceOf; C2: keep the universal-root answer for parentless objects | maintainer |
| 2026-09-22 | protoCore: remove `getAttribute`'s 500-step cap (chains are flat and finite; setParents rejects cycles), so every lookup helper agrees at any depth | maintainer |
| 2026-09-22 | protoCore C2/C3: keep the `newChild` fix; fix the embedders instead — protoJS integrity markers (freeze/seal/preventExtensions) must stop being parent-chain markers, protoPython instances must not inherit the metaclass chain | maintainer |
| 2026-09-22 | Cold-start target raised from < 20 ms to < 25 ms (Phase 2: the embedded Scala prelude adds ~1.2 ms and the standard library will keep growing) | maintainer |
| 2026-09-23 | Closing sequence gains a final step: installers for protoCore + the five runtimes (Linux verified here; macOS/Windows configured but unverified), each runtime depending on an installed protoCore rather than bundling it | maintainer |
| 2026-09-23 | 0.2.0 benchmark numbers are re-measured against protoCore 2.0.0 (the shipped stack) on a quiet machine, not kept tied to e43fa2e4 | maintainer; approved 2026-09-23 |

## Phase I — native installers (2026-09-23) — approved by the maintainer on 2026-09-23

Taken while implementing `docs/plans/2026-09-23-phase-i-installers.md`. Each one
spans the whole family (protoCore plus the five runtimes), not protoScala alone.

| ID | Decision | Taken by |
|---|---|---|
| **D-I1** | protoCore emits a real CMake package config (`install(EXPORT)` + `configure_package_config_file` + `write_basic_package_version_file` with `SameMajorVersion` + an explicit `SOVERSION` assertion) and a pkg-config `.pc`. Every runtime prefers `find_package(protoCore <floor> CONFIG)`; sibling-directory discovery is kept as an explicit developer fallback used only when no installed package is found, and refused by `-DPROTOCORE_REQUIRE_PACKAGE=ON` | agent; approved by the maintainer on 2026-09-23 |
| **D-I2** | The unconditional `DEB;RPM;TGZ` CPack generators in protoPython, protoST, protoClojure and protoJS are guarded on `find_program(dpkg)` / `find_program(rpmbuild)`. `cpack` runs every configured generator in one pass and a missing tool is fatal, so on a host without `rpmbuild` the DEB and the TGZ went down with the RPM | agent; approved by the maintainer on 2026-09-23 |
| **D-I3** | protoScala gains CPack **now** rather than "in Phase 6": the maintainer's scope is all five runtimes. `protoscala-0.3.0-Linux.deb` is this repository's first package | agent; approved by the maintainer on 2026-09-23 |
| **D-I4** | protoJS keeps its standalone `packaging/` pipeline; it is not migrated to CPack. Its hardcoded protoCore `>= 1.0.0` checks are raised to the `[2.0.0, 3.0.0)` range and backed by a `libprotoCore.so.2` soname check | agent; approved by the maintainer on 2026-09-23 |
| **D-I5** | protoST's stdlib self-location gains the macOS (`_NSGetExecutablePath`) and Windows (`GetModuleFileNameA`) branches, ported from protoPython's `getExecutablePath()` | agent; approved by the maintainer on 2026-09-23 |
| **D-I6** | Linux packages are built, installed into a scratch prefix inside the workspace and smoke-tested there. macOS and Windows packaging is configured and reviewed but marked **UNVERIFIED** in the plan and in every repository's installation document | agent; approved by the maintainer on 2026-09-23 |
| **D-I7** | The root-owned protoCore `1.0.0` at `/usr/local/lib` is left untouched. D-I1's version check is what protects a build from picking it up, and it is proved by configuring protoScala against `/usr/local` alone and getting a `FATAL_ERROR` where the same command used to succeed | agent; approved by the maintainer on 2026-09-23 |

### protoScala-specific consequences

| Decision | Taken by |
|---|---|
| protoScala's `find_package` floor is **2.1**, not the 2.0 the other four runtimes use, because the actor mailbox needs `ProtoMPSCQueue` (protoCore 2.1.0). The DEB and RPM relations carry the same floor | agent; approved by the maintainer on 2026-09-23 |
| **A0-8 is superseded.** The mailbox seam no longer selects its backend by grepping `protoCore.h` for the substring `newMPSCQueue`; it compares the protoCore *version* against 2.1.0 (plan risk R7). The probe read whichever header the discovery found first, so which mailbox implementation was compiled in depended on the discovery mode, and an installed 2.1 package and a sibling tree could disagree silently. With the 2.1 floor, 0.3.0 now ships the `ProtoMPSCQueue` mailbox, not the CAS'd-`ProtoList` fallback. The fallback code stays in `src/runtime/Mailbox.cpp` for a developer build against an older protoCore | agent; approved by the maintainer on 2026-09-23 |
| In developer-fallback mode the protoCore version is read from `../protoCore/CMakeLists.txt`'s `project()` call, and a version below 2.1.0 is a `FATAL_ERROR`. Without a package config there is nothing else authoritative to read | agent; approved by the maintainer on 2026-09-23 |
| `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is **not** enabled (the plan's Task 5 Step 4 proposed it; plan risk R9 materialised). `dpkg-shlibdeps` resolves every `NEEDED` entry to the distribution package that owns it, and no distribution owns `libprotoCore.so.2`: it fails with "cannot find library" and takes the whole `.deb` down. `libreadline` is therefore not declared, matching protoST and protoClojure | agent; approved by the maintainer on 2026-09-23 |

## Still open for the maintainer (2026-09-23)

These two are open **questions**, not agent decisions, and the review of
2026-09-23 did not settle them. They are recorded here so they are not mistaken
for approvals.

| Question | Why it is open |
|---|---|
| Does protoCore's `[Unreleased]` CHANGELOG section fold into the **2.1.0** entry, or become **2.1.1**? | The Phase I packaging work wrote an `[Unreleased]` entry and deliberately left `project(protoCore VERSION 2.0.0 …)` alone, while the P2 plan bumps 2.0.0 → 2.1.0. Whether the packaging change rides along with 2.1.0 or gets its own patch release is the maintainer's call; it decides what `find_package` floors and package relations the family declares |
| Does the **`saturation-8` / `saturation-32` addition to DESIGN §8.5** stay? | Those two benchmark modes deviate from the section's own 1,000,000-trivial-message rule: they put the cost inside the handler to separate scheduler limits from actor-count limits, and they are the only modes that read the physical core count. They were added to mirror protoST's `saturation_8a.st` / `saturation_32a.st`. Keeping them means DESIGN §8.5 no longer has one uniform rule |
