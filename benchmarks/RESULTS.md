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
| 2026-09-22 | [2026-09-22-phase2.md](reports/2026-09-22-phase2.md) | 1.06× (4 workloads: `attr_lookup`, `object_tree`, `fib`, `tak`) | Phase 2: `attr_lookup` and `object_tree` added; commit `e8f66a1` (the working tree added only the two benchmark files and their Python twin, no runtime change); load 3.88 / 3.10 / 3.25 at start and 3.78 / 3.31 / 3.31 at end — a shared machine, so the absolute milliseconds are noisier than the ratios; every cell verified |
| 2026-09-23 | [2026-09-23-suite.md](reports/2026-09-23-suite.md) | 0.83× (all 10 workloads) | **The 0.2.0 release run** (superseded by the row below — kept for history). Full suite, `attr_lookup` and `object_tree` included; commit `6e6837f` (the working tree held Phase 2's documentation changes only, no runtime change); load 3.11 / 2.98 / 3.38 at start and 4.21 / 3.73 / 3.63 at end — a shared machine, so absolute milliseconds are noisier than ratios; all 7 columns, every cell verified; **5 timed samples per cell, no spread reported** |
| 2026-09-23 | [2026-09-23-suite-v2.md](reports/2026-09-23-suite-v2.md) | 0.86× (all 10 workloads) | **Interleaved re-run superseding the row above.** Same commit tree (`3703759-dirty`); 7 timed samples per cell (2 warmup), round-robin across all 7 columns, **`[min-max]` spread reported for every cell**; load 3.37/6.73/6.83 at start, 4.08/6.19/6.63 at midpoint, 4.39/5.87/6.49 at end; every workload's protoScala figure moved less than 10% from the superseded run, i.e. that run was not meaningfully noise-distorted for the protoScala column |

The latest table is also in the top-level README ("Performance").

Actor runs (`benchmarks/actor-bench.sh`) are dated reports in the same folder:

| Date | Report | Notes |
|---|---|---|
| 2026-09-23 | [2026-09-23-actors.md](reports/2026-09-23-actors.md) | **The 0.3.0 release run** (superseded by the row below — kept for history). 7 modes × 6 worker counts, all 42 cells verified; CAS-list mailbox; load 3.59 → 9.09 on a shared machine; protoClojure measured the same day but **as a separate block, not interleaved**; **1 sample per cell, no spread reported** |
| 2026-09-23 | [2026-09-23-actors-v2.md](reports/2026-09-23-actors-v2.md) | **Interleaved re-run superseding the row above.** Same 7 modes × 6 workers, **5 samples per cell, round-robin across modes, worker counts and runtimes** (protoScala sample immediately followed by the matching protoClojure sample), median + `[min-max]` spread for every cell; load 2.96 at start, 9.51 at midpoint, 9.68 at end; the protoClojure gap (0.11×-0.83×, was 0.10×-0.89×) and the `fan-out` worker regression (protoScala -46% from w=1 to w=16 vs protoClojure +61%) both reproduce with narrow spreads |

## Actors (Phase 5)

`benchmarks/actor-bench.sh --name actors`, 2026-09-23, AMD Ryzen 5 5500U
(6 physical / 12 logical cores), `RelWithDebInfo`, protoScala `4fd06c4`,
protoCore `bf972d3f`, **actor mailboxes: CAS list** (protoCore 2.0.0 has no
`ProtoMPSCQueue`; the `Mailbox` seam switches to it when Phase P2 merges).
Load average 3.59 at start and 9.09 at end — **the machine was shared with
other builds**, so the absolute rates are a lower bound and the shape across
worker counts is noisier than a quiet-host run would be. Every one of the 42
cells was verified (message count, `Actor.stats`, `ok`) before any rate was
computed; none failed. Full report, with the protoClojure comparison and the
High-band latency percentiles:
[reports/2026-09-23-actors.md](reports/2026-09-23-actors.md).

Peak verified rate per mode (messages per second):

| Mode | 1 worker | peak | at | protoClojure peak | ratio at protoScala's peak |
|---|---:|---:|---|---:|---:|
| single | 126,950 | 126,950 | w=1 | 331,565 | 0.45× (w=1) |
| fan-out | 97,516 | 97,516 | w=1 | 526,039 | 0.32× (w=1) |
| MPSC | 166,983 | 166,983 | w=1 | 296,121 | 0.56× (w=1) |
| MPMC | 181,541 | 250,212 | w=4 | 334,898 | 0.89× (w=4) |
| ping-pong | 21,247 | 22,092 | w=2 | — | no protoClojure twin |
| await | 20,969 | 44,736 | w=4 | — | no protoClojure twin |
| priority | 115,890 | 115,890 | w=1 | — | no protoClojure twin |

What the run says, unadjusted:

- `await` completes at **every** worker count, including one — the proof that
  `await` inside an actor suspends cooperatively instead of blocking a worker.
- `MPMC` is the mode that scales, peaking at four workers and flattening above
  the physical core count (protoST's measured ceiling).
- `single` is flat by construction: the single-method invariant serialises one
  actor.
- **`fan-out` regresses as workers are added** (97k → 54k from 1 to 8 workers)
  where protoClojure improves. The fallback mailbox rebuilds a `ProtoList`
  under a compare-and-swap on every push and retries under contention, and the
  end-of-turn backlog check reads all three bands; with 1000 actors those two
  costs meet. This row is the first thing to re-measure once `ProtoMPSCQueue`
  lands. Recorded, not tuned away.
- High-band ask latency under a 200k-message Low-band flood: p50 30–42 µs
  across all worker counts, p99 ≈ 2.6 ms (the tail of a loaded host).

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

## Cold start (Phase 2)

The cold-start target was raised from < 20 ms to < 25 ms in Phase 2 (DECISIONS-LOG,
2026-09-22; DESIGN §1): the embedded Scala prelude adds ~1.2 ms to every start and the
standard library will keep growing.

Task 11 measured the prelude's cost on DEV12 (AMD Ryzen 5 5500U), `build_bench` with
`-DCMAKE_BUILD_TYPE=Release`, `benchmarks/cold-start.sh <binary> 21`. The baseline was
taken by stashing the prelude change, rebuilding the same tree with the same flags and
restoring it. Every one of the 18 measurements verified its output (`verified=21/21`);
the two `FAIL` marks below are timing only, against the then-current 20 ms target.

| Run | script median (ms) | REPL median (ms) |
|---|---|---|
| Baseline `a59824d` 1 | 18.28 | 18.98 |
| Baseline `a59824d` 2 | 18.22 | 18.76 |
| Baseline `a59824d` 3 | 18.19 | 19.13 |
| With prelude 1 | 19.60 | 19.85 |
| With prelude 2 | 18.93 | 19.93 |
| With prelude 3 | 19.50 | 20.37 (over the old 20 ms target) |
| With prelude 4 | 18.82 | 19.97 |
| With prelude 5 | 19.23 | 20.52 (over the old 20 ms target) |
| With prelude 6 | 19.21 | 19.94 |

`perf stat -r 3 build_bench/protoscala examples/hello.scala`, before (`a59824d`) and
after (`ee3b56a`): elapsed 18.58 ms ± 11.2 % → 19.70 ms ± 4.6 %; task-clock 13.78 →
15.12 ms; cycles 44.0 M → 50.1 M; instructions 29.8 M → 35.9 M; page faults 4 405 →
4 494. The prelude costs about 1.1–1.3 ms (≈ 6 M cycles) — the parse, compile and run
of its ~30 lines. The remaining ~18 ms is pre-existing start-up (protoCore space
set-up, ~4.4 k page faults, primitive installation).

The suite run of 2026-09-22 (`--name phase2`, commit `e8f66a1`, load 3.8–3.9 on a
shared machine) re-measured cold start with 21 runs per case, all verified, all below
the 25 ms target ([report](reports/2026-09-22-phase2.md#cold-start)):

| Build | Case | Runs | Verified | Median (ms) |
|---|---|---:|---:|---:|
| `build_release` (RelWithDebInfo) | script | 21 | 21 | 18.79 |
| `build_release` (RelWithDebInfo) | REPL | 21 | 21 | 20.46 |
| `build_bench` (Release) | script | 21 | 21 | 19.04 |
| `build_bench` (Release) | REPL | 21 | 21 | 20.31 |

## Phase 2 workloads

The `2026-09-22-phase2.md` run added the two workloads that exercise the object model,
which is what protoScala is built for (DESIGN §1: an agile, interoperable, very simple
Scala — not a fast one; the workloads that matter are actors, persistent structures and
deep object graphs, not integer loops):

- `attr_lookup` — 100000 iterations reading three fields of an object. Twin of
  protoPython's `attr_lookup.py` (`BENCH_N=100000`) and protoST's `attr_lookup.st`;
  this closes the last Phase 2 entry of the pending list.
- `object_tree` — build a complete binary tree of case classes (depth 16, 131071
  objects), path-copy its leftmost spine so the copy shares every right subtree with
  the original, and fold both versions with pattern matching. protoScala's own twin
  set: no sibling suite has it, and the CPython/protopy twin
  (`comparable/python/object_tree.py`) uses `__slots__` classes.

The measured medians are in the report and are not interpreted further here;
performance is not a Phase 2 goal and nothing was tuned for these numbers.

## Release run for 0.2.0 (2026-09-23)

`2026-09-22-phase2.md` measured only four workloads; the release table in the README
comes from the full ten-workload run of
[2026-09-23-suite.md](reports/2026-09-23-suite.md), which is the reference for 0.2.0.
Nothing was tuned between the two runs — the only code changes in between were
documentation and conformance fixtures.

Two readings the numbers support, both consistent with DESIGN §1:

- `attr_lookup` — 34.9 ms, 0.79× CPython and 3.5× faster than protoST's twin on the
  same machine. Field reads go through protoCore's per-thread attribute cache and cost
  about what a CPython attribute read costs, start-up included.
- `object_tree` — 391.1 ms, 1.92× CPython. Building 131071 immutable case-class
  instances and path-copying a spine is the workload protoScala is *for*, and it is the
  row where protoScala is furthest behind CPython's `__slots__` twin. protoCore's
  structural sharing makes the path copy cheap; the allocation of 131071 objects is
  what dominates. protopy runs the same twin in 3668.2 ms, so the cost is the shared
  object kernel's, not protoScala's frontend.
