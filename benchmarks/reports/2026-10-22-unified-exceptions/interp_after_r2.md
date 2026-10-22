# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 10:01
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 5.13 / 5.87 / 6.01 at start, 6.72 / 6.02 / 6.05 at midpoint, 4.38 / 5.51 / 5.87 at end
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
| `int_sum_loop` | 36.2 [34.2-38.0] | 214.9 [209.2-228.0] | 41.5 [39.6-49.9] | 0.87× |
| `fib` | 57.6 [56.5-62.8] | 211.2 [208.3-225.1] | 54.3 [45.4-59.8] | 1.06× |
| `str_concat` | 31.7 [29.5-37.9] | 228.8 [209.0-235.6] | 38.9 [36.1-50.9] | 0.82× |
| `range_iterate` | 36.3 [33.0-37.2] | 214.5 [196.8-229.4] | 43.3 [36.7-44.4] | 0.84× |
| `tak` | 37.9 [37.0-44.5] | 215.1 [205.8-229.5] | 40.8 [37.7-42.4] | 0.93× |
| `fib30` | 377.8 [369.5-422.8] | 218.1 [214.4-238.1] | 179.3 [175.8-190.8] | 2.11× |
| `sum_loop` | 86.0 [82.6-87.5] | 224.2 [212.5-231.3] | 102.0 [92.7-107.1] | 0.84× |
| `factorial_100` | 29.7 [27.3-31.9] | 215.3 [203.6-235.7] | 38.9 [35.5-42.3] | 0.76× |
| `attr_lookup` | 51.9 [46.6-53.6] | 212.9 [208.7-235.3] | 50.4 [45.4-55.3] | 1.03× |
| `object_tree` | 320.9 [313.1-347.0] | 231.4 [218.6-249.9] | 206.7 [201.9-225.7] | 1.55× |
| `list_ops` | 480.5 [461.1-487.6] | 285.4 [277.3-308.0] | 1924.7 [1850.2-1976.2] | 0.25× |
| `map_build` | 302.3 [294.0-323.9] | 389.3 [370.0-405.6] | 86.7 [77.0-93.3] | 3.49× |
| `case_copy` | 769.3 [743.4-1118.2] | 286.8 [237.5-328.8] | 1523.0 [1402.6-1571.6] | 0.51× |
| **Geomean vs CPython** | 0.96× (13) | 2.19× (13) | 1.00× | **0.96×** |

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
| `int_sum_loop` | 4.0 | OK |
| `fib` | 3.7 | OK |
| `str_concat` | 4.1 | OK |
| `range_iterate` | 4.3 | OK |
| `tak` | 3.8 | OK |
| `fib30` | 3.8 | OK |
| `sum_loop` | 4.2 | OK |
| `factorial_100` | 4.0 | OK |
| `attr_lookup` | 4.0 | OK |
| `object_tree` | 4.7 | OK |
| `list_ops` | 4.9 | OK |
| `map_build` | 4.6 | OK |
| `case_copy` | 4.8 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 35.89 | [33.53-39.21] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 35.24 | [31.96-45.65] | MISSED |

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

