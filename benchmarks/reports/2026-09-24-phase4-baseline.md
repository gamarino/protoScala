# protoScala benchmarks — 2026-09-24

- **Date:** 2026-09-24 00:12
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-31-generic (x86_64)
- **protoScala:** commit `941f577`; `build_release/protoscala` is RelWithDebInfo; `build_bench/protoscala` is Release
- **Load average (1/5/15 min):** 3.61 / 4.54 / 4.14 at start, 3.15 / 4.34 / 4.09 at midpoint, 3.02 / 4.01 / 3.99 at end (the run waited 60 s for the 1-minute load to drop below 4.0)
- **Method:** 2 warmup + 5 timed runs per cell, interleaved across runtimes (round-robin: one sample of each runtime, then the next, so ambient load hits every column alike); median wall-clock of a cold process (start-up included), spread reported as `[min-max]` beside every median. This machine is a daily-driver desktop (VS Code, Chrome and PyCharm run throughout); ratios to CPython are the primary result, absolute milliseconds are indicative only. Every run's printed result is verified; a wrong result, non-zero exit or timeout marks the cell FAILED and it is excluded from every aggregate.

### Runtimes

| Column | Binary | Build / version | Twin programs |
|---|---|---|---|
| protoScala | `/home/gamarino/Documentos/proyectos/protoScala/build_release/protoscala` | RelWithDebInfo | `benchmarks/comparable/*.scala` |
| protoScala Release | `/home/gamarino/Documentos/proyectos/protoScala/build_bench/protoscala` | Release | the same `.scala` files |
| CPython | `python3` | Python 3.14.0 | protoPython's `benchmarks/*.py` (with `BENCH_N`), or `benchmarks/comparable/python/*.py` |
| protopy | `/home/gamarino/Documentos/proyectos/protoPython/build_release/src/runtime/protopy` | Release, protoPython `a81ec451-dirty` | the same `.py` files |
| protost | `/home/gamarino/Documentos/proyectos/protoST/build_release/protost` | Release, protoST `3837ed3` | protoST's `benchmarks/comparable/*.st` |
| protoclj | `/home/gamarino/Documentos/proyectos/protoClojure/build_release/protoclj` | Release, protoClojure `a517280` | protoClojure's `benchmarks/*.clj` |

## Comparable workloads

Median wall-clock in milliseconds per cold process. `—`: the runtime has no twin of that workload. The last column is protoScala (canonical build) ÷ CPython (>1: protoScala slower). The geomean row gives, per column, the geometric mean of its ratio to CPython over the rows where both cells verified (row count in parentheses).

| Workload | protoScala (ms, median [min-max]) | protoScala Release (ms, median [min-max]) | CPython (ms, median [min-max]) | protopy (ms, median [min-max]) | protost (ms, median [min-max]) | protoclj (ms, median [min-max]) | protoScala ÷ CPython |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 24.4 [23.9-26.9] | 24.2 [23.3-24.9] | 37.6 [35.0-38.2] | 124.4 [116.9-134.7] | 33.4 [32.6-35.2] | — | 0.65× |
| `fib` | 49.2 [46.9-51.9] | 50.1 [49.7-51.7] | 45.1 [43.9-46.0] | 192.6 [189.5-198.9] | 412.3 [401.8-413.8] | — | 1.09× |
| `str_concat` | 21.2 [20.3-22.6] | 22.1 [20.5-23.0] | 34.6 [33.2-37.5] | 120.6 [118.9-128.2] | 18.3 [16.8-18.8] | — | 0.61× |
| `range_iterate` | 24.7 [23.1-26.4] | 25.0 [23.9-26.3] | 37.2 [36.4-40.5] | 118.8 [117.8-124.0] | 57.9 [56.3-60.2] | — | 0.66× |
| `tak` | 27.1 [26.6-28.7] | 27.9 [26.6-30.2] | 37.1 [35.5-43.7] | 40.4 [37.7-41.4] | — | 45.8 [44.9-47.6] | 0.73× |
| `fib30` | 350.5 [345.1-364.1] | 377.2 [364.3-382.4] | 174.6 [172.2-244.9] | 940.7 [920.1-984.6] | — | 603.7 [583.6-607.9] | 2.01× |
| `sum_loop` | 87.8 [75.2-115.0] | 81.5 [72.3-112.3] | 99.4 [94.6-115.5] | 85.1 [82.6-93.2] | — | 76.6 [72.4-80.9] | 0.88× |
| `factorial_100` | 21.2 [18.8-22.2] | 20.9 [20.5-22.0] | 32.9 [31.7-44.8] | 21.3 [20.9-22.4] | — | 15.9 [15.3-18.4] | 0.64× |
| `attr_lookup` | 39.7 [38.5-40.1] | 39.4 [38.5-45.5] | 47.4 [45.0-53.7] | 164.0 [160.8-168.9] | 123.1 [119.9-126.1] | — | 0.84× |
| `object_tree` | 429.9 [410.1-457.4] | 428.6 [396.9-461.8] | 204.3 [203.5-232.6] | 3805.1 [3738.1-4049.5] | — | — | 2.10× |
| `list_ops` | 463.4 [450.9-491.6] | 474.4 [465.4-542.5] | 1885.9 [1861.0-1972.6] | 632.9 [616.1-652.9] | — | — | 0.25× |
| `map_build` | 348.8 [330.0-358.6] | 351.9 [332.1-366.5] | 75.8 [74.7-81.2] | 939.3 [893.1-969.1] | — | — | 4.60× |
| **Geomean vs CPython** | 0.94× (12) | 0.95× (12) | 1.00× | 2.68× (12) | 1.77× (5) | 1.12× (4) | **0.94×** |

### Workloads

| Workload | Work | Origin of the twin set | Notes |
|---|---|---|---|
| `int_sum_loop` | sum of 0 until 100000 | protoPython / protoST | protoST's twin sums 1..N (result 5000050000); the others sum 0 until N. |
| `fib` | recursive fib(25) | protoPython / protoST |  |
| `str_concat` | 2000 string concatenations | protoPython / protoST |  |
| `range_iterate` | count 100000 iterations | protoPython / protoST | protoScala: a `while` loop. `for` landed in Phase 2, but `Range` is Phase 3, so the twin keeps the loop. |
| `tak` | tak(18, 12, 6) | protoClojure |  |
| `fib30` | recursive fib(30) | protoClojure | protoClojure's `fib.clj`; CPython/protopy run `call_recursion.py` with `BENCH_N=30`. |
| `sum_loop` | sum of 0..1000000 | protoClojure |  |
| `factorial_100` | 100! (BigInt, 158 digits) | protoClojure | Declared `BigInt` so the JVM does not overflow (Int at 13!, Long at 21!); protoScala promotes automatically (D1). |
| `attr_lookup` | 3 field reads x 100000 | protoPython / protoST |  |
| `object_tree` | build, path-copy and fold a 131071-object case-class tree | protoScala | the deep-object-graph workload of DESIGN §1; CPython runs the __slots__ twin in comparable/python/. |
| `list_ops` | map / filter / foldLeft over a 100000-element List | protoScala |  |
| `map_build` | build and read back a 50000-entry Map | protoScala |  |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (RelWithDebInfo) | script | 21 | 21 | 24.96 | [22.72-31.52] | STRADDLES |
| protoScala (RelWithDebInfo) | repl | 21 | 21 | 24.80 | [22.17-34.00] | STRADDLES |
| protoScala Release (Release) | script | 21 | 21 | 22.60 | [20.23-25.00] | STRADDLES |
| protoScala Release (Release) | repl | 21 | 21 | 23.80 | [21.98-26.59] | STRADDLES |

## Pending workloads

Workloads of the sibling suites that protoScala cannot express yet; each waits for the phase named below. They are not approximated.

| Workload | Suite | Arrives with | Why |
|---|---|---|---|
| `list_append` | protoPython / protoST | expressible since Phase 3; not in suite v1 | `:+` landed in Phase 3, so the twin is now writable. It is not part of the ROADMAP's suite v1 and `list_ops` already exercises the same surface. |
| `sum_squares` | protoClojure | expressible since Phase 3; not in suite v1 | `map` plus `sum`/`foldLeft` landed in Phase 3, so the twin is now writable, and `list_ops` already exercises both. |
| `exception_latency` | protoPython / protoST | Phase 4 (exceptions) | `throw` and `try`/`catch`. |

## Reproduce

```bash
SCALA_HOME=/path/to/scala3 benchmarks/bench.sh --name suite
```

