# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 09:58
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 5.75 / 5.64 / 5.95 at start, 8.64 / 6.46 / 6.20 at midpoint, 5.49 / 5.95 / 6.04 at end
- **Method:** 2 warmup + 5 timed runs per cell, interleaved across runtimes (round-robin: one sample of each runtime, then the next, so ambient load hits every column alike); median wall-clock of a cold process (start-up included), spread reported as `[min-max]` beside every median. This machine is a daily-driver desktop (VS Code, Chrome and PyCharm run throughout) and this runner gates on nothing: the load averages above are context, not a quietness claim; a claim that the host was quiet has to come from an `mpstat -P ALL` idle reading taken immediately before the run and recorded in the report that cites it. Ratios to CPython are the primary result, absolute milliseconds are indicative only. Every run's printed result is verified; a wrong result, non-zero exit or timeout marks the cell FAILED and it is excluded from every aggregate.

### Runtimes

| Column | Binary | Build / version | Twin programs |
|---|---|---|---|
| protoScala | `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/before/protoScala/build_release/protoscala` | Release | `benchmarks/comparable/*.scala` |
| Scala (JVM) | `/home/gamarino/Documentos/proyectos/tools/scala3-dl/scala3-3.9.0-x86_64-pc-linux` + `java -XX:-UsePerfData` | Scala compiler version 3.9.0 -- Copyright 2002-2026, LAMP/EPFL; openjdk version "25.0.4.1" 2026-08-18 | the same `.scala` files, compiled once with `scalac` (untimed) |
| CPython | `/usr/local/bin/python3.14` | Python 3.14.0 | protoPython's `benchmarks/*.py` (with `BENCH_N`), or `benchmarks/comparable/python/*.py` |

## Comparable workloads

Median wall-clock in milliseconds per cold process. `—`: the runtime has no twin of that workload. The last column is protoScala (canonical build) ÷ CPython (>1: protoScala slower). The geomean row gives, per column, the geometric mean of its ratio to CPython over the rows where both cells verified (row count in parentheses).

| Workload | protoScala (ms, median [min-max]) | Scala (JVM) (ms, median [min-max]) | CPython (ms, median [min-max]) | protoScala ÷ CPython |
|---|---:|---:|---:|---:|
| `int_sum_loop` | 36.1 [32.3-48.9] | 226.8 [210.7-260.7] | 46.2 [40.9-55.0] | 0.78× |
| `fib` | 60.7 [57.6-65.1] | 213.0 [207.4-233.4] | 54.0 [44.9-55.2] | 1.12× |
| `str_concat` | 31.9 [29.3-34.3] | 229.2 [207.8-238.1] | 38.1 [37.0-40.0] | 0.84× |
| `range_iterate` | 38.0 [34.0-39.9] | 260.4 [205.4-303.7] | 50.6 [38.5-57.2] | 0.75× |
| `tak` | 50.7 [42.9-52.6] | 323.1 [270.9-360.8] | 60.1 [50.8-68.8] | 0.84× |
| `fib30` | 514.8 [480.3-610.6] | 329.8 [268.8-386.1] | 246.9 [207.0-279.6] | 2.08× |
| `sum_loop` | 99.2 [93.5-118.5] | 293.8 [264.1-306.1] | 132.2 [101.2-158.3] | 0.75× |
| `factorial_100` | 33.1 [30.8-59.4] | 270.7 [230.1-308.4] | 41.2 [38.8-61.2] | 0.81× |
| `attr_lookup` | 58.5 [55.6-67.8] | 253.8 [245.6-258.1] | 61.3 [49.5-68.6] | 0.96× |
| `object_tree` | 375.6 [356.8-384.6] | 299.7 [258.3-365.7] | 240.1 [232.1-256.5] | 1.56× |
| `list_ops` | 508.3 [470.4-589.2] | 322.9 [293.6-488.6] | 2165.1 [1855.9-2332.0] | 0.23× |
| `map_build` | 312.6 [299.1-318.0] | 393.0 [386.1-428.7] | 86.8 [86.2-90.6] | 3.60× |
| `case_copy` | 746.2 [734.9-762.9] | 258.3 [247.4-280.0] | 1431.1 [1418.7-1437.8] | 0.52× |
| **Geomean vs CPython** | 0.93× (13) | 2.26× (13) | 1.00× | **0.93×** |

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
| `int_sum_loop` | 4.2 | OK |
| `fib` | 3.7 | OK |
| `str_concat` | 4.1 | OK |
| `range_iterate` | 4.3 | OK |
| `tak` | 3.8 | OK |
| `fib30` | 3.8 | OK |
| `sum_loop` | 4.3 | OK |
| `factorial_100` | 4.8 | OK |
| `attr_lookup` | 4.5 | OK |
| `object_tree` | 5.6 | OK |
| `list_ops` | 4.7 | OK |
| `map_build` | 5.3 | OK |
| `case_copy` | 4.7 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 35.42 | [31.41-46.32] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 34.96 | [32.61-38.73] | MISSED |

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

