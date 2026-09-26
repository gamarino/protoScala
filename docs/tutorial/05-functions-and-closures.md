# 5. Functions and Closures

> **Implementation status.** Everything in this chapter runs today, except
> the forms listed here. Named and default arguments work (§5.10), bound in the
> callee, with the departures D81, D88 and D89 record.
> By-name parameters (`x: => T`) work (§5.9), with the one limit D53 records.
> Since Phase 2, `List` carries
> `map`, `flatMap`, `filter`, `withFilter`, `foreach`, `head`, `tail`, `drop`,
> `length`/`size`, `isEmpty`, `nonEmpty`, `apply`, `mkString` and `::`
> (chapter 9); the rest of the collection API, and the other collection types,
> arrive in Phase 3 (chapter 8). A varargs parameter is a `List`, so it
> supports all of those.

Functions are the centre of Scala. This chapter covers methods (`def`),
function values (lambdas), closures, recursion and the two ways of deferring a
computation: `lazy val` and parameterless `def` — and a third, the by-name
parameter (§5.9).

## 5.1 `def`

Fixture: [`tests/conformance/tutorial/05-functions-def.scala`](../../tests/conformance/tutorial/05-functions-def.scala)

```scala
def add(a: Int, b: Int): Int = a + b
def greet(name: String): String = "hello, " + name
@main def run(): Unit =
  println(add(3, 4).toString + " " + greet("Ada"))
```

Prints:

```text
7 hello, Ada
```

`def name(params): ResultType = body` defines a method. The body is one
expression — or an indented or braced block, whose value is its last
expression. There is no `return` in idiomatic Scala; `return` is supported
inside a `def` but not inside a lambda (chapter 3, §3.2).

The result type is optional (`def add(a: Int, b: Int) = a + b` works) and, like
every type, it is erased (D4). Methods may be defined in any order: top-level
definitions are visible from the whole file, so `@main` can call a method
defined below it.

## 5.2 Multiple parameter lists and partial application

Fixture: [`tests/conformance/tutorial/05-functions-currying.scala`](../../tests/conformance/tutorial/05-functions-currying.scala)

```scala
def multiply(a: Int)(b: Int): Int = a * b
@main def run(): Unit =
  val triple = multiply(3)
  println(multiply(3)(5).toString + " " + triple(5))
```

Prints:

```text
15 15
```

A method can take several parameter lists. `multiply(3)(5)` supplies both;
`multiply(3)` supplies the first and yields a function waiting for the second
— Scala 3 eta-expansion, with no trailing `_` needed. `triple(5)` then calls
it.

## 5.3 Varargs and splices

Fixture: [`tests/conformance/tutorial/05-functions-varargs.scala`](../../tests/conformance/tutorial/05-functions-varargs.scala)

```scala
def sum(xs: Int*): Int =
  var total = 0
  xs.foreach(x => total += x)
  total
def sumAll(xs: Int*): Int = sum(xs*)
@main def run(): Unit =
  println(sum(1, 2, 3, 4).toString + " " + sum() + " " + sumAll(1, 2, 3))
```

Prints:

```text
10 0 6
```

`xs: Int*` accepts any number of arguments, including none. Inside the method
`xs` is a sequence (a `List` in protoScala — provisional, chapter 3); here
`xs.foreach(x => total += x)` visits each element. `sum(xs*)` passes an
existing sequence on as separate arguments, the equivalent of Python's
`sum(*xs)` or JavaScript's `sum(...xs)`.

## 5.4 Lambdas and higher-order functions

Fixture: [`tests/conformance/tutorial/05-functions-higher-order.scala`](../../tests/conformance/tutorial/05-functions-higher-order.scala)

```scala
def compose(f: Int => Int, g: Int => Int): Int => Int = x => f(g(x))
@main def run(): Unit =
  val square = (x: Int) => x * x
  val inc = (x: Int) => x + 1
  println(compose(square, inc)(3).toString + " " + compose(inc, square)(inc(2)))
```

Prints:

```text
16 10
```

`(x: Int) => x * x` is a function value. When the expected type already says
what the parameter is, the annotation can be left out, as in
`x => f(g(x))` above. A function that takes or returns functions is a
*higher-order* function: `compose` does both. `compose(square, inc)(3)` is
`square(inc(3))` = 16; `compose(inc, square)(inc(2))` is `inc(square(3))` = 10.

A function value is called with `f(x)`, which is shorthand for `f.apply(x)`.

## 5.5 Closures

Fixture: [`tests/conformance/tutorial/05-functions-closures.scala`](../../tests/conformance/tutorial/05-functions-closures.scala)

```scala
@main def run(): Unit =
  var x = 1
  val readX = () => x
  x = 5
  var calls = 0
  val record = () => calls += 1
  record()
  record()
  println(readX().toString + " " + calls)
```

Prints:

```text
5 2
```

A lambda *captures* the variables it mentions, not their values:

- `readX` sees the later assignment `x = 5`, because it reads the shared `x`
  when it is called.
- `record` updates `calls` in the enclosing scope; after two calls `calls`
  is `2`.

Captured `var`s are shared between the closure and the scope that declared
them, as in JavaScript. A `val` declared inside a loop body is a *fresh*
binding on every iteration, so closures created in different iterations see
different values — the behaviour JavaScript gives `let` in a `for` loop, and
the opposite of the classic Python late-binding surprise. The fixture
[`tests/conformance/05-functions/closures-per-activation.scala`](../../tests/conformance/05-functions/closures-per-activation.scala)
shows it: two closures created in two iterations of a `while` loop return `0`
and `1`.

## 5.6 Local and mutually recursive functions

Fixture: [`tests/conformance/tutorial/05-functions-local-recursion.scala`](../../tests/conformance/tutorial/05-functions-local-recursion.scala)

```scala
def factorial(n: Int): BigInt =
  def loop(i: Int, acc: BigInt): BigInt =
    if i > n then acc else loop(i + 1, acc * i)
  loop(1, 1)

def isEven(n: Int): Boolean =
  def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)
  def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)
  even(n)

@main def run(): Unit =
  println(factorial(10).toString + " " + isEven(1000))
```

Prints:

```text
3628800 true
```

A `def` inside another `def` is local to it and can see the enclosing
parameters (`loop` reads `n`). Local definitions may call each other in either
order, so `even` and `odd` are mutually recursive. As in Scala, a reference
may not reach forward past a strict `val` or `var` of the same block: in
`def f = y; val y = 1` the use of `y` is reported as `forward reference to
value y extends over the definition of value y`. Forward references to local
`def`s and `lazy val`s are fine when no strict `val` or `var` lies between. Integers never overflow
(D1), so `factorial` works for any `n`; `BigInt` as the result type is only an
annotation.

Recursion uses the native stack. Tens of thousands of nested calls are fine
(the fixture `tests/conformance/06-recursion/deep-recursion-ok.scala` recurses
10,000 deep); recursion that goes deeper than the stack allows raises

```text
StackOverflowError: calls nested too deeply for the 32 MiB thread stack (use a while loop for deep iteration)
```

instead of crashing. protoScala does not yet eliminate tail calls. Source
code nested deeper than the stack allows (tens of thousands of nested
parentheses or braces) is reported the same way, as
`StackOverflowError: source nested too deeply for the 32 MiB thread stack`.

## 5.7 `lazy val`

Fixture: [`tests/conformance/tutorial/05-functions-lazy-val.scala`](../../tests/conformance/tutorial/05-functions-lazy-val.scala)

```scala
@main def run(): Unit =
  var log = "start"
  lazy val expensive = { log = log + " computed"; 42 }
  val a = expensive
  val b = expensive
  println(log + " " + a + " " + b)
```

Prints:

```text
start computed 42 42
```

A `lazy val` is computed the first time it is read, and only once. Declaring
`expensive` does not run its block; the first read does (appending
` computed` to `log`), and the second read reuses the value.

## 5.8 Parameterless `def`

Fixture: [`tests/conformance/tutorial/05-functions-parameterless.scala`](../../tests/conformance/tutorial/05-functions-parameterless.scala)

```scala
@main def run(): Unit =
  var n = 0
  def next = { n += 1; n }
  val first = next
  println(first.toString + " " + next)
```

Prints:

```text
1 2
```

A `def` without a parameter list is re-evaluated on every reference — the
opposite of a `lazy val`. Each mention of `next` runs its block again, so
`first` is `1` and the second `next` is `2`. Use a `val` for a value, a
`lazy val` for a value computed on demand, and a parameterless `def` for a
computation that should run each time.

## 5.9 By-name parameters

Fixture: [`tests/conformance/tutorial/05-functions-by-name.scala`](../../tests/conformance/tutorial/05-functions-by-name.scala)

```scala
@main def run(): Unit =
  def unless(cond: Boolean)(body: => Int): Int = if cond then 0 else body
  println(unless(true)({ println("never printed"); 1 }))
  var n = 0
  def twice(body: => Int): Int = body + body
  println(twice({ n = n + 1; n }).toString + " " + n)
```

Prints:

```text
0
3 2
```

A parameter written `x: => T` is **by name**: the caller does not evaluate the
argument. The compiler wraps it in a thunk and the body runs that thunk on every
read of the name. So `unless(true)(…)` never prints "never printed", and `twice`
evaluates its argument twice — `1 + 2`, leaving `n` at `2`. It is how `assert`,
`withResource` and every "run this only if…" helper is written in Scala, and how
`Future(expr)` (chapter 13) keeps its body off the calling thread. protoScala's
own `assert` is written exactly this way, which is why
`assert(ok, expensiveReport())` costs nothing while `ok` holds (chapter 11
§11.6).

There is a limit worth knowing, because it is a real departure (**D53**). Scala
reads the `=>` off the callee's static type; protoScala erases types and
dispatches dynamically, so it can only honour the marker where it can *name* the
callee at the call site: a call by name to a top-level or local `def` (any
parameter list), a method of the class being compiled, a method of an `object`, a
class's primary constructor, and the runtime's own by-name signatures. A method
reached through an arbitrary receiver, or a `def` stored in a `val` and called
through it, evaluates the argument once at the call instead. The arithmetic is
still right; what is lost is the laziness.

Two forms are rejected, exactly as scalac rejects them: a by-name parameter on a
function literal, and a `val`, `var` or case-class constructor parameter.

## 5.10 Named and default arguments

A parameter may carry a default value, and a call site may name the parameter it
is filling.

Fixture: [`tests/conformance/tutorial/05-functions-named-arguments.scala`](../../tests/conformance/tutorial/05-functions-named-arguments.scala)

```scala
def volume(width: Int, height: Int = 1, depth: Int = 1): Int = width * height * depth
var log = ""
def side(tag: String, v: Int): Int =
  log = log + tag + " "
  v
@main def run(): Unit =
  val reordered = volume(depth = 3, width = 1)
  val order = volume(height = side("b", 1), width = side("a", 3))
  println(volume(3).toString + " " + volume(1, 13) + " " + reordered + " | " + log.trim)
```

Prints:

```text
3 13 3 | b a
```

Three things to read off that:

- a parameter with a default may be **omitted**, and the callee fills it;
- a named argument may appear in **any order**, and names and positions may be
  mixed as long as every positional argument comes first;
- the arguments are still **evaluated at the call site in source order**, which is
  why the log says `b a` and not `a b`. Naming reorders the *binding*, never the
  evaluation. That is Scala's rule.

A default may read the parameters declared before it, and any enclosing local:

Fixture: [`tests/conformance/tutorial/05-functions-default-reads-an-earlier-parameter.scala`](../../tests/conformance/tutorial/05-functions-default-reads-an-earlier-parameter.scala)

```scala
def span(from: Int, to: Int = from + 1): String = from.toString + " " + to
@main def run(): Unit = println(span(1) + " | " + span(5))
```

```text
1 2 | 5 6
```

Named arguments work on a `def`, a method, a constructor, a case class's `apply`
and `copy`, a function value and a local function — and, once UMD lands, on a
foreign callable, because they travel in protoCore's own calling convention with
no adapter at the boundary (`docs/INTEROP.md` §7).

**Where this departs from Scala.** The binding happens in the **callee**, not at
the call site. A protoCore runtime may resolve at run time what its source
language resolves at compile time, and doing so is what makes a named argument
work even where the compiler cannot tell which callee a call reaches. The price is
the moment the mistake is reported:

Fixture: [`tests/conformance/tutorial/05-functions-named-argument-errors.scala`](../../tests/conformance/tutorial/05-functions-named-argument-errors.scala)

```scala
def area(width: Int, height: Int): Int = width * height
@main def run(): Unit = println(area(width = 2, depth = 3))
```

```text
05-functions-named-argument-errors.scala:6: error: IllegalArgumentException: area has no parameter named 'depth'
```

scalac rejects that at compile time (**D81**). Here it is a run-time error — but a
**loud** one that names the method and the parameter, and the same is true of an
argument given twice (`area received parameter 'width' twice`) and one left
unfilled (`area is missing argument 'height'`). Late detection is accepted on this
platform; a silently dropped argument would not be.

Two smaller departures in the same area. A default may read a parameter of the
**same** list, which scalac requires to come from a previous list (**D88**) — the
curried spelling `def f(a: Int)(b: Int = a + 1)` works here too. And a named
argument on a **function value** binds against the names written in the function
literal, where scalac rejects it because `Function2.apply`'s parameters are called
`v1` and `v2` (**D89**). Both accept strictly more programs than Scala does.

A default value is not supported on a method that also takes a repeated
parameter: the callee could not tell an omitted default from an empty repeated
argument, so it is a compile error rather than a guess.

## 5.11 Overloading a top-level `def`

Fixture: [`tests/conformance/tutorial/05-top-level-overloads.scala`](../../tests/conformance/tutorial/05-top-level-overloads.scala)

```scala
def f(): String = "zero"
def f(x: Int): String = "one:" + x
def f(x: Int, y: Int): String = "two:" + (x + y)

@main def run(): Unit =
  println(f() + " " + f(1) + " " + f(1, 2))
```

Prints:

```text
zero one:1 two:3
```

Several `def`s at the top level of a file may share a name, provided they take
**different numbers of parameters**. The number of arguments at the call site
picks the one to run.

Scala chooses between overloads by the parameter *types*; protoScala has none, so
it uses the count, which is the part of a signature that erasure leaves behind.
That has consequences worth knowing, all of them reported as errors rather than
guessed at:

- two alternatives with the same number of parameters — `def f(x: Int)` and
  `def f(x: String)` — cannot be told apart, and Scala accepts them where
  protoScala does not;
- an overloaded alternative may not have a **default** parameter value, a
  **repeated** parameter, a **by-name** parameter, or more than one parameter
  list, because each of those turns its argument count into a range;
- a call that matches no alternative says so, and lists the arities available;
- a bare `f` used as a *value* eta-expands the **first** alternative, since
  nothing at that point says which one is meant.

Methods inside a class, trait or object still cannot be overloaded at all
(D31); this is D111 and applies to the top level of a file.
