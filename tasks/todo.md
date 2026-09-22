# protoScala — task tracker

## Phase 0 — Skeleton (2026-09-22)
- [x] Design spec, language reference, roadmap, status, interop docs
- [x] Platform spec `ProtoSparseListObject`
- [x] CMake build against protoCore; `--version`, `--help`
- [x] Unit / conformance / CLI harnesses; hello-world XFAIL
- [x] Plans for Phase P1 and Phase 1
- [x] GitHub repository gamarino/protoScala (public, MIT)

## Next
- [ ] Phase P1 — Task 0: maintainer decisions (iterator option, helper location)
- [ ] Phase P2 — plan + Task 0: GC strategy for `ProtoMPSCQueue` with the maintainer
- [ ] Phase 1 — execute docs/plans/2026-09-22-phase-1-core-language.md

## Review
Phase 0: build and 6/6 tests verified on 2026-09-22 (2 unit, 3 CLI, 1
conformance XFAIL); the conformance runner was checked to fail on a
non-conforming EXPECT and to pass a matching EXPECT-ERROR.
