# protoScala

> **A dynamic Scala 3 dialect on the protoCore object kernel — instant start-up, persistent immutable data by default, GIL-free native actors, and transparent in-memory interop with Python, JavaScript, Smalltalk and Clojure.**

protoScala is a language runtime of the [protoCore](https://github.com/numaes/protoCore) family, alongside [protoST](https://github.com/gamarino/protoST) (Smalltalk), [protoClojure](https://github.com/gamarino/protoClojure) (Clojure), [protoJS](https://github.com/gamarino/protoJS) (JavaScript) and [protoPython](https://github.com/gamarino/protoPython) (Python).

It is **not** a JVM replacement: there is no JVM, no sbt/Maven, no Java interop and no static typechecker. It runs Scala 3 source — braces or significant indentation — with types parsed and erased, on a runtime that aims to:

- **start in under 20 ms with ~20 MB RSS**, making Scala viable for scripts and REPL-driven work;
- map **case classes and functional collections** onto protoCore's persistent, structurally shared data;
- run **native actors without a GIL**: messages are pointers to immutable data, mailboxes are lock-free with three priority bands, and `await` inside an actor suspends cooperatively instead of blocking a thread;
- **import modules from sibling runtimes** (`import py.numpy as np`) through protoCore's Unified Module Discovery, with no serialization at the boundary.

protoScala is also a **platform validation project**: protoCore aims to be a solid base for implementing any language, and each new language exposes missing capabilities. Those capabilities are added to protoCore as part of this project — the first two are `ProtoSparseListObject`, a persistent map with GC-traced object keys, and `ProtoMPSCQueue`, a lock-free GC-traced actor mailbox; both also serve protoClojure and protoST.

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

**Pre-alpha — design complete, implementation starting (version 0.0.1).**
The repository builds against protoCore and runs its test harnesses; the
binary does not run Scala programs yet. See:

- [docs/DESIGN.md](docs/DESIGN.md) — the approved design specification
- [docs/LANGUAGE.md](docs/LANGUAGE.md) — supported language and departures from Scala
- [docs/ROADMAP.md](docs/ROADMAP.md) — phases with verifiable done-when criteria
- [docs/STATUS.md](docs/STATUS.md) — living implementation tracker
- [docs/TUTORIAL.md](docs/TUTORIAL.md) — dual-audience tutorial (Scala programmers; Python/JavaScript developers)
- [docs/INTEROP.md](docs/INTEROP.md) — UMD polyglot interop
- [docs/platform/](docs/platform/) — protoCore extensions specified by this project
- [docs/plans/](docs/plans/) — task-level implementation plans

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

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Gustavo Marino.
