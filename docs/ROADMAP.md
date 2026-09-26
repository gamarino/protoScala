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

## Phase P2 — `ProtoMPSCQueue` in protoCore *(platform)* ✅ — merged and released in protoCore 2.1.0

**Goal:** a lock-free multi-producer / single-consumer queue whose items the
GC traces, used as the actor mailbox of every runtime
([platform/PMQ-SPEC.md](platform/PMQ-SPEC.md); maintainer decision on R9).
**Opened with:** Task 0 — the GC strategy satisfying PMQ-SPEC §3, decided as
the retain-chain design and validated by a test that fails without it
(PMQ-SPEC §7).
**Done when:** the type is merged in protoCore with the tests of PMQ-SPEC §5
(including TSan and GC-pressure runs); the microbenchmark table is recorded;
protoCore's full suite passes; every embedder was rebuilt from clean and passes
its suite.
**Prerequisite for:** Phase 5, and the mailbox part of tracks C and S.
**Plan:** [plans/2026-09-23-phase-p2-protompscqueue.md](plans/2026-09-23-phase-p2-protompscqueue.md).

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

## Phase 3 — Fast paths, collections, prelude ✅ (2026-09-23, 0.4.0)

**Goal:** a usable standard library.
**Done when:** SmallInteger fast paths with LargeInteger promotion are
covered by boundary fixtures (±2^53); `List`, `Vector`, `Range`, `Option`,
`Either`, `Try`, tuples and string interpolation pass their fixtures
(`Option`, tuples and a minimal `List` already landed in Phase 2 — Q7, Q8 —
so this phase completes the `List` surface and adds the rest);
`Map`/`Set` pass on `ProtoMap` (P1, ✅ merged and released in protoCore 2.0.0,
with the hashed-collection helper); benchmark suite
v1 (`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`) self-reports and is
recorded in `benchmarks/RESULTS.md`.

**Every clause is met.** 911 tests green (561 conformance, 316 unit, 22 CLI,
12 benchmark), and green at `PROTOSCALA_ACTOR_WORKERS=1` and `=16`; under
`PROTOCORE_HEAP_LIMIT_CELLS=20000` one pre-existing Phase 5 mailbox test aborts
(STATUS "Open bugs"). Plan:
[plans/2026-09-23-phase-3-collections.md](plans/2026-09-23-phase-3-collections.md).
Deviations D54–D71 (D57, D60 and D64 unused), pending review.

**Delivered beyond the criteria:** the `unary_-`, `unary_!` and binary operator
symbols are no longer interned on every execution (a measured 10.0 % cycle
reduction on an operator-overload loop, `perf stat -r 3`); `->` on `Any`; the
`String` surface of the plan's Task 11 (`split`, `replace`, `stripMargin`,
`format`, and the collection-like methods); and `O(args)` on an object now
honours `apply`'s by-name parameters as `O.apply(args)` already did, which
widens D53's reach.

**Not delivered, and deliberately:** the plan's Task 1 Step 2, a SmallInteger
fast path on `EQ`/`NE`. It was written, measured and backed out under the plan's
own 3 % rule — `valuesEqual` already fast-paths two SmallIntegers, so it bought
nothing on its own workload and cost 8 % of `sum_loop`'s cycles through code
layout. The measurement is recorded at the opcode and in DECISIONS-LOG.

## Phase 4 — Exceptions, super, enums ✅ (2026-09-24, 0.5.0)

**Goal:** complete core semantics.
**Done when:** `try`/`catch`/`finally`/`throw` with pattern-matched handlers,
native error translation, `super[T].m`, `enum`, sealed hierarchies, named and
default arguments for Scala-defined methods, **multiple constructor parameter
lists**, extension methods and templates nested in an `object` pass. ✅ All of
them, in both brace and indentation syntax, with the tutorial chapters written
and the suite green in four configurations.

*(The done-when gained `multiple constructor parameter lists` on 2026-09-24:
LANGUAGE §3 assigned them to this phase, ROADMAP's list omitted them and STATUS
listed them with no phase at all. The contradiction was resolved in favour of
LANGUAGE and all three documents now agree — D84.)*

**Delivered beyond the criteria:** `Priority` became a real prelude `enum`
(retiring **D52**); `Failure` carries the `Throwable` itself and `RuntimeError` is
gone from the prelude (retiring **D44**); a failed `await` raises at its own call
site so an enclosing `try` catches it across a cooperative suspension (retiring
**D50**); a custom string interpolator is an extension method on a new prelude
`StringContext` (retiring **D56**); the keyword-argument convention is documented
in [INTEROP.md](INTEROP.md) §7 and exercised end to end against a stand-in
callee; and a default value may read an enclosing local as well as an earlier
parameter.

**Ordering coupling with Phase 3 (resolved):** Phase 3 extended `Try` with
combinators but deliberately did not re-point `Failure`'s payload off
`RuntimeError`; Phase 4 did it, in one prelude edit plus the `await` raise path.
Phase 3's two `XFAIL` fixtures waiting on `enum`
(`tests/conformance/18-maps-and-sets/map-enum-case-keys.scala` and
`map-parameterised-enum-case-keys.scala`) are flipped and verified against scalac
— and the *prose* of the first one was corrected: a singleton `enum` case is a
case object, so it carries the synthesised `product_equals` and takes the **value**
path, not the identity path. The two are indistinguishable at run time because a
case object has exactly one instance, which is why only the white-box test in
`tests/unit/test_collections.cpp` can tell.

**Landed early (2026-09-23), with the D47 ruling:** by-name parameters
(`x: => T`). They were not scheduled for any phase; `Future(expr)` needs them,
so they were implemented as a general feature. What remains for a later phase is
the part D53 records — honouring a by-name parameter at a call site the compiler
cannot resolve to a declaration, which needs more than erased types give.

**Already done in Phase 2** (Open question Q5): plain `super.m` is the same
DESIGN §4.4 algorithm whether or not stackable traits are involved, so
stackable traits worked before this phase; what this phase added is `super[T].m`.

**Not delivered, and deliberately:** a `class`, `trait` or `object` nested in a
**`class`** or **`trait`**, a local class inside a block, and the anonymous-class
form `new T { … }` (**D80**). Each captures the enclosing instance, which needs a
per-instance class — real machinery, and a different feature. It is the one item
of this phase a reasonably common Scala idiom touches, so it is recorded under
"Later" below rather than left only as a deviation.

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

## Phase 6 — UMD and packaging ✅ (2026-09-24, 0.6.0)

**Goal:** a polyglot, installable runtime.
**Done when:** `ScalaModuleProvider` loads `.scala` modules; `py.`, `js.`,
`st.`, `clj.` prefixes route to their providers (tested with a stand-in
provider, and with protoST's provider when both are built); CPack produces a
`.deb` and a `.tar.gz` that install and run `protoscala` without
`LD_LIBRARY_PATH`. Release `0.6.0`.

**What it delivered**, against that done-when: `ScalaModuleProvider` (alias
`scala`, GUID `protoScala-source-v1`) loads `.scala` modules for protoScala's own
`import` and for protoCore's resolution chain; all four prefixes route; a
`dlopen`'d stand-in provider exercises the routing, a member import, a named
argument across the boundary (including a parameter name too long to embed in a
pointer word) and a provider failure caught as a Scala exception; and both a
`.deb` and a `.tar.gz` at 0.6.0 were built, extracted and run under
`env -u LD_LIBRARY_PATH`, with `ldd` confirming libprotoCore resolved from inside
the package. **Beyond the done-when**, the prelude became a build-time image, the
boundary catch shape gained the clause D74 needs and `callNative` gained the
last-resort clause it lacked.

**What it deliberately did not do**, each with its reason recorded:

- *"tested … with protoST's provider when both are built"* is **partly met and
  the gap is measured, not hand-waved.** Two runtimes were built in one process
  and both providers stayed reachable — R5's first real evidence since Phase 0 —
  but a cross-runtime import misses, because `tryLoad` receives the *caller's*
  context and every provider in the family resolves its runtime from
  `ctx->space`. A provider serves only callers that share its object space. The
  end-to-end test is kept `DISABLED` in its failing shape rather than deleted or
  weakened. See R5 in [STATUS.md](STATUS.md).
- The two Python `XFAIL` fixtures were **not converted**; see the first hand-off
  below and **Track Y**.

Four things Phase 4 left for this phase to finish:

- ~~**Convert `tests/conformance/23-named-arguments/foreign-python-keyword.scala`
  and `foreign-python-open-encoding.scala` from `XFAIL` to `EXPECT`.**~~
  **Moved to Track Y, and this clause was wrong.** It said "what is missing is
  only `import py.…` routing"; the routing shipped in this phase and the
  fixtures still cannot be converted, because **no runtime in the family
  registers the alias `py`** (protoPython registers `native`, `python_stdlib`,
  `compiled` and `hpy`), its providers come with a whole `PythonEnvironment`
  (R5), and `protoscala` links no sibling runtime. Both fixtures stay `XFAIL`
  with their recorded output and a directive that names the real blocker.
  Amending a done-when is the maintainer's, so this edit is
  **[agent, pending review]** — see [DECISIONS-LOG.md](DECISIONS-LOG.md),
  Phase 6 E7, and plan A0-12.
- **Add the boundary catch shape at every UMD and foreign-call site**, in this
  order: `catch (FutureYield&) { throw; }`, `catch (ScalaThrow&) { throw; }`,
  `catch (ScalaError&) { throw; }`, `catch (const std::exception& e) { throw
  ScalaError("RuntimeException", e.what()); }`, `catch (...) { throw
  ScalaError("RuntimeException", "native exception"); }` — the control signals
  pass through and everything else is translated (protoST's mandatory §6).
- **Decide whether extension methods gain import scoping** (D82). The import
  mechanism arrives here, and scoping an extension is a public-surface change that
  needs it.
- ⚠️ **Precompile or cache the prelude, and reclaim the cold-start budget.**
  **The prelude work is done; the budget is NOT met.** This is a correction: this
  item read "Done, and the budget is met" on the strength of a 0.6.0 measurement
  of script 23.73 ms and REPL 23.89 ms (three rounds interleaved, all twelve cases
  `verified=21`). A re-measurement on 2026-09-26 **refuted it and that figure did
  not reproduce**: at load average 1.84 — *lower* than the 2.97 of the original —
  the shipped `Release` binary measures **26.38 ms script / 25.43 ms REPL**, and
  `benchmarks/cold-start.sh` exits **1 in 12 of 12 cases** (both builds, both
  modes, all 252 runs verified). Load does not explain the gap and **its cause is
  unidentified**. Two operands, stated and not deducted: the harness's own floor
  is ~5 ms, so ~21.4 ms of the 26.38 is protoScala; and start-up is kernel-bound
  (0.26 s user against 1.07 s sys over 42 runs), which is a direction to look, not
  a defect. What the *image* is worth is unaffected — the figures below stand as
  the 2026-09-24 measurement. The same binary with `PROTOSCALA_PRELUDE_NO_IMAGE=1` still
  measures 25.65 / 26.01 ms, which is what proves the image moved the number. The
  prelude became a **build product**, not a cache, so it cannot go stale. What no
  protoScala change can remove is `linkSymbols` and running the compiled prelude
  (342 µs + 177 µs); that needs a protoCore space image, which does not exist, and
  stays open on its merits rather than as a blocker (escalation E5). The original
  statement of the problem follows.

  0.5.0 missed DESIGN §1's < 25 ms by about 1 ms, and the cause was measured: roughly
  60 µs per prelude class, so the growth from 156 to 200 lines costs +1.31 ms
  (a probe build with twenty more classes of the same shape costs a further
  +1.19 ms — benchmarks/RESULTS.md). The prelude is parsed, desugared, compiled
  and run at every start-up with nothing cached. Module loading arrives in this
  phase, which is where a serialised prelude belongs.

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

## Track F — file I/O *(language)* — **DELIVERED (2026-09-25)**

**Goal:** a program can read its own input. **Met.**

The maintainer ruled the shape: **be faithful to Scala when reading**, because
`scala.io.Source` is real Scala standard library; **do not simulate
`PrintWriter`** when writing, because protoScala has no Java interop and never
will, and imitating one would drag in half a stream hierarchy for nothing.

Delivered:

- **Reading — `scala.io.Source`**, under Scala's own names:
  `Source.fromFile(path)`, `Source.fromFile(path, enc)`, `Source.fromString(s)`,
  and `mkString`, `getLines()`, `close()`, `isOpen` on the `BufferedSource` they
  answer. Every checkable behaviour was checked against **scalac 3.9.0** rather
  than assumed: which exception class each failure raises, the `<path> (<reason>)`
  shape of its message, and every rule for splitting lines (on `\n`, `\r\n` and a
  lone `\r`, terminator stripped, no final empty line for a trailing terminator, no
  lines at all for an empty file).
- **Writing — `FileIO.write` / `append` / `exists` / `delete`** (D102), four
  operations and one call each, documented as the deviation the absence of Java
  causes.
- **Errors.** Every syscall's result is checked; every failure raises a prelude
  `Throwable` a Scala programmer would catch, with a message naming the path;
  `IOException`, `FileNotFoundException`, `CharacterCodingException` and
  `MalformedInputException` join the hierarchy (D97). UTF-8 decoding is strict like
  the JVM's. A path holding a NUL byte is refused rather than truncated at the NUL.
- **The worked example opens `sample.log`.** The second copy it carried in
  `report/Sample.scala`, and the diff that kept the two in step, are both gone;
  `tests/cli/examples.sh` checks instead that the program reads the file it is
  given, by running it against an edited copy and demanding a different report.
- **Tutorial chapter 16**, dual-audience, with a fixture per runnable snippet.
- **44 conformance fixtures**, each one shown to be capable of failing by a
  mutation of the implementation.

Deviations recorded: **D97–D102** in [STATUS.md](STATUS.md).

**Not** delivered, and now the file-shaped gap: directories (no `mkdir`, no
listing, no rename), binary files and random access, any encoding but UTF-8 (D99),
streaming — there is no `Iterator`, so a file is read whole and one larger than
memory cannot be processed (D100) — and stdin. Each is a separate decision and
none of them blocked the goal.

## Track Y — a working cross-runtime import *(platform)*

**Half delivered.** `import st.<module>` works; `import py.<module>` does not.

### Delivered — `st`, and the no-copy property

A protoScala program imports a protoST module, binds its members and shares its
values with **no copy at the boundary**: the same cell, the same `getHash`, read
from both runtimes, with both addresses printed by
`umd/protost-interop`. Phase 6's diagnosis was right about the mechanism and wrong
about the remedy: it concluded that serving a caller in another `ProtoSpace` needed
a change to the UMD contract, and therefore to protoCore. It did not.
`ModuleProvider` is an object with its own state, so a provider takes its runtime
from that state and uses `ctx` only to allocate the result in the caller's context
— which is what that argument is for. protoST `e82682b` and protoScala `933e75d`,
**no protoCore change**.

Two further per-space facts came out of it and are recorded in
[INTEROP.md](INTEROP.md) §6: interned symbols are per `ProtoSpace`, so a provider
must re-key the module namespace in the caller's space (and a short name matching
by pointer-word accident is the trap); and a module's top level must run in the
provider's own space, or its literals intern in the wrong symbol table.

Still not demonstrated, and stated so nobody infers more: a cross-runtime **call**
(a protoST method is `__bc_ptr__` + protoST's engine, not a `proto::ProtoMethod`),
imports from more than one thread, more than one protoST runtime per process, and a
namespace that changes after import. **R5 itself remains a maintainer ruling**;
this is evidence for it.

The maintainer has since ruled on the reachability half: a module is a
**process-level** entity, its list is global and therefore perennial, and it
anchors its own contents through its variables — so a loaded module is not owned
by a space and there is no cross-space GC edge to reason about. That makes
`SharedModuleCache`'s path-only key the correct identity rather than the hazard
Phase 6 recorded. What is left is reported under R5 in [STATUS.md](STATUS.md) and
belongs to protoCore: today the retention is per-runtime rather than one perennial
global root, `getImportModule` cannot address a *named* provider, and the cache key
drops the family prefix so two languages' modules of the same path would be one
entry. Nothing here was changed on that account.

### Remaining — a `py` provider

**Goal:** make `import py.numpy as np` work from protoScala.
**Done when:** protoPython registers the alias `py` (or an agreed alias
protoScala's prefix table names), its provider serves a caller in another
`ProtoSpace` the way protoST's now does, and it ships a provider plug-in exporting
protoScala's `protoScala-provider-1` ABI ([INTEROP.md](INTEROP.md) §3.1); a
`protoscala` process that loads it imports a Python module and calls it with named
arguments; `tests/conformance/23-named-arguments/foreign-python-keyword.scala` and
`foreign-python-open-encoding.scala` convert from `XFAIL` to `EXPECT` with the
output they already record.

Four blockers, measured from protoPython's source on 2026-09-24 and written up in
[INTEROP.md](INTEROP.md) §6.1: no `py` alias; `PythonEnvironment::fromContext`
ignores its `ctx` and returns a `thread_local`; `PythonEnvironment::getProcessSpace`
is a function-local static ProtoSpace, one per process by design, which a
co-resident protoScala `Session` contradicts; and protoPython ships **no numpy**, so
`foreign-python-keyword.scala` cannot produce its recorded output however the
plumbing is arranged. The first three are `PythonEnvironment`'s ownership model,
which is R5's question; they are not a provider-local change like protoST's was.

Changes to protoPython, which is a different repository and follows its own
conventions.

## Track S — protoST onto the new protoCore types *(platform)*

**Goal:** one mechanism for protoST's `Dictionary` and `Set`, and GC-traced
lock-free mailboxes.
**Done when:** both use `ProtoMap`; protoST's D32 is resolved by
the protoST language decision it is waiting for; actor mailboxes use
`ProtoMPSCQueue` with actor benchmark tables recorded before and after; the
full protoST suite passes. Requires P1 and P2.

## Later (v0.7+)

- **Templates nested in a `class` or a `trait`, local classes inside a block, and
  anonymous classes `new T { … }`** — a reasonably common Scala idiom that Phase 4
  deliberately left out (D80), because a class-nested template captures the
  enclosing instance and needs a per-instance class.
- Implicits/givens with a dynamic resolution scheme (if a sound design
  exists), `PartialFunction` and `collect` (D63), `SortedMap`/`ListMap`, lazy
  collections (`LazyList`, views), threaded-goto dispatch if profiling justifies
  it, debugger (DAP) following protoST, editor integration.
