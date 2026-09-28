# The Scala 3 run-corpus measurement

protoScala's published corpus figures — the in-scope **31.8 %** and the
whole-corpus **13.1 %** — come from the Scala 3 (dotty) compiler's own `tests/run`
corpus: 1654 single-file programs nobody here wrote. Until 2026-09-27 the harness,
the triage and every result file lived outside this repository, so the number could
not be reproduced from a clone. This directory is the harness. It is committed for
the same reason the rest of the suite is: *prefer the command to the number.*

## Fetching the corpus

The corpus is a third-party repository and is **not** vendored here. Pinned commit
`a68b419c8f8aaf53b253313ac0b04e57fbda59a0` (`scala/scala3`), which is what every
published protoScala figure was measured against:

```bash
git clone --filter=blob:none --no-checkout https://github.com/scala/scala3.git scala3-corpus
cd scala3-corpus
git sparse-checkout init --cone
git sparse-checkout set tests/run
git checkout a68b419c8f8aaf53b253313ac0b04e57fbda59a0
export SCALA3_CORPUS=$PWD/tests/run     # 1654 *.scala, 754 of them with a .check
```

This step needs network access, and that is the one thing about the measurement
that a clone of protoScala cannot supply on its own.

## Running it

```bash
export SCALA3_CORPUS=/path/to/scala3/tests/run
python3 tools/corpus/run_corpus.py --protoscala build_release/protoscala --out results.jsonl
python3 tools/corpus/triage.py --out triage.jsonl --results results.jsonl
python3 tools/corpus/score.py --results results.jsonl --triage triage.jsonl --both
```

`--limit 25` on the runner is a smoke check. The full sweep runs one process per
file with a 10-second timeout, three at a time by default; `triage.py` is a pure
regex pass over the sources and takes seconds.

The archived run behind the published figures is summarised in
`2026-09-25-track-s.csv` (test, bucket, whether it has a `.check`, and its verdict
under each scoring rule), so the headline is re-derivable with no corpus and no
build:

```bash
python3 tools/corpus/score.py --csv --both
```

which prints `191/601 = 31.8 %` strict and `193/601 = 32.1 %` lenient. The raw
per-test stdout of that run is not committed: it is third-party program output, and
the verdicts are what the figures rest on.

## The rule, and the two readings of it

dotty's rule for `tests/run` is that the program must run and its output must match
its `.check` file. **754 of the 1654 files have a `.check`** (280 of the 601
in-scope ones). For the rest, dotty requires only that the test run, and there are
two defensible readings:

| rule | a test with no `.check` passes when |
|---|---|
| **strict** (published) | it exits 0 **and** prints nothing unexpected |
| lenient | it exits 0, whatever it prints |

They differ by about 0.3 percentage points and the strict rule is the lower one, so
every published protoScala figure is the more conservative of the two. A
progression table must pick one and keep it: strict 12.3 % → 31.8 %, or lenient
12.5 % → 32.1 %. `score.py --both` exists to catch a mixed table.

## What the numbers do and do not include — read before quoting them

- **44 % of the headline pass count is not an output comparison.** Of the 191
  in-scope passes, **107 matched a checkfile** and **84 had none** and passed on
  "exited 0 and printed nothing unexpected". Those are legitimate passes by dotty's
  own rule, and the files are `assert`-based `object Test extends App` programs, so
  they do fail when the assertion fails — but any silent-failure mode that exits 0
  without running the body would score as a pass on the ~900 files that have no
  output to check.
- **The triage is subtractive, so bucket 3 is biased towards tests that pass.**
  `triage.py` is an ordered list of regex rules that hunt for constructs protoScala
  does not claim to have; bucket 3 is defined as the residue where no rule fired.
  Because the rules look for known-missing features, what is left over skews
  towards what works. Some rules are deliberately coarse (`\bSeq\b` fires on any
  occurrence; `\borElse\b` files Option tests under PartialFunction), so bucket 2
  over-approximates and bucket 3 shrinks.
- **13.1 % is the number with no judgement in it; 31.8 % is the number with our
  judgement in it, and the gap is the judgement.**
- **25 tests the triage put out of scope pass anyway** (216 pass overall, 191
  in-scope), which is direct evidence that the triage is over-broad by at least
  that much.
- One file, `hashCodeDistribution`, hits the 10-second timeout on the reference
  machine; it is the only timeout in the archived runs.

## Harness adaptations

Two, and neither is a change to protoScala:

1. A driver line calling the test's `object X { def main }` is appended to the
   source, because protoScala runs a file rather than a class.
2. A pass is also granted when the expected output matches stdout **plus** stderr,
   for tests whose `.check` includes a diagnostic. In the archived runs this
   leniency fired **zero** times: no pass has non-empty stderr.
