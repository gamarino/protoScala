# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 10:03
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 4.03 / 5.41 / 5.84 at start, 4.51 / 5.11 / 5.69 at midpoint, 5.08 / 5.23 / 5.70 at end
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
| `int_sum_loop` | 35.1 [33.2-37.3] | 217.8 [193.8-227.3] | 44.5 [40.3-49.6] | 0.79× |
| `fib` | 60.0 [57.8-68.6] | 219.7 [201.2-262.5] | 54.8 [53.4-59.1] | 1.09× |
| `str_concat` | 31.6 [29.1-33.6] | 228.0 [205.6-266.0] | 42.0 [38.7-53.8] | 0.75× |
| `range_iterate` | 39.3 [35.9-41.5] | 251.4 [230.5-277.0] | 46.2 [43.0-49.5] | 0.85× |
| `tak` | 36.8 [34.8-38.8] | 214.5 [200.0-218.8] | 43.2 [36.9-51.1] | 0.85× |
| `fib30` | 384.0 [378.9-400.2] | 208.5 [193.5-239.0] | 179.7 [176.2-187.8] | 2.14× |
| `sum_loop` | 102.1 [82.9-132.0] | 235.2 [210.0-528.5] | 99.3 [94.0-236.1] | 1.03× |
| `factorial_100` | 36.2 [34.5-84.7] | 299.0 [240.1-503.0] | 51.3 [39.6-105.0] | 0.70× |
| `attr_lookup` | 54.5 [53.2-58.2] | 254.2 [237.3-276.7] | 52.7 [50.7-58.0] | 1.03× |
| `object_tree` | 364.0 [349.2-371.5] | 281.8 [256.3-284.6] | 217.0 [212.7-237.3] | 1.68× |
| `list_ops` | 517.5 [478.9-685.8] | 306.0 [277.2-423.8] | 2278.1 [1909.4-2651.1] | 0.23× |
| `map_build` | 421.2 [361.8-443.9] | 540.7 [500.4-605.1] | 144.8 [129.2-156.9] | 2.91× |
| `case_copy` | 838.4 [739.7-905.3] | 307.1 [254.7-409.5] | 1605.2 [1575.1-1670.6] | 0.52× |
| **Geomean vs CPython** | 0.94× (13) | 2.20× (13) | 1.00× | **0.94×** |

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
| `int_sum_loop` | 3.8 | OK |
| `fib` | 3.8 | OK |
| `str_concat` | 4.0 | OK |
| `range_iterate` | 4.2 | OK |
| `tak` | 4.5 | OK |
| `fib30` | 5.0 | OK |
| `sum_loop` | 4.4 | OK |
| `factorial_100` | 4.1 | OK |
| `attr_lookup` | 4.1 | OK |
| `object_tree` | 5.1 | OK |
| `list_ops` | 4.9 | OK |
| `map_build` | 4.6 | OK |
| `case_copy` | 4.7 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 38.96 | [32.97-61.90] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 36.50 | [33.40-42.99] | MISSED |

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

