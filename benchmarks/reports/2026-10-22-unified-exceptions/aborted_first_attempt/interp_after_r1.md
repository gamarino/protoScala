# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 09:49
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 5.75 / 6.45 / 6.39 at start, 6.21 / 6.40 / 6.37 at midpoint, 4.66 / 5.93 / 6.21 at end
- **Method:** 2 warmup + 5 timed runs per cell, interleaved across runtimes (round-robin: one sample of each runtime, then the next, so ambient load hits every column alike); median wall-clock of a cold process (start-up included), spread reported as `[min-max]` beside every median. This machine is a daily-driver desktop (VS Code, Chrome and PyCharm run throughout) and this runner gates on nothing: the load averages above are context, not a quietness claim; a claim that the host was quiet has to come from an `mpstat -P ALL` idle reading taken immediately before the run and recorded in the report that cites it. Ratios to CPython are the primary result, absolute milliseconds are indicative only. Every run's printed result is verified; a wrong result, non-zero exit or timeout marks the cell FAILED and it is excluded from every aggregate.

### Runtimes

| Column | Binary | Build / version | Twin programs |
|---|---|---|---|
| protoScala | `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/after/protoScala/build_release/protoscala` | Release | `benchmarks/comparable/*.scala` |
| Scala (JVM) | `/home/gamarino/Documentos/proyectos/tools/scala3-dl/scala3-3.9.0-x86_64-pc-linux` + `java -XX:-UsePerfData` | Scala compiler version 3.9.0 -- Copyright 2002-2026, LAMP/EPFL; openjdk version "25.0.4.1" 2026-08-18 | the same `.scala` files, compiled once with `scalac` (untimed) |
| CPython | `/usr/local/bin/python3.14` | Python 3.14.0 | protoPython's `benchmarks/*.py` (with `BENCH_N`), or `benchmarks/comparable/python/*.py` |

## Comparable workloads

Median wall-clock in milliseconds per cold process. `—`: the runtime has no twin of that workload. The last column is protoScala (canonical build) ÷ CPython (>1: protoScala slower). The geomean row gives, per column, the geometric mean of its ratio to CPython over the rows where both cells verified (row count in parentheses).

| Workload | protoScala (ms, median [min-max]) | Scala (JVM) (ms, median [min-max]) | CPython (ms, median [min-max]) | protoScala ÷ CPython |
|---|---:|---:|---:|---:|
| `int_sum_loop` | 36.3 [32.2-49.6] | 296.6 [244.5-348.9] | 52.4 [47.3-64.6] | 0.69× |
| `fib` | 69.3 [57.2-75.5] | 222.2 [205.0-239.0] | 61.1 [46.2-73.9] | 1.13× |
| `str_concat` | 32.6 [27.8-45.0] | 228.4 [212.7-238.0] | 38.9 [37.4-54.3] | 0.84× |
| `range_iterate` | 31.8 [30.6-43.0] | 244.8 [200.2-256.8] | 50.4 [42.3-62.0] | 0.63× |
| `tak` | 44.4 [40.0-55.9] | 235.8 [223.3-286.9] | 47.9 [42.7-54.7] | 0.93× |
| `fib30` | 397.7 [392.6-408.7] | 221.6 [200.1-244.4] | 183.6 [182.6-216.3] | 2.17× |
| `sum_loop` | 90.7 [83.1-98.2] | 212.7 [203.2-230.2] | 111.5 [91.7-117.5] | 0.81× |
| `factorial_100` | 31.0 [30.1-35.4] | 233.6 [202.6-266.3] | 54.5 [40.2-64.4] | 0.57× |
| `attr_lookup` | 52.5 [50.9-59.8] | 235.1 [220.0-250.9] | 59.3 [51.7-70.5] | 0.89× |
| `object_tree` | 341.1 [331.2-523.2] | 241.9 [230.4-282.1] | 215.8 [208.7-229.9] | 1.58× |
| `list_ops` | 551.6 [508.4-739.8] | 366.6 [316.7-433.9] | 2241.9 [2063.5-2539.1] | 0.25× |
| `map_build` | 323.8 [296.6-418.9] | 403.7 [388.6-482.6] | 99.6 [90.2-108.8] | 3.25× |
| `case_copy` | 799.7 [742.0-831.1] | 271.3 [244.8-290.5] | 1503.3 [1453.2-1757.4] | 0.53× |
| **Geomean vs CPython** | 0.89× (13) | 2.10× (13) | 1.00× | **0.89×** |

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
| `case_copy` | 500000 copy-updates of a four-field case class | protoScala |  |

### JVM compile time (untimed for the table)

`scalac -d build_jvm/<workload> <file>.scala`, once per workload:

| Workload | scalac (s) | Status |
|---|---:|---|
| `int_sum_loop` | 4.8 | OK |
| `fib` | 4.4 | OK |
| `str_concat` | 5.5 | OK |
| `range_iterate` | 5.5 | OK |
| `tak` | 4.6 | OK |
| `fib30` | 4.2 | OK |
| `sum_loop` | 4.7 | OK |
| `factorial_100` | 4.6 | OK |
| `attr_lookup` | 5.0 | OK |
| `object_tree` | 6.4 | OK |
| `list_ops` | 6.6 | OK |
| `map_build` | 5.3 | OK |
| `case_copy` | 4.8 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 35.80 | [31.72-47.55] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 37.26 | [33.08-50.38] | MISSED |

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

