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
| 2026-09-23 | [2026-09-23-actors-v2.md](reports/2026-09-23-actors-v2.md) | **Interleaved re-run superseding the row above.** Same 7 modes × 6 workers, **5 samples per cell, round-robin across modes, worker counts and runtimes** (protoScala sample immediately followed by the matching protoClojure sample), median + `[min-max]` spread for every cell; load 2.96 at start, 9.51 at midpoint, 9.68 at end; the protoClojure gap (0.11×-0.83×, was 0.10×-0.89×) and the `fan-out` worker regression (protoScala -46% from w=1 to w=16 vs protoClojure +61%) both reproduce with narrow spreads — **but see v4: the `fan-out` comparison is not like-for-like** |
| 2026-09-23 | [2026-09-23-actors-v3-pmq.md](reports/2026-09-23-actors-v3-pmq.md) | CAS list against `ProtoMPSCQueue` at **w=1 and w=16 only**. Its `MPSC` "−11% with the queue" was noise and does **not** reproduce on the full curve (see v4). `ProtoMPSCQueue` is **not merged**; the shipped binary uses the CAS-list mailbox |
| 2026-09-23 | [**2026-09-23-actors-v4-curve.md**](reports/2026-09-23-actors-v4-curve.md) | **The worker-count curve, 1-2-3-4-5-6-8-12-16, both mailbox variants, interleaved.** Overturns the cross-runtime `fan-out` reading of v2/v3: given protoScala's rotating shape **protoClojure collapses too, −55% w=1→w=16** against protoScala-with-queue's −32%, so the two scripts were measuring different work. `MPMC` **rises +113%** to w=4 on the same scheduler; `fan-out` is **producer-bound in its send path** (isolated sender 113,600/s vs 87,238 observed; send loop 8.80 s → 18.44 s as w goes 1→16). Records that **no mode in the suite can show a rise to the 6 physical cores** |
| 2026-09-23 | [**2026-09-23-actors-v5-saturation.md**](reports/2026-09-23-actors-v5-saturation.md) | **Adds the CPU-bound `saturation-8` / `saturation-32` modes**, closing the gap v4 recorded: 8 or 32 actors over identical total work with a 20,000-iteration summation (~1.5 ms) inside every handler, so the send loop is 0.14% of the run and the workers are the constraint. Mirrors protoST's `saturation_8a.st` / `saturation_32a.st`. protoClojure twins of the same shape and the same message count. **protoScala's actors do scale**: `saturation-32` reaches **3.68× at w=6** and peaks at **3.97× at w=12** on the shipped CAS-list binary; `saturation-8` peaks at 3.47× at w=8. The near-linear region ends at w=3, and **protoST's SMT regression did not reproduce** — but 6-7 of the 12 logical CPUs were carrying foreign load throughout, so the peak position and the missing SMT cliff are **not settled**. 288/288 samples verified, n=4 per cell, no kills |
| 2026-09-24 | [**2026-09-24-phase4-actors.md**](reports/2026-09-24-phase4-actors.md) | **The 0.5.0 release run, and the first on the `Priority` enum.** All nine modes (the seven of DESIGN §8.5 plus `saturation-8` / `saturation-32`) x 6 worker counts, 5 interleaved samples per cell, 450/450 samples verified, no FAILED cell and no kill. The shipped `ProtoMPSCQueue` mailbox throughout. Load average 7.99 / 8.48 / 8.43 — **higher than any earlier run**, so absolute msg/s are indicative and the ratios are the result. `saturation-8` peaks at **3.32x @ w=6** and `saturation-32` at **3.48x @ w=8**, against v5's ProtoMPSCQueue series' 3.16x @ w=4 and 3.43x @ w=8: Phase 4 did not cost actor scaling anything measurable. High-band ask p50 **27.2 µs @ w=1 -> 40.4 µs @ w=16**, against 26.7 -> 41.9 µs when `Priority` was three integers on an object — **the enum costs the band dispatch nothing**. protoScala leads protoClojure on both CPU-bound modes (1.17x-1.42x) and on `MPMC` at w=4-6, and trails it on the send-bound `single` / `fan-out` / `MPSC` modes |

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
- **`fan-out` regresses as workers are added** (97k → 54k from 1 to 8 workers).
  ~~where protoClojure improves~~ — **corrected by
  [v4](reports/2026-09-23-actors-v4-curve.md):** protoClojure improves only on
  *its own* `fan-out` script, which sends 1000 consecutive messages per actor;
  run with protoScala's rotating-target shape it collapses too, and harder
  (**−55%** from w=1 to w=16 against protoScala-with-queue's −32%). The two
  scripts measure different work, so this row never compared the two
  schedulers. v4 also puts 78-99% of the measurable window **inside the
  sender's loop** — `fan-out` is producer-bound — and shows `MPMC` rising
  **+113%** to w=4 on the same scheduler. The fallback mailbox rebuilds a
  `ProtoList` under a compare-and-swap on every push and retries under
  contention, and the end-of-turn backlog check reads all three bands; with
  1000 actors those two costs meet, and `ProtoMPSCQueue` removes a large
  constant factor (+44% to +134%) without changing the shape. Recorded, not
  tuned away. **`ProtoMPSCQueue` is not merged**; the shipped binary uses the
  CAS-list mailbox.
- **Known gap:** none of these modes can exhibit a rise up to the 6 physical
  cores — each is capped by 1 or 4 producers, or by the single-method
  invariant, not by the core count.
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

## Phase 3 workloads — benchmark suite v1 (2026-09-23)

The ROADMAP's benchmark suite v1 is `fib`, `tak`, `sum-loop`, `list-ops` and
`map-build`. The first three already existed; Phase 3 adds the last two, which
the collections work made expressible. Full table:
[2026-09-23-phase3-v1.md](reports/2026-09-23-phase3-v1.md).

- **Machine:** AMD Ryzen 5 5500U (6 cores, 12 logical CPUs), a daily-driver
  desktop.
- **Date:** 2026-09-23.
- **protoScala commit:** `ae2650b` (the tree also carried the uncommitted Task 12
  benchmark files, which are these two workloads).
- **protoCore commit:** `983bbf98`, version 2.1.0.
- **Load average:** 3.90 at the start, 3.59 at the midpoint, 3.46 at the end.
  Above the 2.0 "quiet host" bar, so the absolute milliseconds are indicative;
  the interleaved round-robin means ambient load hits every column alike, which
  is what makes the ratios usable.
- **No FAILED cell.** Every run's printed result was verified against the
  workload's `// EXPECT:` line before any rate was computed.

| Workload | protoScala | CPython | protoScala ÷ CPython |
|---|---:|---:|---:|
| `fib` (suite v1) | 51.4 ms | 46.4 ms | 1.11× |
| `tak` (suite v1) | 28.6 ms | 37.0 ms | 0.77× |
| `sum_loop` (suite v1) | 66.4 ms | 93.4 ms | 0.71× |
| `list_ops` (new) | 465.5 ms | 1934.7 ms | 0.24× |
| `map_build` (new) | 354.9 ms | 80.0 ms | 4.43× |
| **Geomean, all 12 workloads** | — | — | **0.91×** |

Two readings the numbers support:

- `list_ops` is 0.24× CPython, but the twins are not doing the same work at the
  same cost: the Python twin builds its list with `insert(0, i)`, which is O(n)
  per step, while `::` on a `ProtoList` is O(log n). The row says that
  protoScala's prepend-and-fold surface is cheap, not that protoScala is four
  times faster than CPython at list work.
- `map_build` is 4.43× CPython, and that is the honest cost of an immutable map:
  every one of the 50000 inserts returns a new `ProtoMap` version, where
  CPython's `dict` mutates one object in place. protopy runs the same twin in
  935.0 ms, so 354.9 ms is not a protoScala frontend cost. An immutable map that
  shares structure is what DESIGN §6.1 asks for; a faster mutable one is a
  different data structure, not a tuning target (P5).

**The verification was proved, not asserted.** With `999 ` prepended to
`list_ops.scala`'s `// EXPECT:` line, the run marked both protoScala cells FAILED
and excluded them from every aggregate, reporting:

```
- `list_ops` / protoscala: printed '9999900000 100000 50000', expected '999 9999900000 100000 50000'
- `list_ops` / release: printed '9999900000 100000 50000', expected '999 9999900000 100000 50000'
```

while `map_build`, untouched, still produced numbers. That is the ROADMAP's
"self-reports and is recorded" clause demonstrated end to end.

**Cold start**, same run, 21 runs each, all verified: 24.43 ms (script) and
24.24 ms (repl) for the RelWithDebInfo build, 22.71 ms and 23.72 ms for the
Release build — inside DESIGN §1's 25 ms budget at load 3.90, after the prelude
grew by `Either` and the extended `Option`/`Try`. An earlier measurement during
the phase read 27.05 ms at load 4.03; the budget is at the line and is
load-sensitive, so the figure is only meaningful with its load average beside
it.

## Phase 4 workloads (2026-09-24, 0.5.0)

- **Machine:** AMD Ryzen 5 5500U — 6 cores, 12 logical CPUs; Linux 7.0.0-31-generic.
- **Commit:** the Phase 4 series on `main`; protoCore `983bbf98` (2.1.0) — unchanged
  from 0.4.0, because Phase 4 needed nothing new from protoCore.
- **Reports:** `benchmarks/reports/2026-09-24-phase4-baseline.md` (the 0.4.0 binary,
  recorded before the first Phase 4 commit) and
  `benchmarks/reports/2026-09-24-phase4.md`.
- **Load average:** 3.61 at the baseline's start, 4.93–5.53 during the after-run.
  **Both are well above 2.0, so the absolute milliseconds are a reference only and
  no claim is made from them.** The ratio to CPython is the primary result, because
  CPython moved with the host: it slowed by 8–23 % between the two runs.

| | baseline (0.4.0) | after (0.5.0) |
|---|---:|---:|
| Geomean vs CPython, canonical build | 0.94× (12) | 0.95× (12) |
| Geomean vs CPython, Release build | 0.95× (12) | 0.96× (12) |

One hundredth of a geomean, on a host whose own reference moved by up to 23 %: no
workload regression is claimed and none is visible. The individual rows move in
both directions (`sum_loop` 0.88× → 0.75×, `fib30` 2.01× → 2.26×), which is what a
contended host does to a 5-run median.

Because the wall-clock comparison could not answer the question P5 asks, the two
mechanisms this phase adds to hot paths were measured **directly**, by A/B on the
same binary with `perf stat -r 3`. Instruction counts are load-independent and are
reported beside the cycles.

**The per-frame retry loop** (`execute` → `runFrame` → `runLoop`, one C++ `try`
region per frame). Compared against a build where `execute` calls `runLoop`
directly:

| Workload | with the retry loop | without | verdict |
|---|---:|---:|---|
| `fib30` | 1.2505 Gcycles / 3.112 Ginstr | 1.3016 / 3.045 | **3.9 % fewer cycles with** |
| `attr_lookup` | 122.1 Mcycles / 211.9 Minstr | 128.8 / 212.2 | **5.2 % fewer cycles with** |
| `tak` | 91.4 Mcycles / 129.3 Minstr | 88.7 / 125.1 | 3.1 % more cycles with |

All three inside the noise of a busy host, two of them faster *with* the loop, and
none a regression: one C++ `try` region per frame is free on the non-throwing path
of a zero-cost-exceptions ABI, which is what the phase assumed and now measures.
No `[[gnu::cold]]` split of the catch block was needed.

**Mutable class prototypes** (`MAKE_CLASS` builds a mutable shape, so an extension
method installed after the class exists is visible to instances already created).
Compared against a build with `isMutable=false`:

| Workload | mutable (shipped) | immutable | delta |
|---|---:|---:|---|
| `object_tree` (131071 case-class objects) | 1.3933 Gcycles / 2.367 Ginstr | 1.3550 / 2.335 | **+2.8 % cycles, +1.4 % instructions** |
| `attr_lookup` | 140.4 Mcycles / 211.4 Minstr | 138.1 / 215.0 | +1.6 % cycles (±6 % error bars), fewer instructions |

`object_tree`'s +2.8 % is inside the phase's 3 % gate and is recorded rather than
hidden: it is the one read-path cost this phase adds, and the reason for it is a
correctness requirement, not an optimisation. Class *creation* is cheaper in
exchange, because the members now mutate one object instead of building a fresh
immutable copy per member.

### Actors after Phase 4

`benchmarks/actor-bench.sh --name phase4-actors`, report
[reports/2026-09-24-phase4-actors.md](reports/2026-09-24-phase4-actors.md). Nine
modes x six worker counts, 5 interleaved samples per cell, every sample's
self-report verified before any rate was computed: 450/450 verified, no FAILED
cell, nothing killed. Load average 7.99 at the start and 8.43 at the end — the
busiest host any actor run has had — so the absolute rates are a reference only.

Two questions this phase had to answer, and the answers:

| question | before | after Phase 4 | reading |
|---|---:|---:|---|
| Does the `Priority` **enum** cost the band dispatch anything? (High-band ask p50, w=1 -> w=16) | 26.7 -> 41.9 µs | 27.2 -> 40.4 µs | **No.** An enum case is a singleton object and the band is read from its `ordinal`; the numbers are indistinguishable |
| Does the per-frame **retry loop** cost actor scaling anything? (`saturation-8` / `saturation-32` peak speedup) | 3.16x @ w=4 / 3.43x @ w=8 | 3.32x @ w=6 / 3.48x @ w=8 | **No.** Both peaks are at or above the previous ProtoMPSCQueue series, on a busier host |

The "before" column is the `ProtoMPSCQueue` series of
[v5](reports/2026-09-23-actors-v5-saturation.md) for saturation and
[v2](reports/2026-09-23-actors-v2.md) for the latency percentiles — the latter on
the CAS-list mailbox, which is why only the *shape* of the latency curve is
compared and not its absolute throughput.

`saturation-8`'s peak moving from w=4 to w=6 is **not** claimed as an improvement:
w=4 and w=6 are 2,439 and 2,507 msg/s with `[2,350-2,747]` and `[2,414-2,561]`
spreads that overlap. The honest statement is that the near-linear region and the
plateau are where v5 found them, and that this phase did not move them.

**Cold start** missed the < 25 ms budget of DESIGN §1, by about 1 ms, and the
cause was **measured rather than asserted**. The 0.4.0 and 0.5.0 binaries were
re-measured **interleaved in the same window** (0.4.0 built from `941f577` in a
scratch worktree against the same protoCore), three rounds of 21 verified runs
each, plus a third binary: 0.5.0 with **twenty more exception classes of the same
shape added to the prelude**, a probe that is not shipped.

| binary | script median | REPL median |
|---|---:|---:|
| 0.4.0 (`941f577`) | 23.91 ms | 24.46 ms |
| 0.5.0 (shipped) | 25.22 ms | 25.58 ms |
| 0.5.0 + 20 probe prelude classes | 26.41 ms | — |

Per-round script medians, in order: 0.4.0 24.21 / 23.91 / 23.83, 0.5.0
25.53 / 24.98 / 25.22, probe 26.60 / 26.05 / 26.41 — **monotone in all three
rounds**, which a 1 ms effect on a shared host does not usually manage. The REPL
column comes from an earlier two-binary interleave in the same session, so read
down a column and not across.

Phase 4 cost **+1.31 ms**; twenty more classes cost a further **+1.19 ms**. The
prelude grew from 156 lines / 7,575 bytes to 200 lines / 10,176 bytes — twenty
exception classes, `StringContext` and the `Priority` enum — and at roughly
**60 µs per prelude class** that growth accounts for essentially the whole
regression. This matters because it **rules out the alternative explanation**: it
is not the exception machinery in the engine (the per-frame retry loop was
measured free above, and mutable prototypes make class *creation* cheaper), it is
that the prelude is parsed, desugared, compiled and run at every start-up with
nothing cached between runs. **The budget is not claimed as met**; meeting it
again needs a precompiled prelude or a smaller one, which is a Phase 6 decision.

The earlier, uninterleaved figures this section first recorded (26.31 / 26.98 ms
for 0.5.0 against 24.96 / 24.80 for 0.4.0, measured hours apart at load 3.6 and
4.9-5.5) are superseded by the table above: they had the direction right and the
magnitude roughly twice too large.

### Where the prelude's time goes (Phase 6 Task 1, 2026-09-24)

The 0.5.0 attribution above is at the level of "the prelude costs ~60 µs per
class", which is enough to rule out the engine and not enough to design a fix. A
precompiled image can remove parse, desugar and compile; it can remove nothing of
`linkSymbols` or of *running* the compiled prelude. So the split was measured
before anything was built.

`src/runtime/Prelude.h`'s `PreludeTiming` takes five `steady_clock` reads per
process start and `PROTOSCALA_PRELUDE_TIMING=1` prints them. 21 runs of
`build_release/protoscala examples/hello.scala`, medians, on this host
(AMD Ryzen 5 5500U, load average 5.15 — a shared machine with sibling work
running; the stage split is a within-process measurement and is insensitive to
that, and it reproduced to within 0.5 % across two separate 21-run batches taken
at load 11.2 and load 5.2).

| stage | median | share of the prelude |
|---|---:|---:|
| parse | 2,640 µs | 59.6 % |
| desugar | 115 µs | 2.6 % |
| compile | 997 µs | 22.5 % |
| link (`linkSymbols`) | 342 µs | 7.7 % |
| run (`MAKE_CLASS`, `MAKE_FN`, `STORE_GLOBAL`, …) | 177 µs | 4.0 % |
| **total** | **4,426 µs** | |

`lib/prelude.scala` is 200 lines / 10,176 bytes with 30 top-level `class` /
`trait` / `object` / `enum` declarations and 103 `def`s — the same file the 0.5.0
row above was measured against, so the 60 µs/class rate is being *checked*, not
re-derived. 4,426 µs over 30 declarations is ~148 µs per declaration for the
whole pipeline; the 60 µs/class figure was a *marginal* cost measured by adding
twenty classes, and a marginal cost below the average is what a parser with
fixed set-up work produces. The two measurements agree.

**The decision rule** (Task 1 Step 3, stated before the number was known): let
`R = run / total`. `R = 177 / 4,426 = 0.040`, far below the 0.55 threshold, so
**Task 2 proceeds and E5 does not fire on this rule**. An image that removes
parse + desugar + compile removes 3,752 µs — **84.8 %** of the prelude's cost —
and leaves link (342 µs) and run (177 µs), which no protoScala-side change can
remove.

### Cold start at 0.6.0 — the budget is met (2026-09-24)

`benchmarks/cold-start.sh <binary> 21`, **three rounds interleaved in one
window**, image and source path alternating, on this host (AMD Ryzen 5 5500U).
Load average **2.97 at the start and 2.67 at the end**; 22.7 GiB available. Both
paths live in the same binary, so the third row is what proves the **image** moved
the number rather than anything else in the phase — `PROTOSCALA_PRELUDE_NO_IMAGE=1`
takes the source path and changes nothing else.

**Every one of the twelve cases reported `verified=21`.** A median without that is
not a figure.

| binary | script, per round | script median | REPL, per round | REPL median |
|---|---|---:|---|---:|
| 0.5.0 (shipped, quoted from the table above, not re-measured) | — | 25.22 ms | — | 25.58 ms |
| **0.6.0, precompiled image** | 23.58 / 23.73 / 23.85 | **23.73 ms** | 23.89 / 23.83 / 24.84 | **23.89 ms** |
| 0.6.0, `PROTOSCALA_PRELUDE_NO_IMAGE=1` | 25.71 / 25.31 / 25.65 | 25.65 ms | 26.04 / 27.07 / 26.01 | 26.01 ms |

`benchmarks/cold-start.sh` **exited 0 in all three image rounds** and **1 in all
three source rounds**, on both the script and the REPL case. That exit code *is*
the done-when: it fails when any run printed the wrong last line or when a median
is not below `TARGET_MS=25`.

**Verdict: DESIGN §1's `< 25 ms` is met at 0.6.0, and it is met by the image.**
The image is worth 1.92 ms on the script case and 2.12 ms on the REPL case,
against the 2.46 ms the in-process stage measurement predicted
(`4,116 µs → 1,660 µs` of prelude time) — agreement to within the run-to-run
spread, which is what the `min_ms`/`max_ms` columns show. The source path still
misses by 0.65 ms, which is 0.5.0's regression very nearly unchanged: nothing
about the prelude got smaller, and `ImportError` made it one class larger.

**What was not removed, and cannot be from here.** `linkSymbols` (342 µs) and
*running* the compiled prelude (177 µs) survive the image by construction: the
tables hold strings and PODs, so the symbols must still be interned into a
`ProtoSpace`, and `MAKE_CLASS`/`MAKE_FN`/`STORE_GLOBAL` must still execute.
Removing those needs a **protoCore space image**, which does not exist — there is
no object-graph serialisation, no image and no snapshot API in
`headers/protoCore.h` or `proto_internal.h`, and the only caching facility is the
in-process `SharedModuleCache`, which holds live objects and cannot cross a
process. That is P3 territory and a maintainer decision (escalation **E5**); it is
no longer urgent, because the budget is met with 1.27 ms of headroom on the script
case, but the headroom is what the next twenty prelude classes would spend.

**Reading the jitter honestly.** Round 2's source-path case shows `max_ms` of
34.54 (script) and 43.38 (REPL) against an image-path worst case of 26.50 across
all three rounds. A shared host produces outliers; the medians are what the target
is compared against, and the three rounds are monotone in the direction that
matters — every image round is below 25 ms and every source round is above it, on
both cases, which a 2 ms effect on a noisy host does not usually manage.
