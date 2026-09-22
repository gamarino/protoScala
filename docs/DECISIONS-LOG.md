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
