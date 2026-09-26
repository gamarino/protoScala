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
> vocabulary, user extractors, `List`, and for-comprehensions; the Phase 5
> concurrency layer: actors, priority bands, futures and
> `Try`/`Success`/`Failure`; and the Phase 3 collection and string layer:
> `Vector`, `Range`, `Map`, `Set`, `Either`, the full `List` surface, string
> interpolation (`s"…"`, `f"…"`, `raw"…"`) and the `String` methods. Not
> implemented yet: exceptions, `enum`, extension methods and modules (§3.3
> gives the phase for each). D6 below describes a feature that does not exist
> yet.

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
- **Collections, where they exist.** `List`, `Vector`, `Range`, `Map`, `Set`,
  `Option`, `Either` and `Try` carry Scala's method names with Scala's
  meanings (chapter 8 lists the surface of each). Sequence equality and
  hashing agree across `List`, `Vector` and `Range`, so
  `List(1,2) == Vector(1,2) == (1 to 2)` and a `Map` keyed by one is found by
  another. `Map` and `Set` classify keys as Scala does — by value for numbers,
  strings, `Char`s, `Boolean`s, tuples, case classes and sequences, by
  identity for a plain class that does not override `equals` — with
  cooperative numeric equality, so `1`, `1L` and `1.0` are one key. `Either`
  is right-biased, `sorted` is stable, and `Try { … }` takes its block by
  name.
- **String interpolation.** `s"…"`, `f"…"` and `raw"…"` with both hole forms
  (`$name`, `${expr}`), `$$`, interpolated triple-quoted literals, nested
  interpolations, and `stripMargin`.

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
type, so a value reached through `Any` answers an extension too. In force since
Phase 4; chapter 6 §6.8 has the example, and D82 and D83 record what being
installed on a prototype costs.

**D7 — `Map` and `Set` iteration order is unspecified** and may differ from
Scala's. In force since Phase 3; D58 below states what the order actually is
and what the tutorial does about it.

**D8 — No Java interop.** There are no `java.*` classes. Cross-language
interop goes through protoCore's Unified Module Discovery, which is what the
`py.`, `js.`, `st.` and `clj.` import prefixes reach (chapter 15).

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

By-name parameters (`x: => Int`) were rejected in Phase 1 and now work, with
the resolution limit D53 records. `import` was parsed and ignored until modules
arrived; it now loads a module and binds names (chapter 15, D90-D96).

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

**D31 — no method overloading in a template.** Two members of one template may
not share a name. With types erased there is nothing to dispatch on. *Top-level*
`def`s are different: they overload by **number** of parameters (D111, below).

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
Give the alternatives distinct names, or take an `Any` and `match` on it — or, at
the top level of a file, give them different arities (D111).

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
ran. The same applies to `orElse`. By-name parameters themselves landed in
Phase 3 (D53) and `Try { … }` uses one; the prelude's `getOrElse` has not been
converted to one yet, so keep expensive or effectful defaults out of it.

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

- `class`, `trait` and `object` must be defined at the **top level of a file or
  in an `object`** (Phase 4). A template nested in a `class` or a `trait`, a local
  class inside a block and an anonymous class (`new T { … }`) are rejected (D80).
  Multiple constructor parameter lists (D84) and `super[T].m` (D76) arrived in
  Phase 4.
- **D41** — Scala 3's **universal apply** (a creator application, `C(args)`
  standing for `new C(args)`) is not synthesised for a plain class:
  `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac prints
  `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes
  and case objects are unaffected — their companion `apply` is synthesised.
- **Named and default arguments** work everywhere since Phase 4 — on a `def`, a
  method, a constructor, a case class's `apply` and `copy`, a function value and a
  local function — and are bound in the callee (D81, D88, D89; chapter 5 §5.10).
- **D35** — a refutable pattern in a `for` generator is accepted **without**
  Scala 3.9's `case` keyword and filters, as in Scala 2 (scalac rejects it and
  asks for `case`). *With* `case` the generator filters exactly as Scala 3 does,
  whatever the pattern. The one gap is the no-`case` form of a **tuple**
  pattern, which trusts every element to be a pair — `for ((a, b) <- List(1,
  (1, 2)))` raises `MatchError` on the `1`, where scalac rejects the program.
- **D36** — `for (x <- xs; y = e)` compiles to two `map`s where dotty fuses
  them into one. It gives exactly the values Scala gives; only the number of
  intermediate traversals differs.
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
- **D42** — a `match` that finds no case raises `MatchError: 5 (of class Int)`,
  naming the Scala class; the JVM names the boxed one
  (`scala.MatchError: 5 (of class java.lang.Integer)`).
- **D40** — a `var` that shadows a concrete inherited `var` without `override`
  is reported as `cannot override a mutable variable`, where scalac says
  "needs `override` modifier".

### Concurrency (Phase 5, D43–D53)

Actors, priority bands and futures are described in
[chapter 13](13-actors-and-futures.md); this is their departures catalogue.

- **D43** — `await` suspends only a chain of protoScala frames each stopped at
  a call instruction. Inside a native higher-order method (`map`, `foreach`,
  `withFilter`, a `Future` continuation), or under an instruction that calls
  back into Scala without being a call site (`==` reaching a user `equals`),
  it raises `UnsupportedOperationException` instead of suspending; the ask's
  future receives that failure and the actor stays alive.
- **D45** — an actor handler returns `(newState, reply)` or a bare `newState`,
  which means there is no reply to give; `?` then completes with `()`. A
  `Tuple2` result is always read as the pair form, so an actor whose state is a
  pair returns it inside one. A handler that produces no value at all raises
  `IllegalArgumentException` and fails that message, leaving the actor's state
  unchanged.
- **D46** — an actor lives as long as the session: it is anchored in a
  registry so the collector can reach it and everything it holds while it is
  only referenced by the (C++) ready stacks. `Future.apply` creates one actor
  per call.
- **D47** — `Future.apply` takes its body by name, as in Scala: `Future(expr)`
  and `Future { … }` both work.
- **D48** — `map`/`flatMap`/`recover`/`onComplete` run their continuation on
  the thread that completes the future, or immediately on the caller when it
  is already complete — there is no `ExecutionContext`. A continuation may not
  `await` (D43).
- **D49** — `Thread` and `System` are runtime facilities, not the JVM's:
  `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`,
  `System.currentTimeMillis()`, `System.getenv(name)` (`""` when unset).
  `nanoTime` is a monotonic clock; only differences are meaningful.
- **D51** — `actor.value` reads the state without sending a message, so it may
  observe a state older than a send that is still queued.
- **D53** — a by-name parameter is honoured only where the compiler can name the
  callee: a call by name to a top-level or local `def`, a method of the template
  being compiled, a method of an `object`, a class's primary constructor, and the
  runtime's own by-name signatures. A method reached through a dynamic send, and
  a `def` taken as a function value, evaluate the argument once at the call.
  Scala resolves all of these from static types.

**Retired by Phase 4.** Three ids in this group were provisional behaviours that
existed only because exceptions and `enum` had not landed: **D44** (a failed
`Future` carried `RuntimeError(className, message)` rather than a `Throwable`),
**D50** (awaiting a failed future abandoned the rest of the handler) and **D52**
(`Priority` was three integers on an object). All three are gone; chapters 11 and
12 describe what replaced them.

### Collections and strings (Phase 3, D54–D71)

The collection library and string interpolation are described in
[chapter 8](08-collections.md) and
[chapter 10](10-strings-and-interpolation.md); each ends with its own
departures section, and this is the catalogue. Every id has a fixture that
runs with the test suite.

- **D54** — `s"…"` and `raw"…"` compile to a `CONCAT` opcode rather than to
  a call on `StringContext`, so a user-defined or shadowed `StringContext` is
  never consulted. The string produced for a given input is Scala's; only the
  strategy differs.
- **D55** — the `f` interpolator supports `%s %b %c %d %o %x %X %e %E %f %g %G
  %%` with the flags `-`, `+`, space, `0`, `,` and `#` and a
  `width.precision`, and nothing else: there is no `%n` (write `\n`), no
  locale (`,` always groups with an ASCII comma and the decimal point is
  always `.`), and no `%h`, `%a`, `%t` or argument index (`%1$s`). The `#`
  flag is parsed but currently emits no `0x`/`0` prefix.
- **D58** — `Map` and `Set` iterate in ascending order of their internal
  hashes, which is neither insertion order nor the keys' own order and may
  change between releases. Scala guarantees no order either; every fixture in
  the suite sorts before printing, and so should your code. There is no
  `ListMap` or `SortedMap`.
- **D59** — `Vector(1, 2).hashCode` equals `List(1, 2).hashCode`, as `==`
  requires, and both differ from the JVM's `Seq` hash. Case classes, tuples,
  strings and numbers remain bit-identical to the JVM's (D39 is the `List`
  half of the same statement).
- **D61** — a `Range` bound must fit a 54-bit integer
  (`a Range bound must fit a 54-bit integer`), which keeps every `Range`
  method allocation-free. Scala's `Range` is limited to `Int` bounds, so no
  Scala program is affected.
- **D62** — `sorted` uses the runtime's own ordering and fails with
  `sorted needs comparable elements; use sortWith` on anything it cannot
  order, because there is no `Ordering` to resolve (D3). Numbers, strings,
  `Char`s and `Boolean`s sort exactly as in Scala; `sortBy` and `sortWith`
  cover the rest.
- **D63** — there is no `collect`: it takes a `PartialFunction`, which does
  not exist here. Write `filter(p).map(f)`, or `flatMap` with a `match` that
  answers `List(x)` or `Nil`.
- **D65** — `Seq` and `Iterable` are not provided and are not scheduled.
  `List`, `Vector`, `Range`, `Map` and `Set` share no common ancestor, and
  `case xs: Seq[_]` is rejected at compile time with `Not found: type Seq`. An
  annotation `xs: Seq[Int]` is harmless, since annotations are erased (D4).
- **D66** — `%e`, `%f` and `%g` convert through a `Double`, so an integer
  above 2^53 prints rounded; `%d` is exact at any size (D1). Matching the JVM
  would need a big-decimal formatter.
- **D67** — `Either` and `Try` have no `withFilter`, so a guard in a
  comprehension over them fails with `withFilter is not a member of Right`.
  Scala's needs a `Left` to fall back to, which needs the static type.
- **D68** — `Range.map` and a `for … yield` over a `Range` answer a `List`,
  where Scala answers an `IndexedSeq`. The elements and their order are
  Scala's; `IndexedSeq` is a `Seq` trait, which D65 rules out.
- **D69** — `String.split` answers a `List`, not an `Array`; there is no
  `Array` type (D12 already routes varargs to a `List`). Printing the result
  gives `List(a, b, c)` where the JVM gives `[Ljava.lang.String;@…`.
- **D70** — `String.split` takes a **literal** separator, not a regular
  expression: `"a.b.c".split(".")` is `List(a, b, c)` here and an empty
  sequence in Scala. protoScala has no regular-expression engine and would not
  add one for `split`; `split(",")`, the common case, is identical.
- **D71** — a key that overrides `equals` but not `hashCode` misses its own
  entry at once. Scala's `Map1`…`Map4` compare small maps by `==` alone, so
  the same bug is hidden there until the map reaches five entries; protoScala
  is hashed from the first entry and reports it immediately.

**D57, D60 and D64 are unused ids.** They were drafted for divergences that
the rulings of 2026-09-23 removed: `Char` keys follow Scala (a `Char` is a
value key, so `'a'` and `97` are one key), `(0 until 3) == List(0, 1, 2)` is
`true` as in Scala, and `Try { … }` takes its block by name now that by-name
parameters have landed. The ids are not reused, because documents already
cite them.

### Exceptions, enums, arguments and extensions (Phase 4, D72–D89)

Described in [chapter 11](11-exceptions.md), [chapter 12](12-enums-and-sealed-hierarchies.md),
[chapter 5](05-functions-and-closures.md) §5.10 and
[chapter 6](06-classes-objects-and-traits.md) §6.7–§6.9; each ends with its own
departures section, and this is the catalogue. **D78 is deliberately unused:** the
plan proposed accepting an `enum` case without its qualifier, and matching Scala —
which requires `Colour.Red` — costs nothing, so no deviation was taken.

- **D72** — a `finally` body that itself throws replaces the in-flight exception.
  Scala does the same and warns about it; protoScala has no warnings (D4).
- **D73** — exception class names are unqualified (`ArithmeticException`, never
  `java.lang.ArithmeticException`) and the hierarchy is the small one of chapter
  11 §11.5; there is no `java` namespace to qualify with (D8).
- **D74** — a protoScala defect (a `std::logic_error` from the compiler or the VM)
  is deliberately **not** catchable, so a bug in the language can never be masked
  by `catch { case e: Throwable => }`.
- **D75** — an actor suspended on a future that never completes never runs the
  `finally` of the `try` it suspended inside; the shutdown diagnostic reports how
  many actors are parked, and that is the only notice.
- **D76** — `super[T].m` accepts any ancestor in the linearization, where scalac
  requires `T` to be a **direct** parent. A class's description carries the
  flattened linearization and no direct-parent list. Permissiveness only.
- **D77** — `enum` `values` answers a `List`, where Scala answers an `Array`; the
  elements and their order are identical.
- **D79** — a `derives` clause on an `enum` is parsed and ignored, as on every
  other template (D3).
- **D80** — a `class`, `trait` or `object` nested in a **`class`** or **`trait`**, a
  local class inside a block, and the anonymous-class form `new T { … }` are
  rejected. Each captures the enclosing instance, which needs a per-instance
  class. Nesting in an `object` **is** supported.
- **D81** — a named argument is bound in the callee, so a typo in a parameter name
  is a **run-time** `IllegalArgumentException` (`f has no parameter named 'z'`)
  where scalac rejects it at compile time. The same holds for an argument given
  twice and one left unfilled. Every message names the method and the parameter.
- **D82** — an extension is **global and session-wide**, with no import scoping:
  it is visible to every piece of code that runs after its definition. The import
  mechanism arrived with modules, and scoping extensions to it was then
  **considered and declined**, because protoScala hoists an import to its unit
  (D96), so a "scoped" extension would mean per-*unit* — a third behaviour that
  is neither Scala's per-block scoping nor today's session-wide visibility.
- **D83** — an extension on a builtin type mutates that prototype for the whole
  session, so two units cannot define conflicting extensions of the same name on
  the same type; a collision with an existing member of the type is refused, where
  scalac allows the shadowing and simply never reaches the extension.
- **D84** — multiple constructor parameter lists concatenate into one flat list,
  so `new C(1)(2)` and `new C(1, 2)` are the same call and a constructor cannot be
  partially applied. scalac accepts only the curried spelling.
- **D85** — `catch someFunction` is accepted and rewritten to
  `case e => someFunction(e)`, which is what `catch` of a total function means. A
  genuine `PartialFunction` rethrows in Scala and raises `MatchError` here.
- **D86** — `Throwable.getClass` answers the class's simple name as a `String`;
  protoScala has no `Class[_]` values.
- **D87** — the cleanup a `return` inlines is excluded from its own `try`'s
  handler range, which is what makes a throwing cleanup on a `return` path
  propagate correctly. The multi-level variant is not implemented: with two or
  more nested `try` constructs, an inner handler may see an exception raised by an
  outer cleanup during a `return`.
- **D88** — a default value may read a parameter of the **same** parameter list,
  which scalac requires to come from a previous one. The curried spelling works
  here too.
- **D89** — a named argument on a **function value** binds against the names
  written in the function literal, where scalac rejects it because
  `Function2.apply`'s parameters are called `v1` and `v2`.

### Modules and polyglot imports (D90–D96, D105)

Modules and the `import` that reaches them are described in
[chapter 15](15-modules-and-polyglot-interop.md), which ends with its own
departures section; this is the catalogue. The framing to keep in mind is that
Scala has **no file-level modules** to diverge from — it has packages resolved
against a classpath by a build tool — so most of what follows is a choice with
no Scala counterpart rather than a departure from one.

- **D90** — a module's top level runs **when it is imported**, during the
  importing unit's compilation, and not lazily on first member access the way an
  `object`'s body does. Python and JavaScript both run a module at import, and a
  module that exists for its effects would otherwise never run at all. The load
  is keyed by the file's canonical absolute path, so it happens exactly once per
  file per session.
- **D91** — a module **is an `object`**: `util/Shapes.scala` becomes
  `object Shapes`, its classes are `Shapes.Point`, and its name comes from the
  **file** rather than from anything written inside it. There is no `package`
  clause, no classpath and no `package object`. A module may not define an
  `@main`, which is refused (`a module may not define an @main method: Shapes is
  imported, not run`) rather than ignored, because a silently ignored entry point
  in a library file is a trap.
- **D92** — a **wildcard import of a foreign module** (`import py.numpy.*`) is
  refused: a foreign object's attribute names cannot be enumerated, and binding a
  guessed set would fail silently at the first wrong name. Named selectors work,
  and a wildcard over a protoScala module is unaffected.
- **D93** — `given` selectors (`import M.given`, `import M.{given T}`) are parsed
  and **ignored**, as every other given is (D3): there are no type classes to
  resolve. Ignored rather than rejected, so a file written for Scala 3 still
  compiles.
- **D94** — a **foreign module binds no types**: `new`, a type pattern and
  `isInstanceOf` on a class reached through `py.`, `js.`, `st.` or `clj.` are
  unavailable, because a foreign value carries no class description. Its members
  resolve by name at run time, which is what INTEROP's type-mapping table already
  says; the detection is late, and loud.
- **D95** — `--disassemble` **resolves imports**, and therefore runs the top
  level of every module the file imports. A file cannot be compiled without its
  imports, and an import is resolved by loading (D90).
- **D96** — an `import` is **hoisted to its compilation unit** and is visible for
  the whole unit, where Scala scopes it lexically: an import written inside a
  block or a method binds for the whole file and its binding outlives the block.
  Lexical scoping needs a scope-aware name resolver the compiler does not have,
  and D82's extensions have the same shape, so the two are scoped together or not
  at all.

**D105 (Track X) — `import` has two meanings, and one rule tells them apart.**
Phase 6 gave `import` the file-loading meaning above and, without saying so,
*removed* plain Scala's member import: `enum Color ... import Color.*` failed with
`ImportError: no module found for 'Color'`. Both forms exist now. The rule: the
longest dotted prefix of the path that names something **already in scope** wins
and the import reads its members; otherwise the path is a file to load; a family
prefix wins over both. Scala's own resolution has the same shape — a definition in
scope shadows a package of that name — so a file defining `object util` and
writing `import util.Shapes` gets its own object in either language. Two places
this is narrower than Scala: a **`val` cannot be a member-import prefix** (a
wildcard must enumerate the prefix's members and there is no static type to
enumerate, D4), and a member import of an object defined in the **same file**
cannot supply a *parent* type for a class in that file — write the qualified
`extends Holder.Base`. Everything else is Scala's, including an imported name
working as an expression, a pattern, a type and a constructor.

**What did not diverge**, and is worth stating because you will look for it: the
five import forms have Scala's meaning; `as` and `=>` are both accepted as
renames and `*` and `_` are both accepted as wildcards; a selector that names
nothing is an error rather than a silent no-op; and a selector may name a
**type**, which is usable in a `new`, a type test and a constructor pattern,
because an import is resolved when the importing file is compiled.

**Retired by Phase 4.** **D56** — an interpolator other than `s`, `f` or `raw` was
a compile error; it is now lowered to `StringContext(<literals>).<name>(<args>)`
and supplied as an extension method, exactly as in Scala (chapter 10 §10.8).

### The Predef surface (D103–D104)

The one part of this catalogue that was written *because someone else's tests
said so*. Every other entry came from reading the Scala 3 reference and deciding;
these two came from running the Scala 3 compiler's own `tests/run` corpus and
discovering that `assert`, `require`, `assume`, `???` and `App` did not exist at
all — and that no document here said so. They exist now (chapter 11 §11.6), with
Scala's exception types and Scala's exact message texts, verified by running
scalac.

- **D103** — `assert`, `assume` and `require` are **methods, not macros**. In
  Scala they are `inline` in `Predef`, so `-Xdisable-assertions` removes `assert`
  and `assume` from the bytecode; here nothing removes them, and an assertion
  always costs a call and a by-name thunk. (`require` is never elided in Scala
  either, so that half is identical.) A second consequence of having no
  overloading (D31): each is one method with a default message rather than Scala's
  two overloads. The default is a distinguished object and not `null`, so
  `assert(false, null)` still reports `assertion failed: null`, as Scala's does.
- **D104** — **`App` is the entry point**, as in Scala: initialising
  `object Main extends App` runs the program, and an object extending a trait that
  extends `App` counts too. Three restrictions Scala does not have, because a
  script has no class name with which to choose between two entry points: one App
  object per file, not an App object *and* an `@main` in the same file, and none
  in a module (the reason D91 refuses an `@main` there). All three are compile
  errors naming both candidates. `App` is deprecated in Scala 3 in favour of
  `@main`, which works here too and is the better habit.

**What did not diverge:** which exception each raises, and the message text down
to the colon — `assertion failed`, `assertion failed: why`, `requirement failed`,
`requirement failed: why`, `assumption failed`, and `???`'s
`an implementation is missing`. `AssertionError` and `NotImplementedError` extend
`Error`, so a broad `catch case e: Exception` does not swallow them. The message
parameter is by-name.

### The Scala 3 run corpus (Track S, D108–D111)

Measured against the Scala 3 compiler's own `tests/run` corpus. Almost everything
it found was fixed; these are the two that could not be.

- **D108** — an **`override val` constructor parameter is invisible to an
  ancestor that declares the member in its own body**. `class B { val y = 10;
  println(this.y) }` with `class C(override val y: Int) extends B` prints 10
  where scalac prints 20, and settles on 20 once construction finishes. An
  ancestor that declares `y` as a **parameter** does see the override, matching
  scalac. protoScala keeps one attribute slot per member name; scalac gives each
  class a field and overrides the accessor.
- **D109** — a **`type` alias is recorded for the whole file**, not for the
  template that declares it, so two classes in one file cannot each have their
  own `type T`. Everything else about aliases matches Scala.
- **D111** — **top-level `def`s overload by number of parameters.** `def f(x:
  Int)` and `def f(x: Int, y: Int)` in one file both work and `f(1)` picks the
  first, where before Track S the second definition silently replaced the first.
  Arity is the part of a signature that survives erasure, so what a count cannot
  identify is refused with a diagnostic rather than guessed: two alternatives of
  the same arity (`def f(x: Int)` / `def f(x: String)`, which scalac accepts), a
  default parameter value (which scalac also accepts, resolving by type), a
  repeated or by-name parameter, and several parameter lists. A bare overloaded
  `f` as a function value eta-expands the first alternative.
- **D110** — **widening to `Double` happens where the declared type is written**.
  `val d: Double = 42` is `42.0`, and so are a `def` result, every parameter
  form, a class field and an `(e: Double)` ascription. What does not widen is an
  **assignment** to a variable declared earlier (`var v: Double = 1; v = 2` is
  `2`, Scala `2.0`) and a **type argument** (`List[Double](4, 5)` keeps its
  integers). Deliberate: with no type-name scope, remembering which names were
  declared `Double` would widen an unrelated `x` in another method, which is a
  worse answer than the one it fixes.

## 3.3 What is missing

Out of scope by design: implicits and givens (D3), the static type checker
(D4), the JVM and every Java library (D8), macros and inline metaprogramming,
Java reflection.

Delivered in Phase 2, so no longer on this list: classes, objects,
companions, traits with linearization and stackable `super`, case classes and
case objects, tuples, `Option`, the universal `apply` rule, `List`, pattern
matching and for-comprehensions. Delivered in Phase 5: actors, priority bands,
futures and `Try`/`Success`/`Failure`. Delivered in Phase 3: `Vector`,
`Range`, `Map`, `Set`, `Either`, the rest of the `List` surface, string
interpolation and the `String` methods.

Still missing from `Predef` and the `scala.*` surface, and now known rather than
guessed at — this is what the Scala 3 run corpus asked for and did not find, in
the order of how often it asked: `sys` (`sys.exit`, `sys.error`), `identity`,
`locally`, `StringBuilder`, `Symbol`, `Enumeration`,
`scala.util.control.Breaks`, `Int.MaxValue` and the other numeric limits,
`scala.util.Random`, and `Function22`+. There is also no `scala.*` namespace to
import any of them from (D90/D91), so `import scala.annotation.tailrec` does not
compile.

Still missing in the concurrency area, and not scheduled: supervision trees,
`ExecutionContext`, actor timeouts and `Await.result(f, duration)`. Still
missing in the collection area, and not scheduled: `Seq` and `Iterable` as
traits (D65), `collect` (D63), `Ordering` (D62), `Array`, `SortedMap`/
`ListMap` (D58) and regular expressions (D70).

Not implemented yet, with the phase that brings each (see
[ROADMAP.md](../ROADMAP.md)):

| Feature | Phase |
|---|---|
| Exhaustiveness checking for `match` (D4: types are erased), local and anonymous classes and templates nested in a `class` (D80) | later (v0.7+) |
| `package` and `export` clauses. `type` aliases are **no longer** on this list: Track S implements them (D109) | later |
| A `py`, `js` or `clj` provider for the polyglot import prefixes — cross-repository work, not a protoScala change (chapter 15, §15.5) | later (Track Y) |
| A wildcard import of a foreign module (D92), lexical import scoping (D96) | later |

## 3.4 What is new

What protoScala adds is the substrate rather than the syntax, and most of it
is still ahead (see [DESIGN.md](../DESIGN.md)):

- **Persistent structures.** protoCore's collections are immutable and
  structurally shared; `List`, case classes and maps map onto them directly
  (Phases 2–3).
- **Concurrency without a GIL.** Native actors with lock-free mailboxes,
  three priority bands and a cooperative `await` (Phase 5, DESIGN §8).
- **Polyglot interop in memory.** Python, JavaScript, Smalltalk and Clojure
  modules consumed through Unified Module Discovery, without serialisation. The
  boundary is built and exercised; what is missing is a provider on the other
  side (chapter 15, §15.5 and [INTEROP.md](../INTEROP.md)).
- **Instant start-up.** No JVM to warm up: the REPL prompt is the target of a
  cold-start budget measured every release.
