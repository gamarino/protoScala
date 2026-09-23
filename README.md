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

**Phase 2 complete (version 0.2.0) — not production ready, open for community
review.** The binary runs Scala 3 scripts and offers a REPL, in both brace and
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
- **tuples** `Tuple2`..`Tuple22`, the universal `apply` rule, `update`,
  generated setters and method values;
- **pattern matching** with every pattern of the design (literals, wildcards,
  variables, typed, constructor, tuple, `::`, `List(a, rest*)`, alternatives,
  binders, stable identifiers, custom extractors, guards), pattern `val`s and
  `{ case ... }` literals, `isInstanceOf`/`asInstanceOf`;
- **for-comprehensions** (generators, guards, value definitions, patterns,
  `yield` and `do`) over `List`, `Option` and any class with
  `map`/`flatMap`/`withFilter`/`foreach`, with a lazy `withFilter`;
- `Option`/`Some`/`None` from a prelude written in protoScala, and a minimal
  `List`.

Exceptions, `enum`, extension methods, the Phase 3 collections, string
interpolation, actors and UMD are not implemented yet — see
[docs/STATUS.md](docs/STATUS.md) for the exact boundary and
[docs/ROADMAP.md](docs/ROADMAP.md) for what each later phase brings.

## Performance

Latest measured run: 2026-09-23, AMD Ryzen 5 5500U (6 cores, 12 logical CPUs),
Linux 7.0, protoScala commit `6e6837f` (0.2.0; the working tree held Phase 2's
documentation changes only), load average 3.11 at start and 4.21 at end — a
shared machine, so absolute milliseconds are noisier than the ratios. Full
report, with versions and build types of every runtime:
[benchmarks/reports/2026-09-23-suite.md](benchmarks/reports/2026-09-23-suite.md).
Median wall-clock in ms of a **cold process** (start-up included, for every
runtime), 2 warmup + 5 timed runs, every run's printed result verified;
`—` means the runtime has no twin of that workload.

| Workload | protoScala | protoScala Release | Scala 3.9 (JVM 21) | CPython 3.14 | protopy | protost | protoclj | protoScala ÷ CPython |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (0 until 100000) | 25.0 | 24.3 | 218.5 | 44.7 | 141.6 | 37.0 | — | 0.56× |
| `fib` (fib(25)) | 48.9 | 50.2 | 206.2 | 47.0 | 198.9 | 423.0 | — | 1.04× |
| `str_concat` (2000 concats) | 18.4 | 17.6 | 198.5 | 36.2 | 125.6 | 16.6 | — | 0.51× |
| `range_iterate` (100000) | 22.4 | 21.0 | 207.6 | 37.4 | 127.1 | 63.7 | — | 0.60× |
| `tak` (18, 12, 6) | 25.4 | 25.6 | 212.4 | 35.1 | 38.3 | — | 47.0 | 0.72× |
| `fib30` (fib(30)) | 381.0 | 370.1 | 204.6 | 181.4 | 923.9 | — | 569.7 | 2.10× |
| `sum_loop` (0..1000000) | 72.9 | 67.2 | 208.0 | 93.0 | 88.8 | — | 67.8 | 0.78× |
| `factorial_100` (BigInt) | 16.3 | 16.0 | 208.2 | 35.3 | 19.8 | — | 15.6 | 0.46× |
| `attr_lookup` (3 field reads × 100000) | 34.9 | 34.5 | 215.3 | 44.4 | 165.2 | 120.7 | — | 0.79× |
| `object_tree` (131071-object tree: build, path-copy, fold) | 391.1 | 380.9 | 232.7 | 203.5 | 3668.2 | — | — | 1.92× |
| **Geomean vs CPython** (rows) | 0.83× (10) | 0.80× (10) | 3.56× (10) | 1.00× | 2.82× (10) | 1.74× (5) | 1.08× (4) | **0.83×** |

Cold start (`benchmarks/cold-start.sh`, 21 runs, target < 25 ms): script
18.86 ms, REPL 19.71 ms (RelWithDebInfo); 19.29 / 20.81 ms (Release). The
embedded Scala prelude that Phase 2 added costs about 1.2 ms of that.

**Reading.** Short rows measure start-up more than work: protoScala starts in
about 16 ms (`factorial_100` is almost pure start-up) and CPython in about
30-35 ms, so on short workloads protoScala comes out ahead, and the 0.83×
geomean is as much a start-up figure as a throughput one. Where the work
dominates, the picture reverses. `fib30` (2.69M calls) is 2.1× slower than
CPython 3.14 — protoScala is call-dispatch bound, which the `fib(25)` row hides
behind its start-up advantage. `object_tree`, the workload that exercises what
protoScala is actually *for* (build 131071 immutable case-class instances,
path-copy a spine so the copy shares every right subtree, then fold both
versions with pattern matching), is 1.92× slower than CPython's `__slots__`
twin: structural sharing makes the copy cheap, and allocating the tree is what
dominates. protopy runs the same twin in 3.7 s, so that cost belongs to the
shared object kernel and its GC, not to protoScala's frontend. `attr_lookup`,
the field-read twin, runs at 0.79× CPython and 3.5× faster than protoST on the
same machine: attribute reads go through protoCore's per-thread attribute
cache and no parallel inline caches were added. Loop arithmetic (`sum_loop`,
one million iterations) runs at 0.78× CPython and on a par with protoClojure.
The Release build is indistinguishable from the canonical RelWithDebInfo
build. The JVM column runs the same `.scala` source compiled with `scalac`
(compile time excluded, reported separately); each sample is a fresh `java`
process, so ~200 ms of JVM start-up and class loading dominate every row, and
the JIT's steady state — where the JVM would be far ahead on `fib30` and
`object_tree`, as its ~200 ms floor already suggests — is not measured.
Cold-process timing favours short-lived runtimes.

Adding the two object-model rows moved the geomean from 0.74× (8 workloads) to
0.83× (10), because they are the two heaviest workloads in the suite. That is
the honest direction: the more the suite measures real object-graph work
instead of start-up, the closer protoScala sits to CPython, and nothing here
was tuned.

**What these numbers are for.** protoScala is positioned as an agile,
interoperable, easily integrable and very simple Scala — not a fast one.
Beating the JVM on integer loops is an explicit non-goal (see
[docs/DESIGN.md](docs/DESIGN.md) §1); this suite tracks start-up and guards
against regressions. Phase 2 added the first two workloads that matter for the
positioning, `attr_lookup` and `object_tree`; the rest — actors, the full
persistent collections and interop — join the suite as the phases that enable
them land.

**Pending workloads** (need later phases; not approximated): `list_append`
and protoClojure's `sum-squares` (Phase 3 collections), `exception_latency`
(Phase 4 exceptions), the actor benchmarks (Phase 5). See
[benchmarks/README.md](benchmarks/README.md) to run the suite.

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
