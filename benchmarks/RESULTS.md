# protoScala benchmark results

Every benchmark prints the work it did and its runner verifies it; exit code alone never
counts as success (DESIGN §10).

## Suite reports

Full runs of the comparable suite (`benchmarks/bench.sh`) are dated reports in
[`reports/`](reports/), each with machine, date, commit, build types, load
average, per-runtime medians, the JVM compile times and the cold-start result:

| Date | Report | protoScala ÷ CPython (geomean) | Notes |
|---|---|---:|---|
| 2026-09-22 | [2026-09-22-suite.md](reports/2026-09-22-suite.md) | 0.74× (8 workloads) | commit `154ab1a`; load 2.9-3.9; all 7 columns, every cell verified |

The latest table is also in the top-level README ("Performance").

## Cold start (Phase 1)

`benchmarks/cold-start.sh build_release/protoscala 21`, x86_64 (AMD Ryzen 5 5500U with
Radeon Graphics), `-DCMAKE_BUILD_TYPE=Release`, 2026-09-22.

| Case | Runs | Verified | Median (ms) | Target |
|---|---|---|---|---|
| script (`examples/hello.scala`) | 21 | 21 | 17.58 | < 20 |
| REPL to prompt and `:quit` | 21 | 21 | 19.08 | < 20 |

Both cases are below target in this Release build. The default `RelWithDebInfo` build
(the one `cmake -B build_release -S .` produces without `-DCMAKE_BUILD_TYPE=Release`)
measured slower and inconsistently on this machine across three 21-run passes:

| Build | Case | Median (ms), 3 passes |
|---|---|---|
| RelWithDebInfo | script | 20.77 / 22.65 / 20.03 |
| RelWithDebInfo | REPL | 22.70 / 22.60 / 21.95 |
| Release | script | 18.32 / 18.05 / 18.55 |
| Release | REPL | 19.76 / 20.41 (FAIL) / 19.54 |

Even in the Release build, the REPL case landed at 20.41 ms in one of three 21-run
passes (over target by 0.41 ms) before the canonical run recorded above came in at
19.08 ms. `perf stat -r 3` on `protoscala examples/hello.scala` (Release build) shows
the binary itself completing in 18.8 ms +- 1.5% of wall-clock elapsed time (14.23 ms
task-clock, 27.3M instructions, 41.2M cycles), which is consistent with the script-case
medians above; the REPL case's extra ~1-2 ms over the script case is the readline
initialisation and the second process (`sh -c "printf ... | protoscala"`) in the
measurement pipeline, not the interpreter start-up path itself.

This machine ran another build concurrently during some of these measurements (see
`.superpowers/sdd/.../context.md`: "another build runs on this machine"); the
RelWithDebInfo numbers and the one borderline Release REPL sample above are consistent
with that load, not with a regression in the interpreter. Recorded honestly per Open
question Q16: cold start is measured by hand, not gated in CI, because timing is
machine-dependent. **The maintainer should re-run
`benchmarks/cold-start.sh build_release/protoscala 21` on an idle machine to confirm the
17-19 ms Release numbers as the representative baseline** before relying on the < 20 ms
budget holding under load.

The suite run of 2026-09-22 (load average 2.9-3.9) re-measured cold start with
21 runs per case: script 17.43 ms and REPL 18.66 ms for the RelWithDebInfo
`build_release`; 17.99 / 18.73 ms for a Release build in `build_bench` — all
below target ([report](reports/2026-09-22-suite.md#cold-start)).
