# protoScala benchmarks — 2026-10-22

- **Date:** 2026-10-22 09:56
- **Machine:** AMD Ryzen 5 5500U with Radeon Graphics — 6 cores, 12 logical CPUs; Linux 7.0.0-38-generic (x86_64)
- **protoScala:** commit `1e1cf911`; `build_release/protoscala` is Release; no `build_bench` Release build
- **Load average (1/5/15 min):** 2.95 / 4.91 / 5.77 at start, 6.25 / 5.68 / 5.98 at midpoint, 6.26 / 5.74 / 5.98 at end
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
| `int_sum_loop` | 35.7 [33.3-42.0] | 220.3 [204.3-224.7] | 47.9 [43.4-48.8] | 0.75× |
| `fib` | 61.2 [56.1-66.4] | 227.3 [197.5-280.9] | 52.0 [50.5-57.6] | 1.18× |
| `str_concat` | 34.4 [31.6-36.1] | 302.2 [244.7-319.1] | 44.1 [38.8-47.3] | 0.78× |
| `range_iterate` | 41.1 [40.4-64.3] | 313.2 [288.1-488.8] | 58.0 [51.0-71.9] | 0.71× |
| `tak` | 45.3 [42.1-51.7] | 291.1 [280.5-322.9] | 50.0 [42.6-53.6] | 0.91× |
| `fib30` | 398.9 [388.6-401.3] | 222.2 [205.9-250.3] | 177.2 [173.7-185.7] | 2.25× |
| `sum_loop` | 85.9 [83.3-97.1] | 215.8 [203.5-225.3] | 98.8 [90.2-111.0] | 0.87× |
| `factorial_100` | 30.7 [29.2-31.1] | 220.2 [194.0-240.3] | 36.6 [36.0-39.3] | 0.84× |
| `attr_lookup` | 50.7 [50.1-52.4] | 219.4 [197.1-231.8] | 49.8 [49.2-52.4] | 1.02× |
| `object_tree` | 323.4 [312.2-327.2] | 242.6 [216.2-256.5] | 204.3 [202.6-221.8] | 1.58× |
| `list_ops` | 486.0 [474.2-502.9] | 292.6 [283.9-303.5] | 1971.2 [1885.7-2028.3] | 0.25× |
| `map_build` | 297.4 [295.3-316.8] | 393.7 [373.2-405.0] | 89.7 [83.9-99.0] | 3.32× |
| `case_copy` | 748.9 [728.4-776.8] | 241.9 [214.2-263.0] | 1449.5 [1412.4-1604.7] | 0.52× |
| **Geomean vs CPython** | 0.95× (13) | 2.26× (13) | 1.00× | **0.95×** |

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
| `fib` | 4.5 | OK |
| `str_concat` | 4.2 | OK |
| `range_iterate` | 4.0 | OK |
| `tak` | 3.9 | OK |
| `fib30` | 3.8 | OK |
| `sum_loop` | 4.2 | OK |
| `factorial_100` | 4.2 | OK |
| `attr_lookup` | 4.0 | OK |
| `object_tree` | 4.8 | OK |
| `list_ops` | 4.7 | OK |
| `map_build` | 4.7 | OK |
| `case_copy` | 4.7 | OK |

## Cold start

`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, DESIGN §1). Verdict: **MET** if every sample (including the worst) is below the target; **MISSED** if the median is at or above it; **STRADDLES** if the median is below the target but the spread crosses it (the worst sample is at or above 25 ms) — neither met nor missed, and reported as such rather than picking a side:

| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |
|---|---|---:|---:|---:|---:|---|
| protoScala (Release) | script | 21 | 21 | 34.39 | [32.34-45.56] | MISSED |
| protoScala (Release) | repl | 21 | 21 | 36.67 | [32.26-38.66] | MISSED |

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

