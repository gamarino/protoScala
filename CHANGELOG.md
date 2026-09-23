# Changelog

All notable changes to protoScala are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.2.0] - 2026-09-23

Phase 2: object model, apply, for, match. Built against protoCore `e43fa2e4`.

### Added

- Classes: `val`/`var`/plain constructor parameters, fields, methods, auxiliary
  constructors, `extends`/`with`, abstract members, `override`, `final`,
  `sealed`; `private` enforced as a lookup restriction (D5).
- Traits with Scala's linearization, installed as protoCore parent chains
  (DESIGN §4.3), trait parameters, `super` calls including stackable traits
  (DESIGN §4.4).
- Objects (lazy singletons), companions, case classes and case objects with
  `apply`, `unapply`, `equals`, `hashCode` (equal to the JVM's), `toString`,
  `copy` (positional and named), `canEqual`, `productArity`, `productElement`,
  `productPrefix`, `_1`..`_N`.
- Tuples `Tuple2`..`Tuple22` as case classes (never protoCore tuples,
  DESIGN §4.6).
- The universal `apply` rule, `update`, setters, method values.
- Pattern matching: literals, wildcards, variables, typed patterns,
  constructor and tuple patterns, `::`, `List(a, rest*)`, alternatives,
  binders, stable identifiers, custom extractors, guards, `MatchError`;
  pattern `val`s; `{ case ... }` literals; `isInstanceOf`/`asInstanceOf`.
- For-comprehensions (generators, guards, value definitions, patterns; `yield`
  and `do`) over `List`, `Option` and any class with
  `map`/`flatMap`/`withFilter`/`foreach`; lazy `withFilter`.
- Placeholder syntax (`_ + 1`).
- The prelude (`lib/prelude.scala`): `Option`, `Some`, `None`.
- `List(...)`, `Nil`, `::`, `map`, `flatMap`, `filter`, `withFilter`,
  `foreach`, `length`, `tail`, `drop`, `mkString`.
- REPL: class, trait, object and case-class definitions, redefinition by
  shadowing.
- Benchmarks: `attr_lookup` (twin of protoPython/protoST) and `object_tree`
  (a deep immutable object graph); GC-pressure checks for object graphs.
- Tutorial chapters 6, 7 and 9; chapters 2, 3, 5 and 14 extended.
- Conformance fixtures for the object model, case classes, `apply`, lists,
  pattern matching and for-comprehensions (`tests/conformance/07-*` to
  `12-*`), each in a braces and an indentation variant.
- Benchmark suite: `benchmarks/comparable/*.scala`, Scala 3 twins (same
  algorithm and N) of the protoPython/protoST core workloads (`int_sum_loop`,
  `fib`, `str_concat`, `range_iterate`) and of protoClojure's (`tak`, `fib30`,
  `sum_loop`, `factorial_100`), each self-checking through an `// EXPECT:`
  line and valid for `scalac` unchanged. `benchmarks/bench.sh` /
  `run_benchmarks.py` run them interleaved against Scala on the JVM, CPython,
  protopy, protost and protoclj, verify every run's result, include
  `cold-start.sh`, and write dated reports to `benchmarks/reports/`. First
  results in the README "Performance" section. Every comparable file is also
  a CTest case (`benchmarks/<file>`).

### Fixed

Fixes from the final Phase 1 review, which had not been released yet:


- REPL: an indented construct typed line by line (`while i < 3 do`, then
  its body lines) is read to its end instead of running after its first
  body line; the input ends on a blank line or when a line returns to the
  first column without continuing the construct (`else`, `end`, ...).
- REPL redefinitions shadow as in the Scala REPL: earlier code keeps the
  binding it saw (per-definition global keys), a change of kind never breaks
  it, and an input that fails defines nothing (D25).
- Value discarding: a method declared `: Unit` (also `return e` in it, the
  innermost body of a curried one), `val v: Unit = e` and `(e: Unit)` yield
  `()`; `if` without `else` yields `()` when the condition is true (D26 for
  expected types from function types).
- Forward references follow Scala's block rule ("forward reference to value
  y extends over the definition of value x"); lazy vals are hoisted, so legal
  forward references to them work.
- `return` inside a method with several parameter lists.
- `@main` rejects non-String repeated parameters and curried methods; typed
  parameters are rejected as D27.
- Deeply nested source raises `StackOverflowError` instead of crashing; the
  lambda look-ahead is linear.
- `Char` supports `*`, `/`, `%`, `max`, `min`, bitwise, shift and unary
  operators like `Int`; string literals keep an embedded NUL.
- REPL: failed and Unit-valued inputs no longer use up a `resN`; String
  results are echoed in quotes; a UTF-8 byte-order mark is accepted.
- `benchmarks/cold-start.sh` formats numbers in the C locale.

### Changed

- The `ProtoSparseListObject` platform type is now called `ProtoMap`
  (`docs/platform/PROTOMAP-SPEC.md`).
- D1 also covers integer literals (no range limit).
- The cold-start target is now < 25 ms (was < 20 ms): the embedded Scala
  prelude adds about 1.2 ms to every start and the standard library will keep
  growing (maintainer decision, DESIGN §1).
- Companion objects are linked at compile time instead of through a runtime
  `__companion__` attribute, so class prototypes stay immutable (DESIGN §4.2).
- Class and trait membership is tested with a per-class marker attribute
  rather than protoCore's `isInstanceOf`, whose traversal caps give false
  negatives on flattened chains (DESIGN §5.3, `docs/platform/ISINSTANCEOF-FIX.md`).

### Deviations (provisional, pending maintainer review)

- D28–D42 ([docs/STATUS.md](docs/STATUS.md)); D5 and D10 extended.

## [0.1.0] - 2026-09-22

### Added

- **Lexer** for Scala 3 lexical syntax: alphanumeric, operator, mixed and
  backquoted identifiers; hard and soft keywords; decimal, hex and binary
  integers with `_` and `L`; floating-point literals; characters and strings
  with escapes; triple-quoted strings; interpolated strings as structured
  tokens; nested comments.
- **Significant indentation** (Scala 3 offside rule) and braces, mixable;
  `end` markers.
- **Parser and compiler** for `val`, `var`, `lazy val`, `def` (multiple
  parameter lists, varargs, `@main`), `if`/`then`/`else`, `while`/`do`,
  blocks, lambdas, closures with per-activation captures, `return` in
  methods, imports (parsed).
- **Bytecode VM** on protoCore with SmallInteger fast paths and
  arbitrary-precision promotion (D1), Java-style double printing,
  `StackOverflowError` instead of crashes.
- **Standard surface:** `println`, `print`, methods of Int, Double, Boolean,
  Char, String, List (varargs) and functions.
- **REPL** with readline history, multi-line continuation, `:help`, `:quit`,
  `:load`.
- **Tooling:** `--disassemble`; conformance, unit and CLI test suites;
  tutorial chapters 1–5 and 14.
- Design specification (`docs/DESIGN.md`), language reference
  (`docs/LANGUAGE.md`), roadmap, status tracker and interop design.
- Platform specifications for protoCore: `ProtoMap`
  (`docs/platform/PROTOMAP-SPEC.md`) and `ProtoMPSCQueue`
  (`docs/platform/PMQ-SPEC.md`).
- Actor model on protoClojure's design with the `actor-bench.sh` suite
  (DESIGN §8).
- Implementation plans for Phase P1 and Phase 1 (`docs/plans/`).
- Phase 0 skeleton: CMake build against protoCore, `protoscala --version` /
  `--help`, GoogleTest unit harness, conformance runner with `// EXPECT:`
  directives, CLI tests.
