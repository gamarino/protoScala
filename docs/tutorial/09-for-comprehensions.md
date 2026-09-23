# 9. For-Comprehensions

> **Implementation status.** Everything in this chapter runs today. A `for`
> works over anything that has the right methods — `List`, `Option` and your
> own classes — because it is rewritten into calls to `map`, `flatMap`,
> `withFilter` and `foreach` (§9.3). What is missing is *sources* to iterate:
> `Range` (`1 to 10`, `0 until n`), `Vector`, `Map` and `Set` arrive with the
> collections in Phase 3 (chapter 8). Phase 2's `List` carries `map`,
> `flatMap`, `filter`, `withFilter`, `foreach`, `head`, `tail`, `drop`,
> `length`/`size`, `isEmpty`, `nonEmpty`, `apply`, `mkString` and `::`.

A for-comprehension looks like a loop and is not one. It is an expression
built out of method calls, and that is why the same three lines work over a
list, over an `Option`, and over a type you wrote yourself.

## 9.1 `for … yield`

Fixture: [`tests/conformance/tutorial/09-for-yield.scala`](../../tests/conformance/tutorial/09-for-yield.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val doubled = for x <- xs yield x * 2
  val products =
    for
      x <- List(1, 2)
      y <- List(10, 20)
      if x + y != 21
    yield x * y
  println(doubled.toString + " " + products)
```

Prints:

```text
List(2, 4, 6) List(10, 20, 40)
```

`for … yield` **produces a value**: a new collection of the same kind as the
one it started from. `x <- xs` is a *generator*, read "x drawn from xs".

Two syntaxes, both standard Scala 3 and both implemented:

- **One line**: `for x <- xs yield x * 2`.
- **Indented**: `for`, then one generator, guard or value definition per line,
  then `yield`. Generators after the first iterate *inside* the ones before
  them, so `products` pairs each `x` with each `y` — four combinations, of
  which the guard `if x + y != 21` drops `(1, 20)`, leaving `10`, `20`, `40`.

The parenthesised forms Scala 2 used also work:
`for (x <- xs) yield x * 2` and `for (x <- xs; y <- ys if p) yield …`, with
semicolons between the parts. Braces (`for { … } yield …`) are accepted too.

## 9.2 `for … do`

Fixture: [`tests/conformance/tutorial/09-for-do.scala`](../../tests/conformance/tutorial/09-for-do.scala)

```scala
@main def run(): Unit =
  var out = ""
  for x <- List(1, 2); y <- List("a", "b") do
    out = out + (if out.isEmpty then "" else " ") + x + y
  println(out)
```

Prints:

```text
1a 1b 2a 2b
```

Drop `yield` and write `do`: the comprehension runs its body for its effects
and produces `()`. This is the form that really is a loop, and the order it
visits is the obvious one — the last generator moves fastest.

The Scala 2 form `for (x <- xs) println(x)`, with no `do` and no `yield`,
works as well.

## 9.3 What a for-comprehension means

Fixture: [`tests/conformance/tutorial/09-for-desugared.scala`](../../tests/conformance/tutorial/09-for-desugared.scala)

```scala
@main def run(): Unit =
  val a = for (x <- List(1, 2); y <- List(10, 20) if x + y != 21) yield x * y
  val b = List(1, 2).flatMap(x => List(10, 20).withFilter(y => x + y != 21).map(y => x * y))
  println(a == b)
```

Prints:

```text
true
```

`a` and `b` are the same program. The compiler rewrites every `for` before it
ever reaches the runtime, by these four rules (DESIGN §3.4):

| Written | Becomes |
|---|---|
| `for (x <- e) yield b` | `e.map(x => b)` |
| `for (x <- e; rest…) yield b` | `e.flatMap(x => for (rest…) yield b)` |
| `for (x <- e if g; rest…)` | `e.withFilter(x => g)` then the rule above |
| `for (x <- e) do b` | `e.foreach(x => b)` |

So a `for` is not a language construct with its own semantics: it is sugar
over four method names. Anything that defines them can be iterated — which is
the point of §9.5.

Two consequences worth knowing:

- **Guards are lazy.** `withFilter` does not build an intermediate collection;
  it defers the test so that the guard and the body are interleaved, element
  by element. A `for (x <- List(1, 2, 3) if …) …` whose guard and body both
  record what they saw logs `f1 x1 f2 x2 f3 x3`, not `f1 f2 f3 x1 x2 x3` — the
  same order as Scala (fixture
  [`12-for-comprehensions/guards-are-lazy.scala`](../../tests/conformance/12-for-comprehensions/guards-are-lazy.scala)).
- **`--disassemble` shows the rewrite.** If a comprehension does something
  unexpected, `protoscala --disassemble file.scala` prints the `flatMap`,
  `withFilter` and `map` sends it turned into (chapter 14, §14.7).

## 9.4 Patterns and value definitions

Fixture: [`tests/conformance/tutorial/09-for-patterns.scala`](../../tests/conformance/tutorial/09-for-patterns.scala)

```scala
@main def run(): Unit =
  val pairs = List(("a", 1), ("b", 2))
  val joined = for ((s, n) <- pairs) yield s + n
  val present = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  val big = for (x <- List(1, 2, 3); y = x * 2 if y > 2) yield y
  println(joined.toString + " " + present + " " + big)
```

Prints:

```text
List(a1, b2) List(1, 3) List(4, 6)
```

Three features, one line each:

- **A pattern on the left of `<-`.** `(s, n) <- pairs` destructures every
  element, exactly like the patterns of chapter 7. An *irrefutable* pattern —
  a tuple, a variable, a single-case class — always matches, so nothing is
  dropped.
- **`case` in front of a pattern.** `case Some(v) <- xs` says the pattern may
  fail, and elements that do not match are **filtered out** rather than
  raising a `MatchError`. Three elements go in, two come out.
- **A value definition.** `y = x * 2` binds an intermediate inside the
  comprehension, and a guard after it can use it. It is not a generator: there
  is no `<-`, and it does not iterate.

> protoScala also accepts a refutable pattern **without** the `case` keyword —
> `for (Some(v) <- xs)` — and filters, which is what Scala 2 did; Scala 3.9
> requires `case` there. Conversely, writing `case` in front of an irrefutable
> pattern costs nothing here, because no filter is emitted for a pattern that
> cannot fail. Both are recorded as deviations in
> [chapter 3](03-for-the-scala-developer.md) and STATUS.md.

## 9.5 Beyond lists

Fixture: [`tests/conformance/tutorial/09-for-option.scala`](../../tests/conformance/tutorial/09-for-option.scala)

```scala
def parse(s: String): Option[Int] = if s == "1" then Some(1) else if s == "2" then Some(2) else None

@main def run(): Unit =
  val ok = for (a <- parse("1"); b <- parse("2")) yield a + b
  val bad = for (a <- parse("1"); b <- parse("x")) yield a + b
  println(ok.toString + " " + bad)
```

Prints:

```text
Some(3) None
```

`Option` has `map`, `flatMap` and `withFilter`, so it works in a `for`. Read
the comprehension as "if all of these succeed, then": `bad` short-circuits at
the first `None` and never evaluates `a + b`. That is the whole of error
handling in an `Option`-based API, without a single `if o.isDefined`.

The same holds for your own types. A class with `map` and `flatMap` can be
used in a `for` with no further ceremony:

Fixture: [`tests/conformance/12-for-comprehensions/over-user-class.scala`](../../tests/conformance/12-for-comprehensions/over-user-class.scala)

```scala
case class Box(v: Int):
  def map(f: Int => Int): Box = Box(f(v))
  def flatMap(f: Int => Box): Box = f(v)

@main def run(): Unit =
  println(for (a <- Box(3); b <- Box(4)) yield a * b)
```

Prints:

```text
Box(12)
```

Nothing about `Box` is special: `for` asked for `flatMap` and `map`, and it
found them.

The first generator decides what the comprehension produces. Starting from a
`List` and drawing from an `Option` inside it is idiomatic and gives a `List`
with the empty cases dropped — `for (x <- List(1, 2, 3); y <- halfOf(x))
yield y` — because `List.flatMap` accepts a function returning an `Option`,
exactly as on the JVM (fixture
[`12-for-comprehensions/list-and-option.scala`](../../tests/conformance/12-for-comprehensions/list-and-option.scala)).

The other way round is where protoScala is more permissive than Scala:
`for (x <- Some(2); y <- List(1, 2)) yield x * y` is a type error on the JVM,
and here it quietly returns `List(2, 4)`, because `Option.flatMap` simply
returns what the function gave it and nothing checks the type (D4). Do not
rely on it; write `o.toList` first if you mean it.

## 9.6 Placeholders

Fixture: [`tests/conformance/tutorial/09-for-placeholders.scala`](../../tests/conformance/tutorial/09-for-placeholders.scala)

```scala
@main def run(): Unit =
  val xs = List(1, 2, 3)
  var sum = 0
  xs.foreach(sum += _)
  println(xs.map(_ + 1).toString + " " + xs.filter(_ > 2) + " " + sum)
```

Prints:

```text
List(2, 3, 4) List(3) 6
```

`_` inside an expression that is passed as an argument stands for the
parameter of an anonymous function: `_ + 1` is `x => x + 1`, `_ > 2` is
`x => x > 2`, and `sum += _` is `x => sum += x`. The lambda is created at the
*smallest enclosing expression*, which is why `_ + 1` inside `map(...)` is the
function and not the whole `map` call. Two underscores mean two parameters, in
order: where a two-argument function is expected, `_ + _` is
`(a, b) => a + b`.

Use a placeholder when the parameter is mentioned exactly once and the
expression stays short. `xs.map(x => x * x + 1)` is clearer than trying to
write it with underscores.

## 9.7 Bridges

Fixture: [`tests/conformance/tutorial/02-python-js-comprehension.scala`](../../tests/conformance/tutorial/02-python-js-comprehension.scala)

```scala
@main def run(): Unit =
  val xs = List(0, 1, 2, 3, 4)
  println(for (x <- xs if x % 2 == 0) yield x * x)
```

Prints:

```text
List(0, 4, 16)
```

**For Python readers.** That is `[x * x for x in xs if x % 2 == 0]`, with the
pieces in a different order: Scala puts the source first and the result last.
Nesting maps directly —

| Python | Scala |
|---|---|
| `[x*y for x in a for y in b]` | `for (x <- a; y <- b) yield x*y` |
| `[x for x in a if p(x)]` | `for (x <- a if p(x)) yield x` |
| `[(x, y) for x in a]` | `for (x <- a) yield (x, someY)` |
| `for x in a: body` | `for (x <- a) do body` |

The differences: Scala reads top to bottom in the order the loops nest (Python
reads the *result* first and the loops after), a Scala comprehension can bind
intermediates with `y = …` (Python has the walrus `:=`), and a Scala
comprehension is not limited to sequences — `for (a <- opt1; b <- opt2)` over
`Option` has no Python equivalent.

**For JavaScript readers.** A comprehension is the `flatMap`/`filter`/`map`
chain you already write, with the callbacks removed:

```js
a.flatMap(x => b.filter(y => x + y !== 21).map(y => x * y))
```

is, in Scala,

```scala
for (x <- a; y <- b if x + y != 21) yield x * y
```

— and §9.3 showed that the compiler turns the second into the first. When a
chain has one step, write the chain (`xs.map(_ + 1)`); when it has three and
nests, write the comprehension.
