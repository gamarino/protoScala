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
| 2026-09-22 | Map/Set on a new protoCore type `ProtoSparseListObject` (GC-traced keys) | maintainer | PSLO-SPEC |
| 2026-09-22 | Actor model on protoClojure's design; mailboxes on a new protoCore type `ProtoMPSCQueue` | maintainer | DESIGN §8, PMQ-SPEC |
| 2026-09-22 | P1: iterator option (a); hashed-collection helper in protoCore | maintainer (accepted recommendation) | PSLO-SPEC §3.3, §4 |
| 2026-09-22 | Overnight scope: Phases 1+2 on protoScala `main`; P1 on a protoCore branch, not merged, embedders not rebuilt | maintainer | — |
| 2026-09-22 | P1 D2a–D7 (bucket encoding, hashed key word, keep key, nullptr keys, isEqual identity, own prototype, CellType discrimination, SOVERSION 1) | agent, pending review | PSLO-SPEC §7 |
