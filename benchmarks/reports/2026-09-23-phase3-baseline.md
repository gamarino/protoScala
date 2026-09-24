# protoScala benchmarks — 2026-09-23

- **Date:** 2026-09-23 21:56
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-31-generic (x86_64)
- **protoScala:** commit `bca0352-dirty`; `build_release/protoscala` is RelWithDebInfo; `build_bench/protoscala` is Release
- **Load average (1/5/15 min):** 4.41 / 4.57 / 3.89 at start, 4.88 / 4.67 / 3.95 at midpoint, 6.16 / 5.00 / 4.14 at end (the run waited 60 s for the 1-minute load to drop below 4.0)
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
| `int_sum_loop` | 27.5 [26.7-28.7] | FAILED | 46.5 [44.7-51.0] | 146.4 [137.5-153.3] | 43.1 [41.2-54.7] | — | 0.59× |
| `fib` | 60.1 [58.3-61.2] | FAILED | 60.6 [57.9-73.1] | 241.4 [228.8-268.2] | 524.4 [493.3-555.4] | — | 0.99× |
| `str_concat` | 23.0 [22.1-24.8] | FAILED | 40.6 [38.8-49.8] | 147.6 [137.9-148.4] | 22.1 [18.7-24.7] | — | 0.57× |
| `range_iterate` | 29.0 [28.6-34.5] | FAILED | 46.6 [42.2-66.0] | 143.2 [139.4-146.9] | 71.4 [66.4-78.4] | — | 0.62× |
| `tak` | 31.3 [30.3-35.5] | FAILED | 48.7 [46.1-61.0] | 48.5 [43.2-52.0] | — | 55.8 [50.2-57.9] | 0.64× |
| `fib30` | 446.3 [421.5-469.8] | FAILED | 198.7 [192.3-206.2] | 1099.6 [1064.4-1144.6] | — | 709.2 [669.2-727.4] | 2.25× |
| `sum_loop` | 88.9 [86.7-94.6] | FAILED | 105.0 [102.5-125.2] | 93.4 [89.8-105.0] | — | 85.3 [82.8-114.6] | 0.85× |
| `factorial_100` | 23.9 [23.7-24.3] | FAILED | 47.0 [44.2-49.6] | 24.8 [22.7-27.3] | — | 18.6 [18.0-19.4] | 0.51× |
| `attr_lookup` | 49.3 [43.5-55.2] | FAILED | 56.8 [51.2-59.7] | 202.6 [189.5-208.8] | 152.3 [137.0-183.4] | — | 0.87× |
| `object_tree` | 472.8 [466.5-526.8] | FAILED | 234.3 [224.7-249.3] | 4394.2 [4288.4-4590.8] | — | — | 2.02× |
| **Geomean vs CPython** | 0.86× (10) | — | 1.00× | 2.74× (10) | 1.78× (5) | 1.07× (4) | **0.86×** |

### FAILED cells

- `int_sum_loop` / release: exit -11: 
- `fib` / release: exit -11: 
- `str_concat` / release: exit -11: 
- `range_iterate` / release: exit -11: 
- `tak` / release: exit -11: 
- `fib30` / release: exit -11: 
- `sum_loop` / release: exit -11: 
- `factorial_100` / release: exit -11: 
- `attr_lookup` / release: exit -11: 
- `object_tree` / release: exit -11: 

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

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (RelWithDebInfo) | script | 21 | 21 | 23.94 | [19.86-29.96] | STRADDLES |
| protoScala (RelWithDebInfo) | repl | 21 | 21 | 24.83 | [23.63-28.23] | STRADDLES |
| protoScala Release (Release) | script | 21 | 0 | 1214.42 | [1175.24-1258.64] | **FAIL** (wrong output) |
| protoScala Release (Release) | repl | 21 | 0 | 1232.64 | [1200.46-1413.97] | **FAIL** (wrong output) |

## Pending workloads

Workloads of the sibling suites that protoScala cannot express yet; each waits for the phase named below. They are not approximated.

| Workload | Suite | Arrives with | Why |
|---|---|---|---|
| `list_append` | protoPython / protoST | Phase 3 (collections) | Phase 2 builds lists by prepending (`::`, `List(...)`) but has no append (`:+`, `ListBuffer`); re-spreading varargs would copy the whole list per step, an O(N^2) algorithm that is not a twin. |
| `sum_squares` | protoClojure | Phase 3 (collections) | Phase 2 lists have `map`, but the reduction (`sum`/`foldLeft`) that this workload folds with arrives in Phase 3. |
| `exception_latency` | protoPython / protoST | Phase 4 (exceptions) | `throw` and `try`/`catch`. |

## Reproduce

```bash
SCALA_HOME=/path/to/scala3 benchmarks/bench.sh --name suite
```

