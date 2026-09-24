# Changelog

All notable changes to protoScala are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- **`Future` takes its body by name** — `Future(expr)` and `Future { … }`, as in
  Scala, instead of `Future(() => expr)`. The maintainer overturned D47 on
  2026-09-23 on the ground of least surprise for the Scala programmer.
- **An actor handler may return a bare `newState`** as well as
  `(newState, reply)`. With no reply the ask's future completes with `()`, so `?`
  on such an actor is a `Future[Unit]`. A `Tuple2` result is still read as the
  pair form. The maintainer overturned D45 on 2026-09-23, same ground: a handler
  that only updates state should not have to invent a reply.

### Added

- **By-name parameters** (`x: => T`) on a `def` (any parameter list, including a
  curried one), a method, and a plain constructor parameter. The call site
  compiles the argument into a thunk and the body forces it on every read, so an
  argument used twice evaluates twice and one never used never evaluates —
  verified against scalac 3.9.0. New opcode `FORCE_THUNK` (38).
- **D53**: a by-name parameter is honoured only where the compiler resolves the
  call site to the declaration. A method reached through a dynamic send, and a
  `def` taken as a function value, evaluate the argument once at the call.
  Scala resolves all of these from static types.

## [0.5.0] - 2026-09-24

Phase 4: exceptions, `super[T]`, enums, named and default arguments, extension
methods and templates nested in an `object`. Built against protoCore `983bbf98`
(2.1.0) — the same commit 0.4.0 was built against; **Phase 4 needed nothing new
from protoCore** (P3). Phase 5 shipped out of order as 0.3.0 and Phase 3 as
0.4.0, so Phase 4 is 0.5.0.

### Added

- **`try` / `catch` / `finally` / `throw`, with pattern-matched handlers.** A
  `catch` body is `compileMatch`'s own cascade with `RETHROW` instead of
  `MATCH_ERROR`, so a handler may use constructor patterns, guards and type
  patterns, and an exception no clause matches continues outward rather than
  being replaced. `try` is an expression. Both syntaxes.
- **The `Throwable` hierarchy** in `lib/prelude.scala`: twenty ordinary
  protoScala classes, the JVM's names without the `java.lang.` prefix (D73), so
  `case e: ArithmeticException` is the same per-class marker test as
  `case p: Point`.
- **Native error translation.** Every failure the runtime raises becomes a real
  exception value of the class its name means, materialised lazily so the uncaught
  path allocates nothing. A compiler or VM defect (`std::logic_error`) is
  deliberately **not** catchable (D74).
- **Exceptions across an actor turn and a suspended `await`.** A handler exception
  fails that message with the exception value itself and leaves the actor alive;
  a resumed frame runs with its handler table active; no `finally` runs on a
  suspension (D75); and a failed `await` raises **at its call site**, so
  `try { f.await } catch { … }` works across a cooperative suspension.
- **`super[T].m`**, which probes `T` itself first and then continues after it
  (D76).
- **`enum` and sealed hierarchies**, lowered entirely in the frontend to a sealed
  abstract class, one `case object` or `case class` per case, and a companion with
  `values` / `valueOf` / `fromOrdinal` — whose messages are scalac's own, byte for
  byte. `ordinal` is a `val` and `toString` is Product's, so `enum` needs no
  native method and no new opcode (D77, D79).
- **Named and default arguments** for Scala-defined methods, constructors,
  case-class `apply`/`copy`, function values and local functions, bound in the
  **callee** through protoCore's `keywordParameters` keyed by the address of the
  interned parameter-name symbol — the same convention a foreign callee will
  receive them by, documented in `docs/INTEROP.md` §7 (D81, D88, D89).
- **Extension methods**, installed as attributes of the receiver type's prototype
  so dispatch is the ordinary prototype walk (D6), global for the session (D82,
  D83) — and with them **custom string interpolators**, over a new prelude
  `StringContext`.
- **Templates nested in an `object`**, lifted to the top level with a qualified
  name and resolvable unqualified inside the object (D80 keeps `class`-nested,
  local and anonymous classes out).
- **Multiple constructor parameter lists**, concatenated into one flat list (D84).
- New opcodes: `CALL_KW` (80), `THROW` (96), `RETHROW` (97).
- Tutorial chapters 11 (Exceptions) and 12 (Enums and sealed hierarchies), plus
  new sections in chapters 2, 5, 6, 10 and 13; a conformance fixture for every
  runnable snippet.

### Changed

- **`Failure` carries a `Throwable`.** `RuntimeError` and `__mkRuntimeError` are
  gone from the prelude, and `Try`'s `recover`/`recoverWith` take a `Throwable`,
  so a failed `Future` and a `Try` both carry a value a `catch` clause can match.
  **D44 retired.**
- **`Priority` is a real `enum`** whose ordinals are the scheduler's own band
  indices; a plain `Int` band is still accepted. **D52 retired.**
- **A custom string interpolator** is an extension method on `StringContext`; an
  undefined one is a run-time `NoSuchMethodError` naming the member it looked for
  rather than a compile error. **D56 retired.**
- **Class prototypes are mutable.** An extension is installed after the class
  exists and every instance already created must see it. Measured cost:
  `object_tree` +2.8 % cycles, `attr_lookup` inside its error bars — both within
  the phase's 3 % gate, and class *creation* is cheaper.
- `Throwable.getClass` answers the class's simple name as a `String` (D86); the
  internal global `__classNameOf` supplies it, and `__installExtension` installs
  an extension on a prototype the compiler cannot name.

### Fixed

- A parameter **default** may read an enclosing local. It previously did so only
  when the body happened to read the same local — a local `def` is hoisted, so a
  default that captured an unboxed slot read `null`. The capture analysis now
  walks the defaults and a discarded pre-pass forces the callee's captures through
  the ordinary resolver, so the same program no longer works or fails for an
  unrelated reason.
- Phase 3's two `XFAIL` fixtures waiting on `enum` are flipped and verified
  against scalac; the prose of `map-enum-case-keys.scala` is corrected — a
  singleton `enum` case is a case object, so it is a **value** key, not an
  identity key. The two are indistinguishable at run time (a case object has one
  instance), which is why only the white-box test can tell.

### Performance

- No workload regression. The wall-clock suite could not answer the question on a
  host whose own CPython reference moved by 8-23 % between runs, so the two
  mechanisms this phase puts in hot paths were measured directly with
  `perf stat -r 3`: the **per-frame retry loop is free** (`fib30` -3.9 % cycles,
  `attr_lookup` -5.2 %, `tak` +3.1 %, all inside the noise) and **mutable class
  prototypes cost `object_tree` +2.8 % cycles / +1.4 % instructions**, inside the
  phase's 3 % gate and recorded rather than hidden.
- The **actor suite re-run on 0.5.0** (nine modes x six worker counts, 5
  interleaved samples, 450/450 verified, nothing killed) shows the two Phase 4
  mechanisms cost the scheduler nothing: `Priority`-as-`enum` leaves the High-band
  ask p50 at 27.2 µs (w=1) to 40.4 µs (w=16), against 26.7-41.9 µs when `Priority`
  was three integers on an object; `saturation-8` / `saturation-32` peak at 3.32x
  and 3.48x, at or above the previous `ProtoMPSCQueue` series, on a busier host.
  Report: `benchmarks/reports/2026-09-24-phase4-actors.md`.

### Known issues

- **Cold start is above the < 25 ms budget** by about 1 ms, and the cause is
  measured, not guessed: 0.4.0 and 0.5.0 interleaved in one window over three
  rounds of 21 verified runs give 23.91 ms against 25.22 ms (script) and 24.46
  against 25.58 ms (REPL), and a probe build with twenty *more* prelude exception
  classes costs a further +1.19 ms -- monotone in all three rounds. At roughly
  60 us per prelude class, the prelude's growth from 156 to 200 lines (twenty
  exception classes, `StringContext` and the `Priority` enum, all compiled at
  every start-up) accounts for the whole regression, which rules out the engine's
  exception machinery as the cause. Not claimed as met; a precompiled prelude is
  a Phase 6 decision.
- **The foreign half of named arguments is unexercised**: UMD is Phase 6, so
  `tests/conformance/23-named-arguments/foreign-python-*.scala` are `XFAIL` with
  their expected output recorded.
- `Mailbox.EightProducersLoseNothingAndDuplicateNothing` still aborts under
  `PROTOCORE_HEAP_LIMIT_CELLS=20000`, pre-existing on `bca0352`.

## [0.4.0] - 2026-09-23

Phase 3: fast paths, collections, the prelude and string interpolation. Built
against protoCore `983bbf98` (2.1.0), which supplies `ProtoMap` and the
hashed-collection helper. Phase 5 shipped out of order as 0.3.0, so Phase 3 is
0.4.0 and Phase 4 will be 0.5.0.

### Added

- **String interpolation executes.** `s"…"` and `raw"…"` compile to the new
  `CONCAT` opcode (39), which converts each piece with `toScalaString` and joins
  them with `ProtoString::appendLast` — an O(log n) rope join that copies
  neither side, so `s"$a$b"` on two strings allocates one node. `f"…"` compiles
  to a call of the native `__fmt` with every specifier a compile-time constant,
  so a malformed one is a *compile* error at the interpolation's position, as
  scalac reports it. A hole is parsed by the same Lexer/Layout/Parser pipeline
  as the file, so it may hold any expression, nested interpolations included.
- **`Vector`, `Range`, `Map` and `Set`.** `Map` and `Set` are built on
  protoCore's `ProtoMap` and are read and written only through
  `proto::hashedPut`/`hashedGet`/`hashedRemove`/`hashedForEach` with one
  `KeySemantics` whose callbacks are protoScala's own `scalaHash` and
  `valuesEqual` — so a `Map` can never disagree with `==`, and Scala's
  cooperative numeric equality makes `1`, `1L` and `1.0` one key. `Range` is
  arithmetic: `length`, `apply`, `head`, `last`, `sum` and `contains` are O(1)
  and it is never materialised except by an explicit conversion.
- **The full `List` surface**, and `Vector` sharing *one* implementation with it
  installed on both prototypes, so the two cannot drift; a result is of the
  receiver's own kind. `sorted`/`sortBy`/`sortWith` are a stable merge sort;
  `foldLeft`/`foldRight` accept both `xs.foldLeft(z)(f)` and `xs.foldLeft(z, f)`.
- **Cross-kind `Seq` equality and hashing**: `List(1,2) == Vector(1,2) ==
  (1 to 2)`, all three hash alike, and a `Map` keyed by one is found by another.
  Decided once, in `valuesEqual` and `scalaHash`, over one allocation-free
  `SeqView`, so `==` and `.equals` cannot split and
  `(0 until 1000000000) == List(1)` is O(1).
- **`Either`/`Left`/`Right`**, an extended `Option` (`fold`, `toRight`,
  `toLeft`, `orNull`, `forall`, `count`, `zip`, `iterator`, `toSeq`) and an
  extended `Try` (`map`, `flatMap`, `foreach`, `recover`, `recoverWith`,
  `orElse`, `toEither`), with `Try { … }` taking its body by name.
- **The `String` surface**: `split`, `replace`, `stripMargin`, `stripPrefix`,
  `stripSuffix`, `lastIndexOf`, `capitalize`, `equalsIgnoreCase`, `compareTo`,
  `toBoolean`, `format` (through the same formatter as the `f` interpolator) and
  the collection-like `toList`, `head`, `last`, `init`, `take`, `drop`,
  `takeWhile`, `dropWhile`, `map`, `filter`, `foreach`, `mkString`.
- **`->` on `Any`**, so `k -> v` is the `Tuple2` `(k, v)` — a case-class
  instance, never a `ProtoTuple`.
- **Benchmark suite v1** completed with `list_ops` and `map_build`, both printing
  the work they did and verified by the runner before any rate is computed;
  recorded in `benchmarks/RESULTS.md` with the machine, both commits and the
  load average.
- **Tutorial chapters 8 and 10**, with a conformance fixture per runnable
  snippet, and the two bridge chapters extended.
- **Deviations D54–D71** (D57, D60 and D64 unused — the divergences they were
  reserved for were removed by the rulings of 2026-09-23 and by by-name
  parameters landing). D71 is new and was not foreseen by the plan.

### Fixed

- The dispatch loop interned `unary_-`, `unary_!` and every binary operator
  symbol on **every execution**. All three families now read their interned
  names from `RuntimeLayout`: 1.630 → 1.466 Gcycles on a 200000-iteration
  operator-overload loop (`perf stat -r 3`), 10.0 % fewer.
- A name read inside an interpolation hole was never boxed as a capture, so
  `var v = 1; val f = () => s"$v"` would have read a stale value.
- The lexer scanned `$name` with the full identifier rule, in which `$` is a
  legal character, so `s"$a$b"` lexed as one hole named `a$b`; scalac reads it
  as two, which is what the parser now does.
- The 22 `-Wmissing-field-initializers` warnings the by-name work left behind,
  so the tree builds warning-free again.

### Changed

- `O(args)` on an object honours `apply`'s by-name parameters, as
  `O.apply(args)` already did. Without it `Try { … }` would have evaluated its
  block on the caller while `Try.apply { … }` would not, and DESIGN §5.1 makes
  the two the same call.
- `Map`/`Set` iteration is ascending-hash (D58) — deterministic for a given key
  set, unrelated to insertion order or to Scala's. Every fixture that prints
  more than one entry sorts first.

### Not done, deliberately

- A SmallInteger fast path on the `EQ`/`NE` opcodes. It was written, measured
  and backed out: `valuesEqual` already fast-paths two SmallIntegers, so it won
  nothing on its own workload (inside the error bars) and cost 8 % of
  `sum_loop`'s cycles through code layout in the hottest function. The
  measurement is recorded at the opcode.
- `Failure`'s payload still carries `RuntimeError`; Phase 4 re-points it and
  closes D44 and D50 with it.

## [0.3.0] - 2026-09-23

Phase 5: actors, priority bands and cooperative futures. Built against
protoCore `bf972d3f` (2.0.0). Phase 5 was implemented before Phases 3 and 4,
so the minor version goes from 0.2.0 straight to 0.3.0.

### Added

- **Actors** (DESIGN §8.1, §8.2): `Actor.spawn(state)(handler)` where a handler
  returns `(newState, reply)` (D45; a bare `newState` is accepted since the
  ruling of 2026-09-23, see [Unreleased]); `a ! msg`, `a ? msg`,
  `a.send(msg, priority)`, `a.ask(msg, priority)`, `a.value`,
  `Actor.isActor`, `Actor.stats`, the printed form `Actor(<state>)`.
- **Three priority bands** per actor (`Priority.High`/`Medium`/`Low`, D52),
  drained in strict priority order, eight messages per turn.
- **A GIL-free worker pool** of protoCore threads (`PROTOSCALA_ACTOR_WORKERS`,
  default `max(2, cores − 2)` capped at 16) over three lock-free ready stacks
  with ABA-tagged heads and a type-stable node pool, with spin-before-park
  inside a `ProtoContext::UnmanagedScope`. The pool starts on the first
  `Actor.spawn`, so a script that uses no actor pays nothing at start-up.
- **The single-method invariant**: one atomic per actor across claim, wake and
  suspension, so an actor never runs twice at once however many threads send
  to it. Checked by 8 threads × 25 000 sends at 1, 2, 8 and 16 workers.
- **Futures** (DESIGN §8.3): `await`, `isCompleted`, `value: Option[Try[T]]`,
  `map`, `flatMap`, `recover`, `onComplete`, `Future(() => e)` (D47; taken by
  name since the ruling of 2026-09-23, see [Unreleased]),
  `Future.successful`, `Future.failed`. Continuations run on the thread that
  completes the future (D48).
- **Cooperative `await` inside an actor**: the handler's call chain is
  snapshotted frame by frame during unwinding and rebuilt on resume, so the
  worker is released while the actor waits and the `await` workloads complete
  with a single worker. An `await` whose chain cannot be snapshotted is
  refused instead of corrupting the frame (D43).
- `Try`/`Success`/`Failure` and `RuntimeError(className, message)` in the
  prelude, moved up from Phase 3 (D44).
- `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`,
  `System.currentTimeMillis()`, `System.getenv(name)` (D49).
- The seven actor benchmark modes of DESIGN §8.5
  (`benchmarks/actor-bench.sh`), each self-reporting the work it did and
  verified by the runner before any rate is computed, with a protoClojure
  comparison measured on the same machine and day.
- Tutorial chapter 13, "Actors and futures", with every snippet as a fixture.

### Changed

- `protoscala --version` now names the actor mailbox backend, so a benchmark
  report cannot misattribute its numbers:
  `protoScala 0.3.0 (actor mailboxes: CAS list)`.
- `~Session` joins every actor worker before the `ProtoSpace` is destroyed, on
  every exit path.
- `ExecutionEngine::execute` is split into a prologue and `runLoop`, and every
  re-entrant opcode records the in-flight call's base slot. No new opcodes: the
  128..159 range stays reserved (plan Task 0 A0-11).

### Known limitations

- The actor mailbox is the **CAS'd `ProtoList` fallback**, not protoCore's
  `ProtoMPSCQueue`: protoCore 2.0.0 does not carry `newMPSCQueue` yet. The
  `Mailbox` seam switches to it in one file once Phase P2 merges.
- No supervision trees, no `ExecutionContext`, no actor timeouts and no
  `Await.result(f, duration)`: an `await` waits forever, and the shutdown
  reports any actor still parked on a future that never completed.
- An actor lives as long as the session (D46), and `Future.apply` creates one
  actor per call.
- D43–D52 are recorded in `docs/STATUS.md` as provisional, pending the
  maintainer's review.

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
