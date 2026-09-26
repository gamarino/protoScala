# Silent wrong answers, two crashes and one missing feature

Source: a full run of the Scala 3 compiler's own `tests/run` corpus (1654
single-file tests, corpus commit `a68b419c`). Harness, triage and minimal
reproductions in `../.agent_scratch/scala3-corpus/`; this track's own
measurements in `../.agent_scratch/silent-wrong/`.

Every reference answer in this track was produced with
`tools/scala3-3.9.0/bin/scalac -d out` followed by
`java -cp "$SCALA_HOME/lib/*:out"`, never `bin/scala`.

Corpus in-scope baseline measured with the same scripts: **181/601 = 30.1 %**
(bucket 3), 204/1654 overall. (The Track X note says 183/601; the two extra
passes are timing-sensitive — one test times out at the harness's 10 s limit on
this machine.)

## Silent wrong answers

- [x] 4. `for (case p <- xs)` must filter for every pattern, including a tuple
      pattern. Parser records the `case` keyword; the desugarer inserts
      `withFilter` whenever it is present. Correct D35 and D36.
- [x] 2. A blank line ends a leading-infix continuation. Verified against
      scalac: the blank line is the only missing condition — a dedented
      continuation line still continues (with a warning), and a comment-only
      line is not a blank line.
- [x] 3. `override val` constructor parameter must be visible to the
      superclass constructor body. Distinct path, not a Phase 2 regression.
- [x] 1. Shift counts: decide and document. protoScala's integers are
      unbounded, so there is no width to mask to; `>>>` is implementable
      exactly where the operand is non-negative.

## Decide, do not assume

- [x] 5. Type-directed widening: implement where the declared type is written
      at the point of declaration; document the rest.
- [x] 6. Top-level `def` overloads: arity-based dispatch; document what needs
      types.

## Crashes and the missing feature

- [x] 7. `unordered_map::at` escaping the compiler (D74 says that is a bug).
- [x] 8. Spurious "cyclic inheritance: Enum extends itself"; `case C()`.
- [x] 9. `type` aliases.

## Standards held to

- Full suite green before every push; baseline 1299/1299 against protoCore
  2.4.0 (`d7d03b42`).
- Every fixture shown to fail without its fix, with the mutation named.
- Corpus re-measured with the same scripts; zero regressions across all 1654
  files.

## Review

**Measured with the corpus scripts, copied into
`../.agent_scratch/silent-wrong/` and pointed at the read-only corpus.**

| | in-scope (bucket 3) | whole corpus | internal errors |
|---|---|---|---|
| before | 181/601 = 30.1 % | 204/1654 | 3 |
| after  | 191/601 = 31.8 % | 216/1654 | 0 |

**Zero regressions**: the before and after pass sets were compared test by test
across all 1654 files, and no test that passed before fails after. The one
timeout (`hashCodeDistribution`) is the same file before and after.

Newly passing, in-scope unless noted: `enum-List1`, `i15723`, `i5067`, `i7031`,
`lazyVals`, `stable-enum-hashcodes`, `t6070`, `t6443-by-name`, `t6443-varargs`,
`t6968`, plus `enum-Option` (bucket 1) and `i14587.opaques` (bucket 2).

**Outcome per item.** 1 decided and documented (D112), with `>>>` implemented
where it has an answer; 2, 4, 7, 8 fixed; 3 fixed for the reported shape with the
residue as D108; 9 implemented with D109 as the remainder; 5 and 6 partly fixed
with the remainder documented (D110, D111). Item 3 was a **distinct path**, not a
Phase 2 regression: both Phase 2 commits are present verbatim.

**Corrected deviations.** D1 (shift behaviour follows from having no width, not
from overflow), D15 (`>>>`), D19, D31 (overloading is impossible in a *template*),
D35 and D36. New: D108-D112, with D109, D110 and D112 flagged for the maintainer
in `docs/DECISIONS-LOG.md`.

**Suite:** 1299/1299 at the start, 1343/1343 at the end, green before every one
of the eight pushes. (Both totals are against protoCore 2.4.0. Re-measured from
clean against protoCore 2.5.0 on 2026-09-25 the suite is **1344**, 0 failed, 7
skipped: the whole of the step is protoCore adding one conformance rule,
`mutable.graph_cycles`, which this suite is parameterised over.)

---

# Phase 7 — the C++ transpiler (2026-09-26)

Plan: `docs/plans/2026-09-25-phase-7-transpiler.md` (Task 0 plus 17 tasks).
Scratch, baselines and measurements: `../.agent_scratch/phase7-transpiler/`.

## Done

- [x] **Task 0** — all twelve decisions taken on the agent's authority and recorded
      in `docs/DECISIONS-LOG.md` as `[agent, pending review]`, each with its argument
      and the cost of reversing it. Four more decisions the plan did not anticipate
      are recorded beside them (T0-13, T0-14 and two mechanical consequences of D3).
- [x] **Task 1** — gate, baselines, and three of the plan's numbers corrected from
      the tree: the deviation floor is **D113** (not D103), **60** opcodes are defined
      (not 54) with 82 as the object-model band's lowest free value (not 81), and the
      registered conformance suite is **919** fixtures (not 841). Task 1 Step 5's
      verbatim paragraph was deliberately not copied, because it asserts a cold-start
      budget that had been refuted hours earlier.
- [x] **Task 2** — `libprotoScala.so` with `SOVERSION 1` and exactly one installed
      header; CMake package; 9 unit cases through the shared library alone.
- [x] **Task 3** — `src/runtime/OpcodeOps.h`: one opcode implementation, two
      consumers. Cycle gate measured interleaved (no regression; a win reported, not
      claimed). 17 unit cases, 16 of them shown red by one of 17 named mutations.
- [x] **Task 4** — `protoscalac`: command line, toolchain resolution for both trees,
      generated `Makefile`, exit statuses, CLI test.
- [x] **Task 5** — `src/compiler/CppTables.{h,cpp}`: one escaper, one exact-double
      literal, one flattening, shared with the prelude image. Evidence: the generated
      `PreludeImage.cpp` is byte-identical after the move.
- [x] **Task 6, Task 7 in part** — the frame, the slot layout, the straight-line
      opcodes, calls, sends, closures, pattern matching, `TupleN`.
- [x] **Task 10 Step 5** — `protoscala --run-module`.
- [x] **Task 11** — the differential harness, its three anti-rot guards, the
      exclusion list generated from a measured sweep, and 919 CTest cases.
- [x] **Task 12 in part** — the emitter mutation matrix.
- [x] **Task 13 in part** — `check()` walks first and emits nothing;
      `--report-purity`; the refusals CLI test.
- [x] **Task 16** — `docs/PROTOSCALAC_SPECIFICATION.md`.
- [x] **Task 17 in part** — STATUS deviations D113–D123, ROADMAP, CHANGELOG,
      README, LANGUAGE, INTEROP, DESIGN cross-references, tutorial chapter 17 with
      two fixtures.
- [x] **Added beyond the plan** — the **corpus** differential, on the rule the
      maintainer substituted for the plan's own.

## Not done, in the order they should be taken

- [ ] **Classes, traits and objects (D118).** 305 of the 500 excluded fixtures and
      179 of the 186 corpus refusals. Nothing else moves the corpus number by more
      than a handful, so this is not one item among several: it is the item.
- [ ] **Task 8 — the frame's retry loop (D120).** `try`/`catch`/`finally`. The
      `switch` must be inside the `try`, and the frame must be able to catch a
      second exception; getting that wrong was measured at 11 red fixtures.
- [ ] **Task 9 Step 4 — load-time import resolution (D123).**
- [ ] **Task 9 Step 2 — `emitExports`**, so a compiled module keeps early type
      binding instead of degrading to a foreign module.
- [ ] **Task 10 Steps 1–4, 6 — `CompiledModuleProvider`** and the
      **cross-runtime-call demonstration**. The second is the phase's headline
      capability: argued, artefact in hand, **not demonstrated**.
- [ ] **Task 14 — packaging verified end to end in a scratch prefix** with
      `env -u LD_LIBRARY_PATH`.
- [ ] **Task 15 — measurement.** Start-up (criterion: no regression), load time,
      throughput with the expectation written first, and the compile-cost table that
      justifies `-O2` or overturns it.
- [ ] **Version.** Deliberately **not** bumped to 0.7.0: the phase has not shipped.

## Review

What went right: consuming bytecode meant the whole front end was reused unchanged,
so no Scala semantics was reimplemented anywhere; and building the differential
before trusting the design is what caught four silent wrong answers.

What the plan got wrong, and it is worth recording: its `gen::makeFn` design cannot
work, because `compiledModuleOf` reads `__code__` as a `BytecodeModule*` and
nineteen sites depend on that. The answer turned out to be smaller than either
option the plan considered — a transpiled block simply **is** a `BytecodeModule`,
one with no code and a native entry — and it needed one field and one branch.

What I would do differently: measure the corpus differential **first**. It says in
one table that the fixture corpus is not shaped like real Scala, and it would have
put classes at the top of the task list on day one instead of after the emitter was
written.
