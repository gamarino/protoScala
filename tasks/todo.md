# protoScala — task tracker

## Phase 0 — Skeleton (2026-09-22)
- [x] Design spec, language reference, roadmap, status, interop docs
- [x] Platform spec `ProtoMap`
- [x] CMake build against protoCore; `--version`, `--help`
- [x] Unit / conformance / CLI harnesses; hello-world XFAIL
- [x] Plans for Phase P1 and Phase 1
- [x] GitHub repository gamarino/protoScala (public, MIT)

## Phase 1 — Core language, 0.1.0 (2026-09-22)
- [x] Tasks 1–12: lexer, layout, parser, desugar, compiler, VM, primitives,
      script runner, REPL, tutorial chapters 1–5 and 14
- [x] Task 13: `examples/hello.scala`, `examples/fib.scala`, `cli/examples`,
      `benchmarks/cold-start.sh` + `RESULTS.md`, STATUS.md (D9–D25, opcode
      table mirror), ROADMAP.md, LANGUAGE.md (`this` keyword), tutorial D-id
      references, CHANGELOG.md `[0.1.0]`, README.md, `CMakeLists.txt` 0.1.0

## Next
- [ ] Phase P1 — Task 0: maintainer decisions (iterator option, helper location)
- [ ] Phase P2 — plan + Task 0: GC strategy for `ProtoMPSCQueue` with the maintainer
- [ ] Phase 2 — object model, `apply`, `for`, `match`

## Review
Phase 0: build and 6/6 tests verified on 2026-09-22 (2 unit, 3 CLI, 1
conformance XFAIL); the conformance runner was checked to fail on a
non-conforming EXPECT and to pass a matching EXPECT-ERROR.

Phase 1 (0.1.0): 307/307 tests green (199 unit, 94 conformance, 14 CLI) on
2026-09-22; `protoscala --version` prints `protoScala 0.1.0`; cold start
measured with `benchmarks/cold-start.sh` — see `benchmarks/RESULTS.md`
(Release build: script 17.6–18.6 ms, REPL 19.1–20.4 ms median across
21-run passes, one borderline REPL sample over the 20 ms target on a loaded
machine; the default RelWithDebInfo build measured slower and less
consistently). D9–D25 recorded in STATUS.md as provisional, pending
maintainer review.
