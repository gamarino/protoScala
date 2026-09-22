# Changelog

All notable changes to protoScala are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

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

Fixes from the final Phase 1 review:


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
