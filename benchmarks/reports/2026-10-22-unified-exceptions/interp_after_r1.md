# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 09:54
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 6.03 / 6.02 / 6.20 at start, 4.33 / 5.52 / 6.01 at midpoint, 3.12 / 4.98 / 5.80 at end
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
| `int_sum_loop` | 35.5 [33.2-39.1] | 213.9 [204.1-227.4] | 41.3 [39.0-45.6] | 0.86× |
| `fib` | 60.7 [56.2-63.7] | 219.5 [206.8-228.2] | 54.0 [49.4-60.7] | 1.12× |
| `str_concat` | 31.3 [30.4-35.0] | 215.4 [209.8-230.4] | 40.9 [37.6-42.5] | 0.76× |
| `range_iterate` | 34.8 [34.1-36.6] | 214.0 [199.7-225.2] | 40.8 [38.4-49.1] | 0.85× |
| `tak` | 41.3 [36.1-42.7] | 213.6 [208.0-226.1] | 40.4 [38.8-42.3] | 1.02× |
| `fib30` | 385.2 [379.6-388.8] | 215.1 [193.5-223.0] | 181.1 [176.7-192.0] | 2.13× |
| `sum_loop` | 83.5 [82.7-89.6] | 223.0 [207.9-235.9] | 100.2 [95.4-116.6] | 0.83× |
| `factorial_100` | 29.2 [28.0-31.5] | 228.3 [208.4-238.2] | 39.9 [35.2-42.5] | 0.73× |
| `attr_lookup` | 51.6 [45.4-69.1] | 215.7 [212.7-269.5] | 53.8 [44.6-58.4] | 0.96× |
| `object_tree` | 331.7 [322.2-339.9] | 237.4 [216.4-262.8] | 210.3 [195.7-217.2] | 1.58× |
| `list_ops` | 464.5 [455.7-482.6] | 296.2 [254.4-363.4] | 1935.6 [1911.5-1959.9] | 0.24× |
| `map_build` | 303.4 [297.8-313.9] | 391.6 [380.7-394.7] | 85.4 [79.9-85.9] | 3.55× |
| `case_copy` | 735.7 [725.9-754.0] | 237.4 [235.7-249.5] | 1453.5 [1411.3-1465.0] | 0.51× |
| **Geomean vs CPython** | 0.95× (13) | 2.17× (13) | 1.00× | **0.95×** |

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
| `int_sum_loop` | 4.1 | OK |
| `fib` | 3.8 | OK |
| `str_concat` | 3.9 | OK |
| `range_iterate` | 4.1 | OK |
| `tak` | 4.1 | OK |
| `fib30` | 3.9 | OK |
| `sum_loop` | 4.0 | OK |
| `factorial_100` | 4.0 | OK |
| `attr_lookup` | 4.2 | OK |
| `object_tree` | 5.0 | OK |
| `list_ops` | 4.7 | OK |
| `map_build` | 4.6 | OK |
| `case_copy` | 4.8 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 33.96 | [31.44-46.36] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 36.07 | [33.13-41.77] | MISSED |

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

