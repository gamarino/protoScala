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
