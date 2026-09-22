# 3. For the Scala Developer

If you write Scala 3 on the JVM, the syntax will be exactly what you expect:
braces or significant indentation, `then`/`do`/`end`, operators as methods,
curried definitions, closures. This chapter lists where protoScala is *not*
Scala 3 on the JVM, in the order a working Scala programmer is likely to hit
the differences.

The framing that matters: **protoScala is a dialect, not a port.** It keeps
the surface idiomatic; it does not try to be bytecode-, tooling- or
library-compatible with the JVM. Where JVM-specific behaviour would not earn
its keep on protoCore, protoScala diverges — and every divergence has a stable
`D<n>` id in [LANGUAGE.md §5](../LANGUAGE.md) and [STATUS.md](../STATUS.md).

> **Implementation status.** protoScala today implements the Phase 1 core:
> definitions, expressions, `if`/`while`, lambdas, closures, recursion,
> `Int`/`Long`/`BigInt`/`Double`/`String`/`Char`/`Boolean` values, `println`,
> `@main` and the REPL. Classes, objects, traits, case classes, pattern
> matching, for-comprehensions, collections, string interpolation, exceptions,
> extension methods and actors are not implemented yet (§3.3 gives the phase
> for each). D5, D6 and D7 below describe features that do not exist yet.

## 3.1 What is identical

These behave as in Scala 3:

- **Syntax, both styles.** Braces and significant indentation, in any mix;
  `if … then … else`, `while … do`, the old parenthesised forms, and optional
  `end` markers (`end if`, `end while`, `end f`).
- **Operators are methods.** `1 + 2` is `1.+(2)`; any one-argument method can
  be used infix (`3 max 5`). Precedence follows the first character of the
  operator, and operators ending in `:` are right-associative.
- **Definitions.** `val`, `var`, `lazy val` (initialised once, on first use),
  `def` with or without parameter lists, nested and mutually recursive local
  `def`s, top-level definitions visible from anywhere in the file.
- **Curried definitions and eta-expansion.** `def multiply(a: Int)(b: Int)`
  and partial application `multiply(3)` as in Scala 3, without the trailing
  `_`.
- **Varargs.** `def sum(xs: Int*)` and the splice `sum(xs*)`.
- **Closures over `var`s.** A captured `var` is shared between the closure and
  the enclosing scope; a `val` declared inside a loop body is a fresh binding
  per iteration.
- **`@main`** as the entry point, with no parameters or `args: String*`.

## 3.2 Departures

### Stable deviations (LANGUAGE.md §5)

**D1 — `Int` and `Long` never overflow.** protoScala has one integer type:
`Int`, `Long` and `BigInt` are names for an integer that is a small tagged
value while it fits and promotes to arbitrary precision when it does not.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d1-no-overflow.scala`](../../tests/conformance/tutorial/03-scala-dev-d1-no-overflow.scala)

```scala
// D1: Scala on the JVM prints -2147483648 -9223372036854775808.
@main def run(): Unit =
  val i: Int = 2147483647
  val l: Long = 9223372036854775807L
  println((i + 1).toString + " " + (l + 1))
```

Prints:

```text
2147483648 9223372036854775808
```

Code that relies on wrap-around (hash mixing, `Int.MaxValue + 1` sentinels)
behaves differently; `<<` does not wrap either (`1 << 60` is 2^60, where the
JVM gives `1 << 28`).

**D2 — `Float` is `Double`.** protoCore has one floating-point type, so `Float`
literals and arithmetic are carried out in double precision:

Fixture: [`tests/conformance/tutorial/03-scala-dev-d2-float-is-double.scala`](../../tests/conformance/tutorial/03-scala-dev-d2-float-is-double.scala)

```scala
// D2: Float is Double, so this sum is computed in double precision.
@main def run(): Unit =
  println(0.1f + 0.2f)
```

Prints:

```text
0.30000000000000004
```

On the JVM, `0.1f + 0.2f` is a `Float` and prints `0.3`.

**D3 — No implicits, givens or `using`.** Resolving them needs static types,
which protoScala does not have. They are parsed and rejected with a clear
message (`implicits and givens are not supported (D3)`), never silently
ignored.

**D4 — No static type checking.** Types are parsed and erased; there is no
exhaustiveness check either. A program `scalac` rejects fails when the
offending expression runs:

Fixture: [`tests/conformance/tutorial/03-scala-dev-d4-runtime-type-error.scala`](../../tests/conformance/tutorial/03-scala-dev-d4-runtime-type-error.scala)

```scala
// D4: no static type checker; scalac rejects this program, protoScala fails when it runs.
@main def run(): Unit =
  val n = 1
  if n then println("never")
```

It stops with:

```text
03-scala-dev-d4-runtime-type-error.scala:5: error: ClassCastException: Int cannot be cast to Boolean
```

Calling a function with the wrong number of arguments is likewise an
`IllegalArgumentException` at run time instead of a compile error.

**D5 — Access modifiers are advisory**, except `private` on the defining
class. Applies from Phase 2, when classes arrive.

**D6 — Extension methods dispatch on the runtime prototype**, not the static
type. Applies from Phase 4.

**D7 — `Map` and `Set` iteration order is unspecified** and may differ from
Scala's. Applies from Phase 3.

**D8 — No Java interop.** There are no `java.*` classes. Cross-language
interop goes through protoCore's Unified Module Discovery (Phase 6).

### Provisional deviations (Phase 1)

The following behaviours were chosen while implementing Phase 1. They are
provisional — each is recorded in STATUS.md and may change after review —
so do not rely on them in code you intend to run on the JVM.

**Script mode (provisional, see STATUS.md).** Scala 3 `.scala` files may
contain only definitions at the top level. protoScala also accepts top-level
statements and runs them in order, like a `.sc` script:

Fixture: [`tests/conformance/tutorial/03-scala-dev-script-mode.scala`](../../tests/conformance/tutorial/03-scala-dev-script-mode.scala)

```scala
// Top-level statements run in order, like a .sc script (provisional; see STATUS.md).
val words = "script mode"
println(words + " works")
```

Prints:

```text
script mode works
```

**Eager top-level initialisation (provisional, see STATUS.md).** Scala 3
initialises a file's top-level `val`s on first access. protoScala initialises
them eagerly, in source order, before calling `@main`:

Fixture: [`tests/conformance/tutorial/03-scala-dev-eager-top-level-vals.scala`](../../tests/conformance/tutorial/03-scala-dev-eager-top-level-vals.scala)

```scala
// Top-level vals are initialised eagerly, before @main (provisional; see STATUS.md).
// Scala 3 initialises them on first access, so it would never print "init" here.
val unused = { println("init"); 0 }
@main def run(): Unit = println("main")
```

Prints:

```text
main
```

The program prints `init` and then `main`; Scala 3 prints only `main`.

**Bare reference to a method (provisional, see STATUS.md).** A reference to a
`def` with a parameter list and no arguments is its function value, as with
Scala 3 eta-expansion. For `def f()` (an empty parameter list) Scala 3 would
insist on `f()`; protoScala yields the function value.

**Varargs arrive as a `List` (provisional, see STATUS.md).** Inside
`def f(xs: Int*)`, `xs` is a protoScala `List`, so `xs.toString` prints
`List(1, 2)` where the JVM prints `ArraySeq(1, 2)`.

**Strings count code points; case mapping is ASCII (provisional, see
STATUS.md).** `length`, `charAt`, `substring` and `indexOf` count Unicode code
points; the JVM counts UTF-16 units. The two agree except outside the Basic
Multilingual Plane (emoji, for instance). `toUpperCase`, `toLowerCase` and
`Char.toUpper`/`toLower` map ASCII letters only, so `"héllo".toUpperCase` is
`HéLLO`.

**No non-local `return` (provisional, see STATUS.md).** `return` inside a
`def` works; `return` inside a lambda is a compile error
(`return inside a lambda is not supported`). Scala 3 deprecates non-local
returns.

**Error names without `java.lang.` (provisional, see STATUS.md).** Run-time
errors use unqualified class names — `ArithmeticException`,
`ClassCastException`, `NoSuchMethodError` for a missing member,
`IllegalArgumentException` for a wrong argument count,
`UninitializedFieldError` for a top-level value used before initialisation,
`StackOverflowError` for runaway recursion. Compile errors print as
`file:line:col: error: message` and run-time errors as
`file:line: error: Class: message`; the exit code is 1.

**`>>>` is unsupported (provisional, see STATUS.md).** With no fixed integer
width there is no meaningful unsigned shift; `>>>` raises
`UnsupportedOperationException`.

**Tabs and spaces may not be mixed (provisional, see STATUS.md).** A file
whose indentation mixes tabs and spaces is rejected
(`indentation mixes tabs and spaces`). Scala 3 compares indentation prefixes
instead.

**Multi-line lambda bodies inside parentheses need braces (provisional, see
STATUS.md).** Scala 3 accepts `xs.foreach(x =>` followed by indented
statements. protoScala does not track indentation inside `(...)`, so a
multi-statement body there must be written `xs.foreach { x => ... }` or
`xs.foreach(x => { ... })`. A single expression spanning lines works.

**`:` at the end of a line always opens an indented region (provisional, see
STATUS.md).** `val x:` followed by `Int = 1` on the next line is rejected;
keep a type ascription on the same line as its `:`.

By-name parameters (`x: => Int`) are parsed and rejected with
`by-name parameters are not supported yet`, and `import` is parsed and
ignored until modules arrive in Phase 6.

## 3.3 What is missing

Out of scope by design: implicits and givens (D3), the static type checker
(D4), the JVM and every Java library (D8), macros and inline metaprogramming,
Java reflection.

Not implemented yet, with the phase that brings each (see
[ROADMAP.md](../ROADMAP.md)):

| Feature | Phase |
|---|---|
| Classes, objects, companions, traits, case classes, universal `apply` | 2 |
| Pattern matching, for-comprehensions | 2 |
| Collections (`List` construction, `Vector`, `Range`, `Map`, `Set`), `Option`, `Either`, `Try`, tuples | 3 |
| String interpolation (`s"…"`, `f"…"`) | 3 |
| Exceptions (`try`/`catch`/`finally`/`throw`), `super` in stackable traits, `enum`, sealed hierarchies, named and default arguments, extension methods | 4 |
| Actors and futures | 5 |
| Modules and polyglot imports (UMD), packaging | 6 |

## 3.4 What is new

What protoScala adds is the substrate rather than the syntax, and most of it
is still ahead (see [DESIGN.md](../DESIGN.md)):

- **Persistent structures.** protoCore's collections are immutable and
  structurally shared; `List`, case classes and maps map onto them directly
  (Phases 2–3).
- **Concurrency without a GIL.** Native actors with lock-free mailboxes,
  three priority bands and a cooperative `await` (Phase 5, DESIGN §8).
- **Polyglot interop in memory.** Python, JavaScript, Smalltalk and Clojure
  modules consumed through Unified Module Discovery, without serialisation
  (Phase 6, [INTEROP.md](../INTEROP.md)).
- **Instant start-up.** No JVM to warm up: the REPL prompt is the target of a
  cold-start budget measured every release.
