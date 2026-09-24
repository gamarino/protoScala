# Benchmarks

Every benchmark prints the result it computed and its runner verifies it — an
exit code alone never counts as success (DESIGN §10). Results are written to
dated reports in [`reports/`](reports/) with machine, date, commit, build type
and load average; [`RESULTS.md`](RESULTS.md) indexes them.

## Comparable workloads — `comparable/`

Scala 3 programs (indentation syntax) that run the same algorithm with the
same N as their twins in the sibling runtimes, so the harness can place
protoScala next to them. Each file's first line is an `// EXPECT: <result>`
directive; the program prints that result on its last line. The same source
compiles unchanged with `scalac` for the JVM column (hence `BigInt` and
`Long` where a JVM `Int` would overflow; on protoScala integers promote
automatically, D1).

| File | Work | Twins |
|---|---|---|
| `int_sum_loop.scala` | sum of 0 until 100000 | protoPython `int_sum_loop.py` (`BENCH_N=100000`), protoST `int_sum_loop.st` (sums 1..N) |
| `fib.scala` | recursive `fib(25)` | protoPython `call_recursion.py`, protoST `fib.st` |
| `str_concat.scala` | 2000 × `s = s + "x"` | protoPython `str_concat_loop.py`, protoST `str_concat.st` |
| `range_iterate.scala` | count 100000 iterations (a `while` loop until `for`/Range land) | protoPython `range_iterate.py` (`BENCH_N=100000`), protoST `range_iterate.st` |
| `tak.scala` | `tak(18, 12, 6)` | protoClojure `tak.clj`, `comparable/python/tak.py` |
| `fib30.scala` | recursive `fib(30)` | protoClojure `fib.clj`, protoPython `call_recursion.py` (`BENCH_N=30`) |
| `sum_loop.scala` | sum of 0..1000000 | protoClojure `sum-loop.clj`, `comparable/python/sum_loop.py` |
| `factorial_100.scala` | `100!` as a `BigInt` (158 digits) | protoClojure `factorial-100.clj`, `comparable/python/factorial_100.py` |
| `attr_lookup.scala` | 3 field reads × 100000 | protoPython `attr_lookup.py` (`BENCH_N=100000`), protoST `attr_lookup.st` |
| `object_tree.scala` | build, path-copy and fold a 131071-object case-class tree | `comparable/python/object_tree.py` |

`comparable/python/` holds the CPython/protopy twins of the protoClojure
workloads, which protoPython's suite does not have, and of `object_tree`.

`object_tree` is protoScala's own twin set: no sibling suite has it. It is
the deep-object-graph, persistent-structure workload DESIGN §1 names as the
kind of work protoScala is for (attribute lookup, structural sharing and
pattern matching over an immutable graph), as opposed to the integer loops
that dominate the rest of the table.

Every comparable file is also a CTest case (`benchmarks/<file>`): it runs once
through the conformance runner and must print its `EXPECT` value, so a broken
benchmark fails `ctest`.

`object_tree` keeps a 131071-object graph alive at once, so it is one of the
fixtures that cannot fit a small heap: its live set measures 252253
cells (about 1.9 cells per object), and because protoCore defers collection
until the ceiling is approached it needs a ceiling of roughly 600000 cells to
complete at all. Its CTest case therefore pins its own
`PROTOCORE_HEAP_LIMIT_CELLS=2000000` (see `tests/CMakeLists.txt`), so it is
independent of the ambient environment and the low-heap rooting sweep runs
unfiltered:

```bash
PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release --output-on-failure
```

The GC-pressure stress test for object graphs is `tests/cli/gc-pressure.sh`,
which runs its own case at a fixed ceiling.

`list_ops` and `map_build` (Phase 3) pin the same ceiling and for the same
reason: three 100000-element lists and 50000 string keys with their entries do
not fit the 20000-cell sweep, and an honest out-of-memory there would read as a
rooting defect.

`list_ops` and `map_build` complete the ROADMAP's **benchmark suite v1**
(`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`). Both print the work they
did -- the fold result, the collection sizes and, for `map_build`, the number of
keys read back -- so a collection surface that silently loses entries fails the
run instead of reading as a fast one. The `map_build` twin uses Python's mutable
`dict` against protoScala's immutable `Map`: the algorithm is the same, the
allocation behaviour deliberately is not, and `benchmarks/RESULTS.md` says what
that costs.

**Pending** (not approximated): `exception_latency` (Phase 4 exceptions), the
actor benchmarks (Phase 5). `list_append` and protoClojure's `sum-squares`
became expressible in Phase 3 (`:+`, `foldLeft`, `sum`) and are listed in the
runner's `PENDING` with that note; they are not in suite v1, and `list_ops`
already exercises the same surface.

## Running

```bash
cmake -B build_release -S . && cmake --build build_release   # canonical build
cmake -B build_bench -S . -DCMAKE_BUILD_TYPE=Release          # optional Release column
cmake --build build_bench -j4
SCALA_HOME=/path/to/scala3-3.x benchmarks/bench.sh            # all columns found
benchmarks/bench.sh --only fib,tak --runs 3 --name quick      # a subset
```

`bench.sh` wraps `run_benchmarks.py`. Per workload it does `--warmup` (2)
discarded and `--runs` (5) timed runs of every runtime, interleaved
round-robin so load changes hit all columns alike, and reports the median
wall-clock. Each sample is a fresh process: start-up is included for every
runtime, the JVM's start-up and JIT warm-up among them. The table's last
column and geomean row are ratios to CPython. Every run's output is checked;
a wrong result, non-zero exit or timeout marks the cell `FAILED` and it is
excluded from every aggregate (the harness then exits 1). It then runs
`cold-start.sh` on each protoScala build and writes
`reports/YYYY-MM-DD-<name>.md` (`--name`, default `suite`).

Before measuring, the harness waits while the 1-minute load average exceeds
`--max-load` (4.0), for at most `--max-wait` seconds (1200), and records the
load average at start and end in the report.

### Columns

| Column | What runs | Skipped when |
|---|---|---|
| protoScala | `build_release/protoscala` (canonical, RelWithDebInfo) | never (required) |
| protoScala Release | `build_bench/protoscala` (`-DCMAKE_BUILD_TYPE=Release`) | not built |
| Scala (JVM) | the same `.scala`, compiled once with `$SCALA_HOME/bin/scalac -d build_jvm/<workload>` (compile time reported separately, untimed), run as `java -XX:-UsePerfData -cp "$SCALA_HOME/lib/*:build_jvm/<workload>" <@main name>` | `SCALA_HOME` unset or without `bin/scalac` |
| CPython | `python3` on the `.py` twin | never (required) |
| protopy | protoPython on the same `.py` twin | no built binary found |
| protost | protoST on its `.st` twin | no built binary found, or no twin |
| protoclj | protoClojure on its `.clj` twin | no built binary found, or no twin |

### Environment variables

| Variable | Default |
|---|---|
| `PROTOSCALA_BIN` | `build_release/protoscala` |
| `PROTOSCALA_RELEASE_BIN` | `build_bench/protoscala` |
| `SCALA_HOME` | unset (JVM column skipped) |
| `JAVA_BIN` | `java` |
| `CPYTHON_BIN` | `python3` |
| `PROTOPY_BIN` | `../protoPython/build_release/src/runtime/protopy`, then other build dirs |
| `PROTOST_BIN` | `../protoST/build_release/protost`, then `../protoST/build/protost` |
| `PROTOCLJ_BIN` | `../protoClojure/build_release/protoclj` |

`scalac` runs with `HOME=build_jvm/.home` (its caches stay in the build tree)
and `JAVA_OPTS=-XX:-UsePerfData`, like the timed `java` runs, so the JVM
writes no `hsperfdata` files outside the tree.

## Cold start — `cold-start.sh`

`benchmarks/cold-start.sh [protoscala] [runs]` runs `examples/hello.scala` and
a REPL session that quits immediately, verifies every run's output and checks
the median against the < 25 ms target (DESIGN §1). The suite harness runs it
on each protoScala build and includes the result in its report.

## Actor benchmarks — `actor-bench.sh`, `actors/`

The seven modes of DESIGN §8.5 plus two CPU-bound `saturation-*` modes, each a
protoScala script in [`actors/`](actors/) that sizes itself from
`PROTOSCALA_BENCH_N`:

| Mode | Script | Shape |
|---|---|---|
| `single` | `actor-throughput.scala` | 1 sender thread × 1 actor: the per-actor pipeline floor |
| `fan-out` | `actor-fanout.scala` | 1 sender thread × 1000 actors: ready-queue stress |
| `MPSC` | `actor-mpsc.scala` | 4 OS producer threads → 1 actor |
| `MPMC` | `actor-mpmc.scala` | 4 OS producer threads × 4 actors, round-robin |
| `ping-pong` | `actor-pingpong.scala` | ask/reply through the cooperative suspension |
| `await` | `actor-await.scala` | 100 caller actors awaiting one shared actor; must complete at 1 worker |
| `priority` | `actor-priority.scala` | a Low-band flood plus 1000 timed High-band asks |
| `saturation-8` | `actor-saturation-8.scala` | 8 actors × N/8 **CPU-bound** messages (20,000-iteration summation each) |
| `saturation-32` | `actor-saturation-32.scala` | 32 actors × N/32 CPU-bound messages, identical total work |

### Why the `saturation-*` modes exist

The seven original modes are all capped by their own structural concurrency
rather than by the machine's cores: `single` has one producer and one actor,
`fan-out` one producer, `MPSC` four producers against one actor, and `MPMC`
four producers against four actors (and duly peaks at w=4). `fan-out` is
additionally **producer-bound** — 78-99% of its measurable window is inside the
sender's own loop — so adding workers cannot help it. **None of them can
exhibit a rise up to the physical core count**, which made the suite unable to
answer the one question a worker-count sweep is for (see
[`reports/2026-09-23-actors-v4-curve.md`](reports/2026-09-23-actors-v4-curve.md)).

The `saturation-*` modes remove the producer from the critical path instead of
trying to make it faster: every message carries a 20,000-iteration summation
(~1.5 ms of interpreter work), so the send loop costs **well under 1%** of the
run and the workers are the constraint. They mirror protoST's
`benchmarks/actors/saturation_8a.st` and `saturation_32a.st`, where the same
shape measured 1.00× / 1.70× / 2.83× / **3.11× at w=6** and then regressed
under SMT oversubscription. Reading `saturation-8` against `saturation-32`
separates "the scheduler cannot fill the cores" from "8 actors cannot fill the
cores"; both carry identical total work, so their curves are comparable.

Both have a protoClojure twin (`actor-saturation-8.clj`, `actor-saturation-32.clj`)
of the same shape and the **same message count**, so the comparison is
same-machine, same-shape. Their `processed` value is the **sum the actors
actually computed**, not a message count, so a handler that silently did no
work fails the check instead of reading as a very fast run.

Each script prints three lines — `mode=… messages=… processed=…`, then
`Actor.stats`, then `ok` or `FAILED` — and the runner **verifies all of them
before computing any rate**: the process must exit 0, the last line must be
`ok`, `processed` must equal the count the runner computed independently, and
`Actor.stats` must report at least as many messages. A cell that fails any
check is printed as `FAILED` and is never turned into a number. A silent
failure must never read as infinite throughput (the lesson of protoClojure's
2026-06-14 harness and protoPython's sprint 9).

```bash
benchmarks/actor-bench.sh --name actors                 # 9 modes x 6 worker counts
benchmarks/actor-bench.sh --only single,MPSC --size 100000
benchmarks/actor-bench.sh --no-compare                  # skip the protoClojure run
```

The report is written to `reports/<date>-<name>.md` with the machine, the core
count, the commit, the build type, **the mailbox backend the measured binary
was built with** (read from `protoscala --version`, never guessed), the load
average at start and end, the High-band latency percentiles, and the four rows
protoClojure's `benchmarks/actor-bench.sh` also measures, run on the same
machine on the same day. `ping-pong`, `await` and `priority` have no
protoClojure twin and say so.

`tests/cli/actor-bench-smoke.sh` runs all of them at `PROTOSCALA_BENCH_N=2000`
with the test suite, so a benchmark cannot rot unnoticed between full runs.

## Later

Phase 3 adds the collection workloads above.
