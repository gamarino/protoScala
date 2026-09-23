# protoScala Roadmap

Versioning: `0.0.1` until Phase 1 is complete; each completed phase bumps the
minor version (`0.1.0` after Phase 1, ...). Releases are tagged `vX.Y.Z`.

Each milestone has a **Goal** and a verifiable **Done when**. A milestone is
done only when its conformance fixtures pass, [STATUS.md](STATUS.md) is updated,
**its tutorial chapters are written or extended** (see the Documentation track)
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

## Phase P1 — `ProtoMap` in protoCore *(platform)* ✅ — merged and released in protoCore 2.0.0

**Goal:** a persistent map whose keys are GC-traced objects
([platform/PROTOMAP-SPEC.md](platform/PROTOMAP-SPEC.md)).
**Done when:** the type and (if the maintainer adopts it) the hashed-collection
helper are merged in protoCore with the tests of PROTOMAP-SPEC §5; protoCore's
full suite passes; every embedder was rebuilt from clean and passes its suite.
**Prerequisite for:** Phase 3 (`Map`/`Set`), tracks C and S.
**Plan:** [plans/2026-09-22-phase-p1-protosparselistobject.md](plans/2026-09-22-phase-p1-protosparselistobject.md).
**Decided (2026-09-22):** iterator option (a) (PROTOMAP-SPEC §3.3); the
hashed-collection helper lives in protoCore (§4).

## Phase P2 — `ProtoMPSCQueue` in protoCore *(platform)*

**Goal:** a lock-free multi-producer / single-consumer queue whose items the
GC traces, used as the actor mailbox of every runtime
([platform/PMQ-SPEC.md](platform/PMQ-SPEC.md); maintainer decision on R9).
**Opens with:** Task 0 — the GC strategy that satisfies PMQ-SPEC §3, agreed
with the maintainer.
**Done when:** the type is merged in protoCore with the tests of PMQ-SPEC §5
(including TSan and GC-pressure runs); the microbenchmark table is recorded;
protoCore's full suite passes; every embedder was rebuilt from clean and passes
its suite.
**Prerequisite for:** Phase 5, and the mailbox part of tracks C and S.
**Plan:** written when P2 starts (after P1).

## Phase 1 — Lexer, parser, core evaluator, REPL ✅ (2026-09-22)

**Goal:** run straight-line and functional Scala 3 programs.
**Done when:**
- lexer unit tests cover every token class and every offside-rule region
  (DESIGN §3.2), with braces and indentation variants of each fixture;
- programs using `val`/`var`/`def`, `if`/`while`, lambdas, recursion, integer
  and string arithmetic, `println` run, in both brace and indentation syntax;
- deep recursion raises `StackOverflowError` (no crash);
- the REPL evaluates expressions and definitions with readline history and
  multi-line continuation;
- `examples/hello.scala` and `examples/fib.scala` run; cold start < 20 ms (the target became < 25 ms in Phase 2, see DESIGN §1).
**Plan:** [plans/2026-09-22-phase-1-core-language.md](plans/2026-09-22-phase-1-core-language.md).

## Phase 2 — Object model, apply, for, match ✅ (2026-09-23)

**Goal:** idiomatic Scala data modelling.
**Done when:** classes, objects, companions, traits with Scala linearization
(unit-tested against `scalac`-documented examples), case classes with all
synthesised members, universal `apply`, for-comprehensions over `List`, and
pattern matching (all DESIGN §5.3 patterns) pass their fixtures.
**Plan:** [plans/2026-09-22-phase-2-object-model.md](plans/2026-09-22-phase-2-object-model.md).
**Delivered beyond the criteria:** plain `super` including stackable traits
(Open question Q5), an `Option` prelude written in protoScala and a minimal
`List` moved up from Phase 3 (Q7, Q8), `SEND_KW` for named arguments to native
methods (Q9), tuples `Tuple2`..`Tuple22` as case classes, REPL definitions of
classes, traits, objects and case classes, and the `attr_lookup` and
`object_tree` benchmarks. Provisional deviations D28–D42 are recorded in
[STATUS.md](STATUS.md#provisional-deviations-phase-2--pending-maintainer-decision).

## Phase 3 — Fast paths, collections, prelude

**Goal:** a usable standard library.
**Done when:** SmallInteger fast paths with LargeInteger promotion are
covered by boundary fixtures (±2^53); `List`, `Vector`, `Range`, `Option`,
`Either`, `Try`, tuples and string interpolation pass their fixtures
(`Option`, tuples and a minimal `List` already landed in Phase 2 — Q7, Q8 —
so this phase completes the `List` surface and adds the rest);
`Map`/`Set` pass on `ProtoMap` (requires P1); benchmark suite
v1 (`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`) self-reports and is
recorded in `benchmarks/RESULTS.md`.

## Phase 4 — Exceptions, super, enums

**Goal:** complete core semantics.
**Done when:** `try`/`catch`/`finally`/`throw` with pattern-matched handlers,
native error translation, `super[T].m`, `enum`, sealed
hierarchies, named and default arguments for Scala-defined methods, extension
methods and templates nested in an `object` pass.

**Landed early (2026-09-23), with the D47 ruling:** by-name parameters
(`x: => T`). They were not scheduled for any phase; `Future(expr)` needs them,
so they were implemented as a general feature. What remains for a later phase is
the part D53 records — honouring a by-name parameter at a call site the compiler
cannot resolve to a declaration, which needs more than erased types give.

**Already done in Phase 2** (Open question Q5): plain `super.m` is the same
DESIGN §4.4 algorithm whether or not stackable traits are involved, so
stackable traits work today — the classic
`Doubling`/`Incrementing` queue example is the fixture
`tests/conformance/07-classes/stackable-traits.scala`. What remains for this
phase is `super[T].m`, which names the ancestor explicitly instead of taking
the next one in the linearization, and its done-when fixture.

## Phase 5 — Actors and futures ✅ (2026-09-23, 0.3.0)

**Goal:** GIL-free concurrency on protoClojure's actor model (DESIGN §8).
**Requires:** Phase P2 (`ProtoMPSCQueue` mailboxes; R9 decided 2026-09-22) — shipped without it, through the `Mailbox` seam on a CAS'd `ProtoList`, because protoCore 2.0.0 does not carry `newMPSCQueue` yet.
**Done when:**
- `Actor.spawn`, `!`, `?`, `send`/`ask` with `Priority`, `value`,
  `Actor.isActor`, `Actor.stats`, `Future` (`await`, `map`, `flatMap`,
  `recover`, `Future.apply`) pass their fixtures;
- the single-method invariant holds under a concurrent-senders race fixture
  (protoClojure `concurrent-sends-no-race` shape), priorities are observable,
  a handler exception yields `Failure` and leaves the actor alive;
- `await` inside an actor suspends cooperatively (the `await` benchmark
  completes with one worker);
- every queued payload is GC-reachable (GC-pressure fixtures pass under a low
  memory limit), and shutdown joins all workers on every exit path;
- `benchmarks/actor-bench.sh` runs all seven modes of DESIGN §8.5, verifies
  message counts, and `RESULTS.md` records the table next to protoClojure's
  numbers on the same machine and date;
- tutorial chapter 13 (actors and futures) is written — 13, not 12: TUTORIAL.md's chapter table is the live list and numbers it 13.

## Phase 6 — UMD and packaging

**Goal:** a polyglot, installable runtime.
**Done when:** `ScalaModuleProvider` loads `.scala` modules; `py.`, `js.`,
`st.`, `clj.` prefixes route to their providers (tested with a stand-in
provider, and with protoST's provider when both are built); CPack produces a
`.deb` and a `.tar.gz` that install and run `protoscala` without
`LD_LIBRARY_PATH`. Release `0.6.0`.

## Documentation track (every phase)

**Goal:** documentation and a dual-audience tutorial ([TUTORIAL.md](TUTORIAL.md))
for traditional Scala programmers and for developers coming from Python or
JavaScript, as in protoClojure and protoST.
**Done when (per phase):** the chapters listed for that phase in TUTORIAL.md
are written; chapter 2 (Python/JavaScript bridge) and chapter 3 (departures
from Scala 3, keyed to the D-ids) are extended with the phase's features;
every runnable snippet has a matching fixture in
`tests/conformance/tutorial/`; LANGUAGE.md, STATUS.md, README and CHANGELOG
reflect the phase.
**Final deliverable (Phase 6):** all 15 chapters, the worked example, and a
"protoScala in 10 minutes" section in the README for each audience.

## Track C — protoClojure onto the new protoCore types *(platform)*

**Goal:** remove protoClojure's custom map/set layout, its `ProtoTuple`
vectors (DESIGN R2) and its unrooted actor mailboxes.
**Done when:** protoClojure maps and sets use `ProtoMap` (via the
shared helper if adopted); vectors no longer retain memory perennially; the
full protoClojure suite passes; no ordering guarantee beyond Clojure's is
introduced; actor mailboxes use three `ProtoMPSCQueue`s per actor, with
`actor-bench.sh` tables recorded before and after. Requires P1, P2 and a
maintainer decision on R2.

## Track S — protoST onto the new protoCore types *(platform)*

**Goal:** one mechanism for protoST's `Dictionary` and `Set`, and GC-traced
lock-free mailboxes.
**Done when:** both use `ProtoMap`; protoST's D32 is resolved by
the protoST language decision it is waiting for; actor mailboxes use
`ProtoMPSCQueue` with actor benchmark tables recorded before and after; the
full protoST suite passes. Requires P1 and P2.

## Later (v0.7+)

- Implicits/givens with a dynamic resolution scheme (if a sound design
  exists), `SortedMap`/`ListMap`, lazy collections (`LazyList`, views),
  threaded-goto dispatch if profiling justifies it, debugger (DAP) following
  protoST, editor integration.
