# 5. Functions and Closures

> **Implementation status.** Everything in this chapter runs today, except
> the forms listed here. Named and default arguments arrive in Phase 4;
> by-name parameters (`x: => T`) are parsed and rejected with
> `by-name parameters are not supported yet`; collection methods such as
> `map` and `filter` arrive with the collections in Phase 3 (chapter 8). A
> varargs parameter supports `length`, `isEmpty`, `head`, `apply(i)`,
> `foreach` and `mkString` today.

Functions are the centre of Scala. This chapter covers methods (`def`),
function values (lambdas), closures, recursion and the two ways of deferring a
computation: `lazy val` and parameterless `def`.

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
