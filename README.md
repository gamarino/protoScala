# protoScala

> **A dynamic Scala 3 dialect on the protoCore object kernel — instant start-up, persistent immutable data by default, GIL-free native actors, and transparent in-memory interop with Python, JavaScript, Smalltalk and Clojure.**

protoScala is a language runtime of the [protoCore](https://github.com/numaes/protoCore) family, alongside [protoST](https://github.com/gamarino/protoST) (Smalltalk), [protoClojure](https://github.com/gamarino/protoClojure) (Clojure), [protoJS](https://github.com/gamarino/protoJS) (JavaScript) and [protoPython](https://github.com/gamarino/protoPython) (Python).

It is **not** a JVM replacement: there is no JVM, no sbt/Maven, no Java interop and no static typechecker. It runs Scala 3 source — braces or significant indentation — with types parsed and erased, on a runtime that aims to:

- **start in under 20 ms with ~20 MB RSS**, making Scala viable for scripts and REPL-driven work;
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

**Phase 1 complete (version 0.1.0) — not production ready, open for community
review.** The binary runs Scala 3 scripts and offers a REPL: `val`/`var`/
`lazy val`/`def`, `if`/`while`, lambdas and closures, recursion (with
`StackOverflowError` instead of a crash), `println`, in both brace and
significant-indentation syntax. Classes, pattern matching, collections,
exceptions and actors are not implemented yet — see
[docs/STATUS.md](docs/STATUS.md) for the exact boundary and
[docs/ROADMAP.md](docs/ROADMAP.md) for what each later phase brings.

## Performance

Latest measured run: 2026-09-22, AMD Ryzen 5 5500U (6 cores, 12 logical CPUs),
Linux 7.0, protoScala commit `154ab1a`, load average 2.92 at start and 3.88 at
end. Full report, with versions and build types of every runtime:
[benchmarks/reports/2026-09-22-suite.md](benchmarks/reports/2026-09-22-suite.md).
Median wall-clock in ms of a **cold process** (start-up included, for every
runtime), 2 warmup + 5 timed runs, every run's printed result verified;
`—` means the runtime has no twin of that workload.

| Workload | protoScala | protoScala Release | Scala 3.9 (JVM 21) | CPython 3.14 | protopy | protost | protoclj | protoScala ÷ CPython |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (0 until 100000) | 19.2 | 19.3 | 213.6 | 34.4 | 111.0 | 31.5 | — | 0.56× |
| `fib` (fib(25)) | 42.2 | 42.8 | 193.4 | 41.3 | 168.4 | 379.4 | — | 1.02× |
| `str_concat` (2000 concats) | 16.3 | 16.0 | 192.1 | 30.7 | 128.0 | 16.4 | — | 0.53× |
| `range_iterate` (100000) | 19.3 | 19.2 | 192.1 | 36.2 | 115.7 | 55.0 | — | 0.53× |
| `tak` (18, 12, 6) | 22.1 | 22.1 | 188.8 | 32.4 | 34.5 | — | 40.6 | 0.68× |
| `fib30` (fib(30)) | 336.1 | 340.0 | 189.6 | 154.2 | 790.4 | — | 513.4 | 2.18× |
| `sum_loop` (0..1000000) | 61.3 | 63.3 | 212.9 | 86.3 | 79.8 | — | 60.3 | 0.71× |
| `factorial_100` (BigInt) | 15.0 | 15.5 | 199.8 | 29.4 | 18.1 | — | 15.3 | 0.51× |
| **Geomean vs CPython** (rows) | 0.74× (8) | 0.74× (8) | 4.30× (8) | 1.00× | 2.20× (8) | 1.62× (4) | 1.11× (4) | **0.74×** |

Cold start (`benchmarks/cold-start.sh`, 21 runs, target < 20 ms): script
17.43 ms, REPL 18.66 ms (RelWithDebInfo); 17.99 / 18.73 ms (Release).

**Reading.** Most rows measure start-up more than work: protoScala starts in
about 15 ms (`factorial_100` is almost pure start-up) and CPython in about
25-30 ms, so on short workloads protoScala comes out ahead and the 0.74×
geomean is largely a start-up figure, not a throughput claim. Where the work
dominates, the picture reverses: `fib30` (2.69M calls) is 2.2× slower than
CPython 3.14 — protoScala is call-dispatch bound, which the `fib(25)` row hides
behind its start-up advantage. Loop arithmetic (`sum_loop`, one million
iterations) runs at 0.71× CPython and on a par with protoClojure. The Release
build is indistinguishable from the canonical RelWithDebInfo build. The JVM
column runs the same `.scala` source compiled with `scalac` (compile time
excluded, reported separately); each sample is a fresh `java` process, so
~190 ms of JVM start-up and class loading dominate every row, and the JIT's
steady state — where the JVM would be far ahead on `fib30`, as its 190 ms
total already suggests — is not measured. Cold-process timing favours
short-lived runtimes.

**Pending workloads** (need later phases; not approximated): `list_append`
and protoClojure's `sum-squares` (Phase 3 collections), `attr_lookup`
(Phase 2 classes), `exception_latency` (Phase 4 exceptions), the actor
benchmarks (Phase 5). See [benchmarks/README.md](benchmarks/README.md) to run
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
