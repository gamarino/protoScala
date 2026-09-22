# Decisions log

Decisions taken during implementation, with who took them. Entries marked
**[agent, pending review]** were taken by the implementing agent under the
maintainer's authorisation of 2026-09-22 ("decide what is missing and correct
it tomorrow"); the maintainer reviews them and may reverse any of them.

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
| 2026-09-22 | P1 D2a–D7 (bucket encoding, hashed key word, keep key, nullptr keys, isEqual identity, own prototype, CellType discrimination, SOVERSION 1) | agent, pending review | PROTOMAP-SPEC §7 |
| 2026-09-22 | Phase 1 Q1–Q22: the plan's provisional behaviours adopted as written (notably Q21: `ProtoContext::safepoint()` at loop back-edges, touching DESIGN R1; Q4: captured `var`s boxed in mutable cells; Q2: script mode with eager top-level vals); recorded as provisional D9–D18 in STATUS.md by Task 13 | agent, pending review | plans/2026-09-22-phase-1-core-language.md, "Open questions for the maintainer" |
| 2026-09-22 | Rename `ProtoSparseListObject` → **`ProtoMap`** (and iterator, tag, CellTypes, accessors `newMap`/`isMap`/`asMap`, prototype `mapPrototype`, files, spec) | maintainer | protoCore feature/pslo-p1, docs |

## Overnight run 2026-09-22 — agent rulings pending review

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
