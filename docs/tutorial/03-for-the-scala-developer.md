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

> **Implementation status.** protoScala today implements the Phase 1 core —
> definitions, expressions, `if`/`while`, lambdas, closures, recursion,
> `Int`/`Long`/`BigInt`/`Double`/`String`/`Char`/`Boolean` values, `println`,
> `@main` and the REPL — and the Phase 2 object model: classes, objects and
> companions, traits with Scala's linearization and stackable `super`, case
> classes and case objects, tuples, `Option`, `match` with the full pattern
> vocabulary, user extractors, `List`, and for-comprehensions. Not implemented
> yet: collections beyond `List` (`Vector`, `Range`, `Map`, `Set`), string
> interpolation, exceptions, `enum`, extension methods, actors, and modules
> (§3.3 gives the phase for each). D6 and D7 below describe features that do
> not exist yet.

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
- **The object model.** Classes with constructor-parameter fields, auxiliary
  constructors, `object` singletons created on first access, companions that
  see each other's `private` members, traits with parameters, abstract
  members, Scala's right-to-left linearization and stackable `super`,
  `override` exactly where Scala 3 requires it, trait initialisation order.
- **Case classes.** `apply`, `unapply`, `copy`, structural `equals` with
  `canEqual`, `toString`, the `Product` members, and a `hashCode` that is
  **bit-identical to the JVM's** (Scala 3's MurmurHash3 with the same seed):
  `Point(1, 2).hashCode` is `-694993394` on both. Tuples `Tuple2`…`Tuple22`
  are case classes and hash the same way.
- **Pattern matching.** Literals, wildcards, variables, typed patterns, type
  tests, constructor patterns, tuples, `::`, sequence patterns with `_*`,
  alternatives, binders (`x @ p`), stable identifiers, back-quoted names,
  guards, and user `unapply` returning `Option` or `Boolean`. Patterns in
  `val` definitions and in `for` generators.
- **For-comprehensions.** The rewrite to `flatMap`/`withFilter`/`map`/
  `foreach`, lazy guards, value definitions (`y = e`), `case` generators, and
  `for` over any type that provides the methods.
- **The universal `apply` rule** and `a(i) = v` as `a.update(i, v)`.

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
Integer literals have no range limit either: `0xFFFFFFFF` is `4294967295`
(the JVM reads it as the `Int` `-1`), and `2147483648` is accepted without an
`L` suffix.

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
class. In force since Phase 2. `private` is enforced: a `private` member is
reachable from the class that defines it, from another instance of that class,
and from its companion, and nowhere else. `protected` and the qualified forms
(`private[p]`, `protected[p]`) are parsed and treated as public. Because types
are erased (D4), a violation is a **run-time** `NoSuchMethodError`, not a
compile error:

```text
06-classes-private.scala:8: error: NoSuchMethodError: value balance is not a member of Account
```

The program is
[`tests/conformance/tutorial/06-classes-private.scala`](../../tests/conformance/tutorial/06-classes-private.scala),
shown in full in [chapter 6, §6.5](06-classes-objects-and-traits.md).

Since the check runs on the receiver rather than on a static type, a foreign
object that happens to expose a member of the same name resolves to it instead
of failing.

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

**Script mode (D9, provisional — see STATUS.md).** Scala 3 `.scala` files may
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

**Eager top-level initialisation (D9, provisional — see STATUS.md).** Scala 3
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

**Bare reference to a method (D10, provisional — see STATUS.md).** A reference to a
`def` with a parameter list and no arguments is its function value, as with
Scala 3 eta-expansion. For `def f()` (an empty parameter list) Scala 3 would
insist on `f()`; protoScala yields the function value. Since Phase 2 the same
rule applies to members: `obj.m` for a method that declares parameters is a
*bound* function value that remembers its receiver, while `obj.m` for a
parameterless `def m` calls it. A `lazy val` member is a per-instance holder
whose initialiser runs, with the receiver of the first access, at most once.

**Varargs arrive as a `List` (D12, provisional — see STATUS.md).** Inside
`def f(xs: Int*)`, `xs` is a protoScala `List`, so `xs.toString` prints
`List(1, 2)` where the JVM prints `ArraySeq(1, 2)`.

**Strings count code points; case mapping is ASCII (D13, provisional — see
STATUS.md).** `length`, `charAt`, `substring` and `indexOf` count Unicode code
points; the JVM counts UTF-16 units. The two agree except outside the Basic
Multilingual Plane (emoji, for instance). `toUpperCase`, `toLowerCase` and
`Char.toUpper`/`toLower` map ASCII letters only, so `"héllo".toUpperCase` is
`HéLLO`.

**No non-local `return` (D11, provisional — see STATUS.md).** `return` inside a
`def` works; `return` inside a lambda is a compile error
(`return inside a lambda is not supported`). Scala 3 deprecates non-local
returns.

**Error names without `java.lang.` (D14, provisional — see STATUS.md).** Run-time
errors use unqualified class names — `ArithmeticException`,
`ClassCastException`, `NoSuchMethodError` for a missing member,
`IllegalArgumentException` for a wrong argument count,
`UninitializedFieldError` for a top-level value used before initialisation,
`StackOverflowError` for runaway recursion. Compile errors print as
`file:line:col: error: message` and run-time errors as
`file:line: error: Class: message`; the exit code is 1.

**`>>>` is unsupported (D15, provisional — see STATUS.md).** With no fixed integer
width there is no meaningful unsigned shift; `>>>` raises
`UnsupportedOperationException`.

**Tabs and spaces may not be mixed (D16, provisional — see STATUS.md).** A file
whose indentation mixes tabs and spaces is rejected
(`indentation mixes tabs and spaces`). Scala 3 compares indentation prefixes
instead.

**Multi-line lambda bodies inside parentheses need braces (D17, provisional —
see STATUS.md).** Scala 3 accepts `xs.foreach(x =>` followed by indented
statements. protoScala does not track indentation inside `(...)`, so a
multi-statement body there must be written `xs.foreach { x => ... }` or
`xs.foreach(x => { ... })`. A single expression spanning lines works.

**`:` at the end of a line always opens an indented region (D18, provisional —
see STATUS.md).** `val x:` followed by `Int = 1` on the next line is rejected;
keep a type ascription on the same line as its `:`.

**Top-level redefinitions (D25, provisional — see STATUS.md).** A second
top-level definition of a name in the same file replaces the first (Scala 3
rejects it). In the REPL a redefinition shadows the earlier one, as in the
Scala REPL: code compiled before keeps the binding it saw.

**Value discarding needs a written `Unit` (D26, provisional — see
STATUS.md).** `def f(): Unit = 5` returns `()`, as do `return e` inside it,
`val v: Unit = e`, `(e: Unit)` and an `if` without `else`. When `Unit` comes
only from a function type — `val f: Int => Unit = x => x + 1`, or a lambda
passed where an `A => Unit` is expected — protoScala does not know it (D4)
and the lambda returns its last value.

**`@main` takes no typed parameters (D27, provisional — see STATUS.md).** An
`@main` method takes no parameters or one `args: String*`; Scala 3's typed
parameters (`@main def m(n: Int, s: String)`, parsed from the command line)
are rejected. Convert the strings yourself (`args(0).toInt`).

By-name parameters (`x: => Int`) are parsed and rejected with
`by-name parameters are not supported yet`, and `import` is parsed and
ignored until modules arrive in Phase 6.

### Provisional deviations (Phase 2)

The object model brought seven more. They are provisional in the same sense as
the Phase 1 list — recorded in STATUS.md, open to review — and each has a
fixture that runs with the test suite. The comment on each fixture states what
Scala 3.9 prints for the same program; all of them were checked against it.

**D28 — construction of immutable instances.** An instance of a class that
declares no `var` field is an immutable protoCore object, rebuilt as each
field is stored. A `this` that escapes the constructor before the last field
is initialised therefore denotes an *earlier version*: it lacks the fields
stored after it and is neither `==` nor `eq` to the finished instance.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d28-construction.scala`](../../tests/conformance/tutorial/03-scala-dev-d28-construction.scala)

```scala
class Node(val id: Int):
  Registry.last = this
  val label = "n" + id

object Registry:
  var last: Any = null

@main def run(): Unit =
  val n = new Node(1)
  println(n.label + " " + (Registry.last == n))
```

Prints:

```text
n1 false
```

Scala prints `n1 true`. The same applies to a lambda in the class body that
captures `this`. The remedy is the one you would want anyway: do not publish
`this` from a constructor. Declaring any `var` field makes the instance
mutable and removes the effect.

**D29 — type tests follow the runtime representation.** There is one integer
type and one floating type (D1, D2), so the numeric type tests give different
answers from the JVM's.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d29-type-tests.scala`](../../tests/conformance/tutorial/03-scala-dev-d29-type-tests.scala)

```scala
@main def run(): Unit =
  println(3L.isInstanceOf[Int].toString + " " + (1L << 40).isInstanceOf[Int])
```

Prints:

```text
true true
```

Scala prints `false false` (and warns that the test is always false).
`Int`, `Long`, `Short`, `Byte` and `BigInt` are one class, `Float` and
`Double` another, so `case n: Int` matches any integer. `asInstanceOf` never
converts — `(1: Any).asInstanceOf[Double]` raises
`ClassCastException: Int cannot be cast to Double` rather than widening — and
`null.asInstanceOf[Int]` is `null`. An extractor's `unapply` is called without
the type test its parameter type would imply.

**D30 — reading an uninitialised field.** Reading a field of the instance
under construction before its initialiser has run raises `NoSuchMethodError`,
where Scala yields the default value (`0`, `null`).

Fixture: [`tests/conformance/tutorial/03-scala-dev-d30-uninitialised-field.scala`](../../tests/conformance/tutorial/03-scala-dev-d30-uninitialised-field.scala)

```scala
class A:
  val a = b + 1
  val b = 1

@main def run(): Unit = println(new A().a)
```

It stops with:

```text
03-scala-dev-d30-uninitialised-field.scala:3: error: NoSuchMethodError: value b is not a member of A
```

Scala prints `1`, because `b` is still `0` when `a` is computed. The
protoScala behaviour surfaces the initialisation-order bug instead of hiding
it behind a zero, which is why it is kept.

**D31 — no method overloading.** Two members of one template may not share a
name. With types erased there is nothing to dispatch on.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d31-no-overloading.scala`](../../tests/conformance/tutorial/03-scala-dev-d31-no-overloading.scala)

```scala
class Calc:
  def f(x: Int) = x
  def f(x: String) = x.length
```

It is rejected with:

```text
03-scala-dev-d31-no-overloading.scala:4:3: error: f is already defined in Calc
```

Scala compiles it. Auxiliary constructors *may* share the name `this`, but
only when they differ in their **number** of parameters
(`constructors of Calc must differ in their number of parameters (D31)`).
Give the alternatives distinct names, or take an `Any` and `match` on it.

**D32 — tuples stop at 22.** A tuple literal or a tuple pattern of more than
22 elements is a compile error
(`tuples of more than 22 elements are not supported (D32)`), and there is no
`Tuple23` companion to fall back on. Scala 3 uses `TupleXXL`. Nothing else
about tuples differs: they are case classes here as they are there, with
identical `==`, `hashCode` and `toString`.

**D33 — `Option.getOrElse` is eager.** By-name parameters do not exist yet, so
the default is evaluated before the call.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d33-getorelse-eager.scala`](../../tests/conformance/tutorial/03-scala-dev-d33-getorelse-eager.scala)

```scala
var log = ""
def fallback(): Int =
  log = "evaluated"
  0

@main def run(): Unit =
  val x = Some(1).getOrElse(fallback())
  println(x.toString + " " + log)
```

Prints:

```text
1 evaluated
```

Scala prints `1 ` — the trailing empty `log` shows that `fallback()` never
ran. The same applies to `orElse`. Until by-name parameters arrive (Phase 4),
keep expensive or effectful defaults out of `getOrElse`.

**D34 — `{ case … }` takes exactly one argument.** A pattern-matching function
literal is always a one-parameter function here; Scala 3 also accepts it where
a function of several parameters is expected, matching the argument tuple.

Fixture: [`tests/conformance/tutorial/03-scala-dev-d34-case-lambda-arity.scala`](../../tests/conformance/tutorial/03-scala-dev-d34-case-lambda-arity.scala)

```scala
@main def run(): Unit =
  val add: (Int, Int) => Int = { case (a, b) => a + b }
  println(add(1, 2))
```

It stops with:

```text
03-scala-dev-d34-case-lambda-arity.scala:4: error: IllegalArgumentException: wrong number of arguments for <lambda>: expected 1, got 2
```

Scala prints `3`. Write `(a: Int, b: Int) => a + b`, or pass one tuple.

**Smaller Phase 2 behaviours,** recorded in
[STATUS.md](../STATUS.md#provisional-deviations-phase-2--pending-maintainer-decision)
without a plan question of their own:

- `class`, `trait` and `object` must be defined at the **top level of a
  file**; local, nested and anonymous classes (`new T { … }`), multiple
  constructor parameter lists and `super[T].m` are rejected with
  "not implemented yet" and arrive in Phase 4.
- Scala 3's **universal apply** is not synthesised for a plain class: `C(args)`
  needs an explicit companion `apply`, or use `new`. Case classes are
  unaffected.
- **Named arguments** work only in a case class's `copy`; elsewhere they are
  rejected with `named arguments are not implemented yet` until Phase 4.
- **D35** — a refutable pattern in a `for` generator is accepted **without**
  Scala 3.9's `case` keyword and filters, as in Scala 2 (scalac rejects it and
  asks for `case`).
- **D36** — writing `case` in front of an irrefutable pattern emits no filter,
  since the pattern cannot fail, and `for (x <- xs; y = e)` compiles to two
  `map`s where dotty fuses them into one. Both give exactly the values Scala
  gives; only the number of intermediate traversals differs.
- **D37** — an **intersection type cannot be tested at run time**:
  `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time
  with `this type cannot be tested at run time`, where scalac accepts them and
  tests both components. Write two nested tests, or one test plus a guard. The
  unparenthesised `case v: A & B` is a syntax error in scalac too. A
  parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by
  scalac, and both test only the tuple's erasure.
- **D39** — `List` hash codes differ from the JVM's (they stay consistent with
  `==`); case classes, tuples, strings and numbers are bit-identical.
- **D38** — the default `toString` of a plain instance is
  `Name@<identity hash>`; an `object` prints `O@…` where the JVM prints
  `O$@…`, and `println(Tuple2)` prints `Tuple2@<hash>`.
- A `match` that finds no case raises `MatchError: 5 (of class Int)`, naming
  the Scala class; the JVM names the boxed one (`java.lang.Integer`).
- **D40** — a `var` that shadows a concrete inherited `var` without `override`
  is reported as `cannot override a mutable variable`, where scalac says
  "needs `override` modifier".

## 3.3 What is missing

Out of scope by design: implicits and givens (D3), the static type checker
(D4), the JVM and every Java library (D8), macros and inline metaprogramming,
Java reflection.

Delivered in Phase 2, so no longer on this list: classes, objects,
companions, traits with linearization and stackable `super`, case classes and
case objects, tuples, `Option`, the universal `apply` rule, `List`, pattern
matching and for-comprehensions.

Not implemented yet, with the phase that brings each (see
[ROADMAP.md](../ROADMAP.md)):

| Feature | Phase |
|---|---|
| Collections beyond `List` (`Vector`, `Range`, `Map`, `Set`), `Either`, `Try`, the rest of the `List` API | 3 |
| String interpolation (`s"…"`, `f"…"`) | 3 |
| Exceptions (`try`/`catch`/`finally`/`throw`), `enum`, exhaustiveness, named and default arguments, by-name parameters, extension methods, local/nested/anonymous classes, multiple constructor parameter lists, `super[T]` | 4 |
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
