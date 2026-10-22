# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 10:06
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 5.93 / 5.39 / 5.74 at start, 4.52 / 5.17 / 5.64 at midpoint, 5.30 / 5.23 / 5.63 at end
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
| `int_sum_loop` | 34.0 [32.5-42.5] | 222.9 [206.3-228.0] | 47.2 [42.6-60.6] | 0.72× |
| `fib` | 58.7 [55.5-63.1] | 195.6 [189.1-222.2] | 59.4 [51.9-68.6] | 0.99× |
| `str_concat` | 29.6 [27.2-31.0] | 212.1 [206.1-222.5] | 37.4 [34.9-62.6] | 0.79× |
| `range_iterate` | 35.4 [31.7-43.3] | 220.2 [194.0-240.4] | 47.5 [41.8-58.3] | 0.75× |
| `tak` | 37.7 [34.8-59.6] | 208.7 [195.8-234.3] | 42.1 [37.2-58.6] | 0.90× |
| `fib30` | 385.3 [377.9-409.8] | 222.8 [213.0-239.7] | 173.6 [168.9-193.3] | 2.22× |
| `sum_loop` | 85.0 [80.0-87.6] | 219.8 [188.9-249.6] | 96.7 [89.2-108.3] | 0.88× |
| `factorial_100` | 32.7 [26.2-41.3] | 212.1 [188.2-215.4] | 43.5 [38.2-54.9] | 0.75× |
| `attr_lookup` | 49.1 [45.4-58.8] | 226.4 [206.4-283.3] | 59.0 [48.0-72.9] | 0.83× |
| `object_tree` | 342.2 [321.1-362.9] | 250.2 [229.4-270.4] | 212.8 [191.4-220.1] | 1.61× |
| `list_ops` | 507.7 [459.9-519.4] | 289.7 [256.2-497.5] | 1876.2 [1849.3-2006.0] | 0.27× |
| `map_build` | 326.3 [302.8-353.7] | 437.2 [416.9-601.6] | 92.3 [84.3-117.1] | 3.54× |
| `case_copy` | 908.7 [779.3-1068.6] | 375.0 [290.9-470.6] | 1690.7 [1581.9-1895.9] | 0.54× |
| **Geomean vs CPython** | 0.93× (13) | 2.15× (13) | 1.00× | **0.93×** |

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
| `int_sum_loop` | 5.1 | OK |
| `fib` | 4.5 | OK |
| `str_concat` | 4.0 | OK |
| `range_iterate` | 4.1 | OK |
| `tak` | 3.9 | OK |
| `fib30` | 3.8 | OK |
| `sum_loop` | 4.5 | OK |
| `factorial_100` | 4.0 | OK |
| `attr_lookup` | 4.2 | OK |
| `object_tree` | 5.1 | OK |
| `list_ops` | 4.6 | OK |
| `map_build` | 4.8 | OK |
| `case_copy` | 4.4 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 43.99 | [34.59-87.60] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 40.94 | [33.54-69.33] | MISSED |

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

