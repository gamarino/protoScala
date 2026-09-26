# 4. Values and Expressions

> **Implementation status.** Everything in this chapter runs today. String
> interpolation (`s"…"`, `f"…"`) and the richer `String` API arrive in
> Phase 3 (chapter 10); this chapter builds strings with `+`.

This chapter covers the building blocks every program is made of: literals,
numbers, operators, strings, equality, `if`, `while` and `Unit`.

## 4.1 Literals

Fixture: [`tests/conformance/tutorial/04-values-literals.scala`](../../tests/conformance/tutorial/04-values-literals.scala)

```scala
@main def run(): Unit =
  println(0xFF.toString + " " + 1_000_000 + " " + 2.5 + " " + 0.0001 + " " + 'x' + " " + true)
```

Prints:

```text
255 1000000 2.5 1.0E-4 x true
```

| Literal | Meaning |
|---|---|
| `42`, `0xFF`, `1_000_000` | integers; hexadecimal and `_` digit separators are allowed |
| `42L` | a `Long` literal — the same integer type in protoScala (D1) |
| `2.5`, `1e-4`, `0.0001` | `Double` |
| `2.5f` | a `Float` literal — a `Double` in protoScala (D2) |
| `'x'` | a `Char` |
| `"text"`, `"""raw, multi-line"""` | `String`; the usual `\n`, `\t`, `\"` escapes in `"…"` |
| `true`, `false` | `Boolean` |
| `()` | the `Unit` value (§4.7) |

Doubles print the way the JVM prints them: `2.5`, `100.0`, `1.0E-4`, `1.0E7`,
`Infinity`.

## 4.2 Integers and doubles

`Int`, `Long` and `BigInt` are one integer type that grows as needed — no
overflow, no wrap-around (D1). Arithmetic mixing an integer and a `Double`
produces a `Double`.

- Integer `/` truncates toward zero and `%` takes the sign of the dividend, as
  on the JVM: `-7 / 2` is `-3`, `-7 % 2` is `-1`.
- Integer division or remainder by zero raises `ArithmeticException: / by zero`.
- `7.0 / 2` and `7 / 2.0` are both `3.5`; `1.0 / 0` is `Infinity`.
- A `Char` in arithmetic is its code point: `'a' + 1` is `98`.

## 4.3 Operators are methods

Fixture: [`tests/conformance/tutorial/04-values-operators-are-methods.scala`](../../tests/conformance/tutorial/04-values-operators-are-methods.scala)

```scala
@main def run(): Unit =
  println((1 + 2).toString + " " + 1.+(2) + " " + (3 max 5))
```

Prints:

```text
3 3 5
```

`1 + 2` is shorthand for calling the method `+` on `1` with argument `2`, and
`1.+(2)` writes that call out. The reverse also holds: any method with one
argument can be written infix, so `3 max 5` is `3.max(5)`.

Precedence depends on the first character of the operator, lowest to highest:
letters (`max`, `min`); `|`; `^`; `&`; `=` and `!`; `<` and `>`; `:`; `+` and
`-`; `*`, `/` and `%`; any other special character. So `1 + 2 * 3` is `7` and
`a < b && c < d` needs no parentheses. Operators ending in `:` are
right-associative.

An expression may be continued on the next line by starting that line with the
operator, which is how a long arithmetic or string expression is usually laid
out:

```scala
val total = 1
  + 2
  + 3
```

One thing to watch: a **blank line** ends the expression. Insert one before the
`+ 2` and `total` is `1`, with `+ 2` becoming a statement of its own -- the same
as in Scala 3, and the reason a stray blank line inside a long expression
changes the answer rather than failing. A comment on its own line is harmless:
only whitespace makes a line blank.

## 4.4 Strings and characters

Fixture: [`tests/conformance/tutorial/04-values-strings.scala`](../../tests/conformance/tutorial/04-values-strings.scala)

```scala
@main def run(): Unit =
  val s = "hello"
  println(s.toUpperCase + " " + s.length + " " + s.substring(1, 4) + " " + s + "!" * 3 + " " + s.startsWith("he"))
```

Prints:

```text
HELLO 5 ell hello!!! true
```

Strings are immutable. The methods available today include `length`,
`charAt`, `substring`, `indexOf`, `toUpperCase`, `toLowerCase`, `startsWith`,
`endsWith`, `contains`, `trim` and `*` (repetition). `+` concatenates a string
with any value, calling its `toString`. Note that `s + "!" * 3` is
`s + ("!" * 3)`: `*` binds tighter than `+`.

Lengths and indices count Unicode code points, and case mapping covers ASCII
letters only — both provisional (chapter 3, §3.2).

## 4.5 Equality

Fixture: [`tests/conformance/tutorial/04-values-equality.scala`](../../tests/conformance/tutorial/04-values-equality.scala)

```scala
@main def run(): Unit =
  val a = "pro" + "to"
  println((a == "proto").toString + " " + (1 == 1.0) + " " + (1 == 2) + " " + (a != "Scala"))
```

Prints:

```text
true true false true
```

`==` compares values, not identities: two strings with the same characters are
equal however they were built, unlike Java's `==` on strings. Numeric equality
is cooperative across types, so `1 == 1.0` is `true`, as in Scala. `!=` is its
negation.

## 4.6 `if` and `while`

Fixture: [`tests/conformance/tutorial/04-values-if-and-while.scala`](../../tests/conformance/tutorial/04-values-if-and-while.scala)

```scala
def label(n: Int): String =
  if n % 3 == 0 then "fizz"
  else if n % 5 == 0 then "buzz"
  else n.toString

@main def run(): Unit =
  var out = ""
  var i = 1
  while i <= 5 do
    out = out + (if i == 1 then "" else " ") + label(i)
    i += 1
  println(out)
```

Prints:

```text
1 2 fizz 4 buzz
```

`if` is an expression (chapter 2, §2.3); `else if` chains need no special
syntax. An `if` without `else` has type `Unit` and always yields `()`: when
the condition is true the branch runs for its effect and its value is
discarded (§4.7). The condition must be a `Boolean`: `if 1 then …` is a
`ClassCastException` at run time (D4).

`while cond do body` repeats while `cond` is true; with braces it is written
`while (cond) { body }`. `while` is a statement: its value is `()`. Both forms
also accept the parenthesised Scala 2 style, `if (c) a else b`.

## 4.7 `Unit` and `()`

Fixture: [`tests/conformance/tutorial/04-values-unit.scala`](../../tests/conformance/tutorial/04-values-unit.scala)

```scala
def log(msg: String): Unit =
  println(msg)
  msg.length

@main def run(): Unit =
  val result = log("side effect")
  val maybe = if result == () then 42
  println(result.toString + " " + maybe)
```

Prints:

```text
side effect
() ()
```

`Unit` is the type of expressions evaluated only for their effect; its only
value is `()` — Scala's equivalent of Python's `None` returned from a
procedure, or JavaScript's `undefined`. `println` returns `()`.

*Value discarding.* When the expected type of an expression is `Unit`, Scala
evaluates the expression and throws its value away. `log` is declared
`: Unit`, so the value of its last expression, `msg.length`, is discarded and
`log` returns `()`. The same holds for `return e` inside such a method, for
the innermost body of a method with several parameter lists declared `: Unit`,
for `val v: Unit = e`, and for an ascription `(e: Unit)`. An `if` without
`else` has type `Unit` as well, so `maybe` is `()` even though the condition
is true and the branch computed `42`.

One consequence of erased types (D4, D26): protoScala does not type-check, so
it discards values only where the `Unit` is written on the definition itself.
When the expected type comes from a function type — `val f: Int => Unit =
x => x + 1`, or a lambda passed to a parameter of type `Int => Unit` — the
lambda returns its last value (`f(1)` is `2`, where Scala gives `()`).
Programs that call such functions only for their effect see no difference.
