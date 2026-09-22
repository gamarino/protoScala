# protoScala Roadmap

Versioning: `0.0.1` until Phase 1 is complete; each completed phase bumps the
minor version (`0.1.0` after Phase 1, ...). Releases are tagged `vX.Y.Z`.

Each milestone has a **Goal** and a verifiable **Done when**. A milestone is
done only when its conformance fixtures pass, [STATUS.md](STATUS.md) is updated
and the full test suite is green. Platform phases (P, C, S) change other
repositories; they follow those repositories' conventions and are merged there.

## Phase 0 — Skeleton ✅ (2026-09-22)

**Goal:** a repository that builds against protoCore and runs its test
harnesses.
**Done when:** `cmake -B build_release -S . && cmake --build build_release &&
ctest --test-dir build_release` passes; `protoscala --version` prints the
version; a unit test creates a `ProtoSpace` and exercises SmallInteger
promotion, proving protoCore links and runs; the conformance runner registers fixtures,
including one `XFAIL` that pins the first Phase 1 program.

## Phase P1 — `ProtoSparseListObject` in protoCore *(platform)*

**Goal:** a persistent map whose keys are GC-traced objects
([platform/PSLO-SPEC.md](platform/PSLO-SPEC.md)).
**Done when:** the type and (if the maintainer adopts it) the hashed-collection
helper are merged in protoCore with the tests of PSLO-SPEC §5; protoCore's
full suite passes; every embedder was rebuilt from clean and passes its suite.
**Prerequisite for:** Phase 3 (`Map`/`Set`), tracks C and S.
**Plan:** [plans/2026-09-22-phase-p1-protosparselistobject.md](plans/2026-09-22-phase-p1-protosparselistobject.md).
**Opens with:** maintainer review of PSLO-SPEC §3.3 (iterator) and §4 (helper
location).

## Phase 1 — Lexer, parser, core evaluator, REPL

**Goal:** run straight-line and functional Scala 3 programs.
**Done when:**
- lexer unit tests cover every token class and every offside-rule region
  (DESIGN §3.2), with braces and indentation variants of each fixture;
- programs using `val`/`var`/`def`, `if`/`while`, lambdas, recursion, integer
  and string arithmetic, `println` run, in both brace and indentation syntax;
- deep recursion raises `StackOverflowError` (no crash);
- the REPL evaluates expressions and definitions with readline history and
  multi-line continuation;
- `examples/hello.scala` and `examples/fib.scala` run; cold start < 20 ms.
**Plan:** [plans/2026-09-22-phase-1-core-language.md](plans/2026-09-22-phase-1-core-language.md).

## Phase 2 — Object model, apply, for, match

**Goal:** idiomatic Scala data modelling.
**Done when:** classes, objects, companions, traits with Scala linearization
(unit-tested against `scalac`-documented examples), case classes with all
synthesised members, universal `apply`, for-comprehensions over `List`, and
pattern matching (all DESIGN §5.3 patterns) pass their fixtures.

## Phase 3 — Fast paths, collections, prelude

**Goal:** a usable standard library.
**Done when:** SmallInteger fast paths with LargeInteger promotion are
covered by boundary fixtures (±2^53); `List`, `Vector`, `Range`, `Option`,
`Either`, `Try`, tuples and string interpolation pass their fixtures;
`Map`/`Set` pass on `ProtoSparseListObject` (requires P1); benchmark suite
v1 (`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`) self-reports and is
recorded in `benchmarks/RESULTS.md`.

## Phase 4 — Exceptions, super, enums

**Goal:** complete core semantics.
**Done when:** `try`/`catch`/`finally`/`throw` with pattern-matched handlers,
native error translation, stackable traits with `super` (classic
`Doubling`/`Incrementing`/`Filtering` queue example), `enum`, sealed
hierarchies, named and default arguments, extension methods pass.

## Phase 5 — Actors and futures

**Goal:** GIL-free concurrency.
**Done when:** `Actor.spawn`, `!`, `?`, `await`, three priority bands,
cooperative suspension inside actors, handler failure → `Failure`, clean
shutdown; stress fixtures run under GC pressure without leaks or crashes;
`benchmarks/actor-bench.sh` reports throughput for MPSC, fan-out and ping-pong
and verifies message counts.

## Phase 6 — UMD and packaging

**Goal:** a polyglot, installable runtime.
**Done when:** `ScalaModuleProvider` loads `.scala` modules; `py.`, `js.`,
`st.`, `clj.` prefixes route to their providers (tested with a stand-in
provider, and with protoST's provider when both are built); CPack produces a
`.deb` and a `.tar.gz` that install and run `protoscala` without
`LD_LIBRARY_PATH`. Release `0.6.0`.

## Track C — protoClojure onto `ProtoSparseListObject` *(platform)*

**Goal:** remove protoClojure's custom map/set layout and its `ProtoTuple`
vectors (DESIGN R2).
**Done when:** protoClojure maps and sets use `ProtoSparseListObject` (via the
shared helper if adopted); vectors no longer retain memory perennially; the
full protoClojure suite passes; no ordering guarantee beyond Clojure's is
introduced. Requires P1 and a maintainer decision on R2.

## Track S — protoST onto `ProtoSparseListObject` *(platform)*

**Goal:** one mechanism for protoST's `Dictionary` and `Set`.
**Done when:** both use `ProtoSparseListObject`; protoST's D32 is resolved by
the protoST language decision it is waiting for; the full protoST suite
passes. Requires P1.

## Later (v0.7+)

- Implicits/givens with a dynamic resolution scheme (if a sound design
  exists), `SortedMap`/`ListMap`, lazy collections (`LazyList`, views),
  threaded-goto dispatch if profiling justifies it, debugger (DAP) following
  protoST, editor integration.
