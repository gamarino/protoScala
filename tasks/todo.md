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

See the final report and `docs/STATUS.md`.
