# protoScala

> **A dynamic Scala 3 dialect on the protoCore object kernel — instant start-up, persistent immutable data by default, GIL-free native actors, and transparent in-memory interop with Python, JavaScript, Smalltalk and Clojure.**

protoScala is a language runtime of the [protoCore](https://github.com/numaes/protoCore) family, alongside [protoST](https://github.com/gamarino/protoST) (Smalltalk), [protoClojure](https://github.com/gamarino/protoClojure) (Clojure), [protoJS](https://github.com/gamarino/protoJS) (JavaScript) and [protoPython](https://github.com/gamarino/protoPython) (Python).

It is **not** a JVM replacement: there is no JVM, no sbt/Maven, no Java interop and no static typechecker. It runs Scala 3 source — braces or significant indentation — with types parsed and erased, on a runtime that aims to:

- **start in under 25 ms with ~20 MB RSS**, making Scala viable for scripts and REPL-driven work;
- map **case classes and functional collections** onto protoCore's persistent, structurally shared data;
- run **native actors without a GIL**: messages are pointers to immutable data, mailboxes are lock-free with three priority bands, and `await` inside an actor suspends cooperatively instead of blocking a thread;
- **import modules from sibling runtimes** (`import py.numpy as np`) through protoCore's Unified Module Discovery, with no serialization at the boundary.

protoScala is also a **platform validation project**: protoCore aims to be a solid base for implementing any language, and each new language exposes missing capabilities. Those capabilities are added to protoCore as part of this project — the first two are `ProtoMap`, a persistent map with GC-traced object keys, and `ProtoMPSCQueue`, a lock-free GC-traced actor mailbox; both also serve protoClojure and protoST.

## A flavour of the language (target)

```scala
case class Increment(by: Int)
case object GetValue

val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}

counter ! Increment(10)
println((counter ? GetValue).await)          // 10

val squares = for x <- List(1, 2, 3) yield x * x
println(squares)                              // List(1, 4, 9)
println(List.range(1, 101).map(BigInt(_)).product.toString.length)  // 158 — no overflow
```

## Project status

**Phase 5 complete (version 0.3.0) — not production ready, open for community
review.** Phase 5 was implemented before Phases 3 and 4, so 0.2.0 is followed
by 0.3.0. The binary runs Scala 3 scripts and offers a REPL, in both brace and
significant-indentation syntax:

- `val`/`var`/`lazy val`/`def`, `if`/`while`, lambdas and closures, placeholder
  syntax (`_ + 1`), recursion (with `StackOverflowError` instead of a crash),
  `println`;
- **classes** with `val`/`var`/plain constructor parameters, auxiliary
  constructors, `extends`/`with`, abstract members, `override`, `final`,
  `sealed`, `private` (enforced as a lookup restriction);
- **objects and companions**, **case classes** and **case objects** with
  `apply`, `unapply`, `equals`, `hashCode` (bit-equal to the JVM's),
  `toString`, `copy`, `productArity`/`productElement`/`productPrefix`,
  `_1`..`_N`;
- **traits** with Scala's linearization installed as protoCore parent chains,
  trait parameters, and `super` — stackable traits included;
- **tuples** `Tuple2`..`Tuple22`, the universal `apply` rule for receivers that
  have an `apply` (a case class, or a companion that defines one), `update`,
  generated setters and method values;
- **pattern matching** with every pattern of the design (literals, wildcards,
  variables, typed, constructor, tuple, `::`, `List(a, rest*)`, alternatives,
  binders, stable identifiers, custom extractors, guards), pattern `val`s and
  `{ case ... }` literals, `isInstanceOf`/`asInstanceOf`;
- **for-comprehensions** (generators, guards, value definitions, patterns,
  `yield` and `do`) over `List`, `Option` and any class with
  `map`/`flatMap`/`withFilter`/`foreach`, with a lazy `withFilter`;
- `Option`/`Some`/`None` from a prelude written in protoScala, and a minimal
  `List`;
- **actors and futures, without a GIL**: `Actor.spawn(state)(handler)`, `!`,
  `?`, three priority bands, `Actor.stats`; `Future` with `await`, `map`,
  `flatMap`, `recover` and `Future(() => e)`; a worker pool of real OS threads
  with lock-free mailboxes and ready stacks; and an `await` inside a handler
  that **suspends cooperatively** — the worker is released, so an actor that
  asks another actor completes even with a single worker;
- `Try`/`Success`/`Failure`, `Thread.start`/`join` and
  `System.nanoTime`/`currentTimeMillis`/`getenv`.

```scala
val counter = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
counter ! 1                      // tell: queue a message, return at once
println((counter ? 41).await)    // ask:  a Future of the reply  ->  42
```

Exceptions, `enum`, extension methods, the Phase 3 collections, string
interpolation and UMD are not implemented yet — see
[docs/STATUS.md](docs/STATUS.md) for the exact boundary and
[docs/ROADMAP.md](docs/ROADMAP.md) for what each later phase brings.

## Performance

protoScala is positioned as an agile, interoperable, easily integrable and
very simple Scala — **not a fast one.** Beating the JVM on integer loops, or
matching protoClojure's raw actor throughput, are explicit non-goals (see
[docs/DESIGN.md](docs/DESIGN.md) §1). Where the numbers below show protoScala
losing, they are reported as measured, not adjusted or explained away.

Both tables in this section come from an **interleaved re-run** (2026-09-23)
that supersedes an earlier same-day run measured in blocks. This machine is
the maintainer's daily-driver desktop: VS Code, Chrome and PyCharm run
throughout, the load-average floor is roughly 2-3 on 12 logical CPUs, and
there is no quiet window to wait for. Every runtime is sampled round-robin
(one sample of column A, then B, then C, … then back to A) so ambient load
hits every column alike, and every cell below reports its median **and**
`[min-max]` spread — ratios between columns are the primary result, absolute
milliseconds/msg-per-second are indicative only.

### Actors (0.3.0)

Interleaved re-run: 2026-09-23, same machine, **CAS-list mailboxes**
(protoCore 2.0.0 has no `ProtoMPSCQueue` yet), load average 2.96 at start,
9.51 at midpoint, 9.68 at end. All 42 protoScala cells (7 modes × 6 worker
counts) and 24 protoClojure comparison cells verified their own message
counts before any rate was computed, 5 samples per cell, protoScala and
protoClojure sampled back to back at every (mode, workers) pair so the
comparison is same-machine, same-moment. Full report, with every cell's
spread:
[benchmarks/reports/2026-09-23-actors-v2.md](benchmarks/reports/2026-09-23-actors-v2.md)
(the superseded single-sample run is kept at
[benchmarks/reports/2026-09-23-actors.md](benchmarks/reports/2026-09-23-actors.md)).

| Mode | peak msg/s (median) | at | protoClojure at the same worker count |
|---|---:|---|---:|
| single | 133,942 | 2 workers | 302,051 [294,048-330,161] |
| fan-out | 104,384 | 1 worker | 314,711 [302,402-329,415] |
| MPSC | 161,528 | 2 workers | 285,136 [264,822-301,420] |
| MPMC | 254,412 | 4 workers | 307,878 [299,924-323,011] |
| ping-pong | 20,397 | 2 workers | no twin |
| await | 46,815 | 4 workers | no twin |
| priority | 118,575 | 1 worker | no twin |

Across the four comparable shapes, protoScala runs at **0.11×-0.83×**
protoClojure's median rate on *these scripts* — but see the correction
below: for `fan-out` that ratio compares two different workloads that share
a name. `await` completes at every worker count **including one**, which is
the point of the cooperative suspension. protoScala's mailboxes are still
the CAS-list fallback (`protoscala --version` reports it explicitly).

#### Correction (v4): the `fan-out` comparison was not like-for-like

An earlier version of this section stated that protoScala's actors are
roughly 2×-5× slower than protoClojure's and that `fan-out` regresses as
workers are added *where protoClojure improves*. The worker-count curve
measured afterwards shows **that comparison was invalid**, and the tables
above inherit the flaw. What the full curve establishes:

- **The two `fan-out` scripts measure different work.** protoScala's rotates
  its target on every send, so nearly every message is a cold wake;
  protoClojure's sends 1000 consecutive messages to one actor, so 999 of
  every 1000 coalesce into an already-claimed actor. **Run with protoScala's
  rotating shape, protoClojure collapses too, and harder**: 143,900 → 97,100
  → 78,400 → 64,100 msg/s at 1, 4, 6 and 16 workers — **−55%**, against
  **−32%** for protoScala's `ProtoMPSCQueue` variant over the same window.
  protoClojure's own batched script, run minutes apart on the same binary,
  held its 231k → 448k → 418k rise. Equalising the shape **reverses the
  ranking from 2 workers up**.
- **The scheduler does scale; `fan-out` specifically does not.** On the same
  scheduler, the same binaries and the same interleaved run, **`MPMC` rises
  +113%** from 1 to 4 workers (154,322 → 329,335 msg/s) and is still **+81%**
  on its 1-worker figure at 16 workers.
- **`fan-out` is producer-bound in its send path.** An isolated sender
  reaches 113,600 sends/s against the 87,238 msg/s the benchmark observes at
  1 worker, and the send loop itself slows from 8.80 s to 18.44 s as workers
  go 1 → 16 with the work held fixed. 78-99% of the measurable window is
  inside the sender's loop, not the pool's.
- **The `MPSC` "−11% with the queue" reported in v3 does not reproduce** — it
  was noise from two worker counts with overlapping bands. Across the full
  curve the queue is a wash at 1 worker and better at every higher count.
- **`single` stayed flat**, as its single-method invariant requires, which is
  the harness sanity check.

None of this makes protoScala fast, and it is not claimed to. protoScala is
an agile, interoperable, easily integrable and very simple Scala — **not a
fast one**; at 1 worker on the identical rotating shape it is still 14%
behind protoClojure. The honest correction is narrower and it is this: the
earlier cross-runtime `fan-out` comparison was not measuring the same work,
the scheduler scales on `MPMC`, and `fan-out`'s ceiling is in its producer.

**None of the modes above can exhibit a rise up to the 6 physical cores of
this machine**: `single` has one producer and one actor, `fan-out` one
producer, `MPSC` four producers against one actor, and `MPMC` four producers
against four actors — so each is capped by its own structural concurrency,
1 or 4, and not by the core count. No conclusion about protoScala's 6-core
ceiling can be drawn from any number in the table above.

#### The saturation modes (v5): the worker-count rise the suite was missing

Two CPU-bound modes were added to close that gap, mirroring protoST's
`saturation_8a.st` / `saturation_32a.st`: **`saturation-8`** and
**`saturation-32`** run 8 or 32 actors over the same total work, with a
20,000-iteration summation (~1.5 ms) inside every handler. That puts the cost
in the pool instead of the sender — the send loop is **0.14% of the run at 1
worker** — so the workers, not the producer, are the constraint. Each has a
protoClojure twin of the same shape and the **same message count**, and both
assert the **sum their actors actually computed**, so a handler that silently
did no work fails instead of reading as a fast run.

Shipped CAS-list binary, median of 4 interleaved samples, speedup against 1
worker (full tables:
[benchmarks/reports/2026-09-23-actors-v5-saturation.md](benchmarks/reports/2026-09-23-actors-v5-saturation.md)):

| speedup vs w=1 | w=2 | w=3 | w=4 | w=6 | w=8 | w=12 | w=16 |
|---|---|---|---|---|---|---|---|
| `saturation-8` | 1.98× | 2.35× | 3.18× | **3.16×** | 3.47× | 3.14× | 3.26× |
| `saturation-32` | 2.02× | 2.82× | 3.43× | **3.68×** | 3.90× | **3.97×** | 3.93× |
| protoClojure twin, 32 actors | 1.79× | 2.69× | 3.09× | 3.51× | 3.65× | 3.91× | 3.76× |

**protoScala's actors do scale with workers** — this is the first mode in the
suite that shows it, reaching **3.68× at 6 workers** and peaking at **3.97× at
12**. Three honest qualifications: the near-linear region ends at about 3
workers, not 6; **protoST's SMT regression above 6 workers did not reproduce**
here; and the machine was carrying 6-7 foreign threads on its 12 logical CPUs
throughout, which is enough to explain both. **The exact peak and the absence
of an SMT cliff are therefore not settled** and need a quiet machine.
protoClojure's twin traces the same curve, within a few percent, with the same
peak — evidence that the shape is the machine's and not protoScala's
scheduler.

**Nothing involving `ProtoMPSCQueue` is shipped**: it is not in protoCore
master, the released protoScala still uses the CAS-list mailbox, and every
queue figure quoted here describes a branch build that no released
protoScala uses. Measurement and method:
[benchmarks/reports/2026-09-23-actors-v4-curve.md](benchmarks/reports/2026-09-23-actors-v4-curve.md)
(superseding [v3](benchmarks/reports/2026-09-23-actors-v3-pmq.md)'s reading).
Full reading, with every mode and worker count:
[benchmarks/RESULTS.md](benchmarks/RESULTS.md). protoScala's actors carry
protoCore objects that the collector traces, which is a deliberate cost of
the design (DESIGN §8.4), not an accident.

### The general suite (0.3.0)

Interleaved re-run: 2026-09-23, AMD Ryzen 5 5500U (6 cores, 12 logical CPUs),
Linux 7.0, protoScala commit `3703759` (main, actors landed), protoCore
`bf972d3f` (2.0.0), load average 3.37 at start, 4.08 at midpoint, 4.39 at
end. Full report, with versions and build types of every runtime:
[benchmarks/reports/2026-09-23-suite-v2.md](benchmarks/reports/2026-09-23-suite-v2.md)
(the superseded run, whose protoScala figures agree within 10%, is kept at
[benchmarks/reports/2026-09-23-suite.md](benchmarks/reports/2026-09-23-suite.md)).
Median wall-clock in ms of a **cold process** (start-up included, for every
runtime) with its `[min-max]` spread, 2 warmup + 7 timed runs, every run's
printed result verified; `—` means the runtime has no twin of that workload.

| Workload | protoScala | protoScala Release | Scala 3.9 (JVM 21) | CPython 3.14 | protopy | protost | protoclj | protoScala ÷ CPython |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (0 until 100000) | 24.6 [22.2-25.2] | 23.5 [23.1-25.8] | 216.4 [201.4-263.2] | 38.0 [36.6-44.5] | 123.7 [120.5-129.5] | 36.7 [35.2-38.6] | — | 0.65× |
| `fib` (fib(25)) | 49.2 [47.5-56.0] | 49.2 [46.8-57.0] | 210.5 [204.5-225.0] | 46.0 [44.0-50.8] | 190.1 [179.8-192.6] | 415.3 [400.3-425.0] | — | 1.07× |
| `str_concat` (2000 concats) | 19.4 [17.8-20.0] | 20.6 [19.1-21.5] | 205.1 [197.8-214.8] | 34.2 [33.2-45.3] | 122.2 [119.2-140.1] | 17.4 [16.7-18.0] | — | 0.57× |
| `range_iterate` (100000) | 23.0 [21.5-25.7] | 23.9 [22.9-25.6] | 218.1 [204.3-240.3] | 36.5 [35.3-42.6] | 122.6 [119.5-142.6] | 63.3 [50.6-72.0] | — | 0.63× |
| `tak` (18, 12, 6) | 26.6 [25.6-30.8] | 26.2 [25.4-26.6] | 216.3 [192.4-270.0] | 36.9 [35.2-45.3] | 39.2 [37.5-41.0] | — | 46.4 [44.0-50.3] | 0.72× |
| `fib30` (fib(30)) | 372.3 [350.2-482.4] | 382.6 [350.9-438.7] | 206.7 [190.8-249.4] | 179.6 [169.8-228.2] | 887.3 [852.3-1051.0] | — | 582.1 [558.8-784.5] | 2.07× |
| `sum_loop` (0..1000000) | 67.1 [66.5-76.1] | 68.1 [66.4-79.9] | 205.8 [188.6-228.7] | 90.1 [87.7-97.0] | 87.6 [77.3-97.1] | — | 68.7 [65.9-71.2] | 0.75× |
| `factorial_100` (BigInt) | 17.9 [16.4-18.7] | 17.9 [17.3-19.7] | 206.1 [196.5-221.1] | 32.9 [31.5-35.5] | 20.4 [19.7-21.5] | — | 16.3 [14.5-17.0] | 0.54× |
| `attr_lookup` (3 field reads × 100000) | 35.8 [33.9-38.7] | 36.8 [34.0-37.9] | 210.0 [197.6-227.6] | 45.6 [42.0-47.1] | 161.6 [157.2-167.9] | 123.6 [117.6-129.4] | — | 0.79× |
| `object_tree` (131071-object tree: build, path-copy, fold) | 406.9 [383.7-629.9] | 404.7 [379.6-613.6] | 255.0 [216.0-384.1] | 209.1 [195.6-261.0] | 3657.1 [3619.2-4142.0] | — | — | 1.95× |
| **Geomean vs CPython** (rows) | 0.86× (10) | 0.87× (10) | 3.72× (10) | 1.00× | 2.82× (10) | 1.84× (5) | 1.11× (4) | **0.86×** |

Cold start (`benchmarks/cold-start.sh`, 21 runs, target < 25 ms): script
21.02 [19.44-22.68] ms, REPL 21.77 [20.37-23.28] ms (RelWithDebInfo); script
21.05 [19.50-22.26] ms, REPL 22.59 [20.42-24.33] ms (Release). **Verdict: MET**
for all four rows — every individual sample, including the worst, stayed
below the 25 ms target (the closest approach was the Release REPL's worst
sample at 24.33 ms, still under target). This is a real verdict, not a
close call disguised as one: unlike an earlier informal pass that saw the
median straddle 25 ms under heavier load, this interleaved run's full spread
never touched the target.

**Reading.** Short rows measure start-up more than work: protoScala starts in
about 17-18 ms (`factorial_100` is almost pure start-up) and CPython in about
32-38 ms, so on short workloads protoScala comes out ahead, and the 0.86×
geomean is as much a start-up figure as a throughput one. Where the work
dominates, the picture reverses, and protoScala loses. `fib30` (2.69M calls)
is 2.07× slower than CPython 3.14 — protoScala is call-dispatch bound, which
the `fib(25)` row hides behind its start-up advantage. `object_tree`, the
workload that exercises what protoScala is actually *for* (build 131071
immutable case-class instances, path-copy a spine so the copy shares every
right subtree, then fold both versions with pattern matching), is 1.95×
slower than CPython's `__slots__` twin: structural sharing makes the copy
cheap, and allocating the tree is what dominates. protopy runs the same twin
in 3.66 s, so that cost belongs to the shared object kernel and its GC, not
to protoScala's frontend. `attr_lookup`, the field-read twin, runs at 0.79×
CPython and about 3.5× faster than protoST on the same machine: attribute
reads go through protoCore's per-thread attribute cache and no parallel
inline caches were added. Loop arithmetic (`sum_loop`, one million
iterations) runs at 0.75× CPython and close to protoClojure. The Release
build is indistinguishable from the canonical RelWithDebInfo build within
spread. The JVM column runs the same `.scala` source compiled with `scalac`
(compile time excluded, reported separately); each sample is a fresh `java`
process, so ~200-260 ms of JVM start-up and class loading dominate every row,
and the JIT's steady state — where the JVM would be far ahead on `fib30` and
`object_tree`, as its ~200 ms floor already suggests — is not measured.
Cold-process timing favours short-lived runtimes.

Compared against the superseded, single-sample run of the same day, every
protoScala workload's median moved less than 10%, and the geomean moved from
0.83× to 0.86× — inside the spread shown above, i.e. the earlier run's
numbers were not meaningfully noise-distorted for the protoScala column, even
though it reported no spread of its own. The columns that moved most were
`protoScala Release`'s `str_concat` (+17%, 17.6 → 20.6 ms) and `range_iterate`
(+14%, 21.0 → 23.9 ms) — both still inside a few milliseconds and both
plausibly load noise given the higher midpoint/end load of this run (up to
6.83) versus the superseded run (up to 4.21).

**What these numbers are for.** This suite tracks start-up and guards against
regressions; it is not a claim that protoScala is fast. Phase 2 added the
first two workloads that matter for the positioning, `attr_lookup` and
`object_tree`; the rest — the full persistent collections and interop — join
the suite as the phases that enable them land. Phase 5 added the actor
benchmarks above.

**Pending workloads** (need later phases; not approximated): `list_append`
and protoClojure's `sum-squares` (Phase 3 collections), `exception_latency`
(Phase 4 exceptions). See [benchmarks/README.md](benchmarks/README.md) to run
the suite.

## Building

Prerequisites: a C++20 compiler, CMake ≥ 3.20, libreadline (from Phase 1), and
protoCore built as a sibling directory (or installed and passed via
`-DPROTO_CORE_PREFIX=<prefix>`).

```bash
# protoCore, once
cd ../protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

# protoScala
cd ../protoScala
cmake -B build_release -S .
cmake --build build_release
ctest --test-dir build_release
./build_release/protoscala --version
```

## Usage

```bash
# Run a script
protoscala examples/hello.scala
protoscala examples/fib.scala 10

# Start the REPL
protoscala
```

## Learn more

- [docs/TUTORIAL.md](docs/TUTORIAL.md) — dual-audience tutorial (Scala programmers; Python/JavaScript developers)
- [docs/LANGUAGE.md](docs/LANGUAGE.md) — supported language and departures from Scala
- [docs/STATUS.md](docs/STATUS.md) — living implementation tracker
- [docs/DESIGN.md](docs/DESIGN.md) — the approved design specification
- [docs/ROADMAP.md](docs/ROADMAP.md) — phases with verifiable done-when criteria
- [docs/INTEROP.md](docs/INTEROP.md) — UMD polyglot interop
- [docs/platform/](docs/platform/) — protoCore extensions specified by this project
- [docs/plans/](docs/plans/) — task-level implementation plans

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Gustavo Marino.
