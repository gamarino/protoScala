# 2. For the Python or JavaScript Developer

> **Implementation status.** This chapter uses only what protoScala runs
> today. Classes, objects and traits (chapter 6), case classes and pattern
> matching (chapter 7) and for-comprehensions (chapter 9) now run, and §§2.9
> to 2.11 introduce them. Since Phase 3 the collections — `Vector`, `Map`,
> `Set`, `Range`, `Either`, `Try` (chapter 8) — and string interpolation
> (`s"Hi $name"`, chapter 10) run too, and §2.12 and §2.13 introduce them. See
> [STATUS.md](../STATUS.md) for the exact list.

Scala looks like a cross between the languages you know. From JavaScript it
takes braces, arrow functions and `val`/`var` that feel like `const`/`let`;
from Python it takes significant indentation and integers that never
overflow. What is new is the idea that almost *everything is an expression*
that produces a value, and a type annotation on most definitions. This
chapter walks through those ideas with the programs you would write in Python
or JavaScript first.

Every program below is a file you can run with `protoscala file.scala`; the
block under it is what it prints.

## 2.1 Programs and `@main`

A Python script runs top to bottom, and the idiom
`if __name__ == "__main__": main()` marks the entry point. A Node.js file also
runs top to bottom. A Scala 3 program instead names its entry point with the
`@main` annotation on a method (chapter 1, §1.3):

Fixture: [`tests/conformance/tutorial/01-introduction-hello.scala`](../../tests/conformance/tutorial/01-introduction-hello.scala)

```scala
@main def hello(): Unit =
  println("Hello, protoScala!")
```

Prints:

```text
Hello, protoScala!
```

protoScala also accepts the Python/JavaScript style — statements at the top
level of the file run in order, like a Scala `.sc` script. That is a
protoScala convenience rather than standard Scala 3 (chapter 3, §3.2), so the
rest of this tutorial uses `@main`.

## 2.2 Names: `val` and `var`

Fixture: [`tests/conformance/tutorial/02-python-js-val-var.scala`](../../tests/conformance/tutorial/02-python-js-val-var.scala)

```scala
@main def run(): Unit =
  val name = "Ada"      // like a JS const, or a Python name you never rebind
  var visits = 0        // like a JS let
  visits += 1
  visits += 2
  println(visits.toString + " visits, name=" + name)
```

Prints:

```text
3 visits, name=Ada
```

- `val` is a name you cannot reassign — JavaScript's `const`, or a Python name
  you simply never rebind. Reassigning a `val` is a compile error.
- `var` can be reassigned — JavaScript's `let`. `visits += 1` means
  `visits = visits + 1`, as in both languages.

Idiomatic Scala uses `val` almost everywhere and reaches for `var` only for a
local counter or accumulator. You will see why in chapter 5: closures and
immutable values make most loops unnecessary.

Two small things in the last line: `visits.toString` turns the integer into a
string (like `str(visits)` or `String(visits)`), and `+` between a string and
anything else concatenates, like JavaScript.

## 2.3 Everything is an expression

In JavaScript, `if` is a statement and the ternary `c ? a : b` is its
expression form; Python has `a if c else b`. In Scala there is only one `if`,
and it always produces a value:

Fixture: [`tests/conformance/tutorial/02-python-js-if-expression.scala`](../../tests/conformance/tutorial/02-python-js-if-expression.scala)

```scala
def category(age: Int): String = if age < 18 then "minor" else "adult"
@main def run(): Unit =
  println(category(12) + " " + category(40))
```

Prints:

```text
minor adult
```

`if age < 18 then "minor" else "adult"` *is* the result of `category`; there is
no `return`. A method whose body is one expression is written on one line
after `=`.

Blocks are expressions too. The value of a block is the value of its last
expression — like a Ruby block, or a JavaScript arrow function whose body is
an expression:

Fixture: [`tests/conformance/tutorial/02-python-js-blocks-are-expressions.scala`](../../tests/conformance/tutorial/02-python-js-blocks-are-expressions.scala)

```scala
@main def run(): Unit =
  val area =
    val width = 5
    val height = 5
    width * height
  println(area)
```

Prints:

```text
25
```

`width` and `height` exist only inside the indented block; `area` receives the
value of its last line, `width * height`.

## 2.4 Indentation and braces

Scala 3 accepts both styles, and protoScala implements both. The two methods
below are the same program:

Fixture: [`tests/conformance/tutorial/02-python-js-indentation-and-braces.scala`](../../tests/conformance/tutorial/02-python-js-indentation-and-braces.scala)

```scala
def sumIndented(n: Int): Int =
  var total = 0
  var i = 1
  while i <= n do
    total += i
    i += 1
  total

def sumBraces(n: Int): Int = {
  var total = 0
  var i = 1
  while (i <= n) {
    total += i
    i += 1
  }
  total
}

@main def run(): Unit =
  println(sumIndented(3).toString + " " + sumBraces(3))
```

Prints:

```text
6 6
```

The indentation style is close to Python's, with keywords where Python uses a
colon:

| Python | Scala 3 (indentation) | Scala 3 (braces) |
|---|---|---|
| `def f(n):` + indented body | `def f(n: Int): Int =` + indented body | `def f(n: Int): Int = { ... }` |
| `if c:` | `if c then` | `if (c) { ... }` |
| `while c:` | `while c do` | `while (c) { ... }` |

A region ends when the indentation returns to the enclosing level; an optional
end marker (`end if`, `end while`, `end sumIndented`) makes long regions easier
to read. The brace style is what a JavaScript developer already writes; the
parentheses around the condition are then required, as in JavaScript. You can
mix the two styles in one file — but not tabs and spaces in the same
indentation (chapter 3).

## 2.5 Functions and arrow functions

Fixture: [`tests/conformance/tutorial/02-python-js-arrow-functions.scala`](../../tests/conformance/tutorial/02-python-js-arrow-functions.scala)

```scala
@main def run(): Unit =
  val double = (x: Int) => x * 2          // JS: x => x * 2, Python: lambda x: x * 2
  def applyTwice(f: Int => Int, x: Int): Int = f(f(x))
  println(double(7).toString + " " + applyTwice(double, 3))
```

Prints:

```text
14 12
```

- `(x: Int) => x * 2` is an anonymous function — JavaScript's `x => x * 2`,
  Python's `lambda x: x * 2`. Stored in a `val`, it is called like any
  function: `double(7)`.
- `def applyTwice(f: Int => Int, x: Int)` takes a function as a parameter.
  `Int => Int` is the *type* of a function from `Int` to `Int`; like every
  type, protoScala reads it and does not check it.
- A `def` can be nested inside another `def`, as a Python `def` can be nested
  inside a function.

### Arguments that are not evaluated (by-name parameters)

Fixture: [`tests/conformance/tutorial/02-python-js-by-name.scala`](../../tests/conformance/tutorial/02-python-js-by-name.scala)

```scala
@main def run(): Unit =
  def orElse(v: String, fallback: => String): String = if v != "" then v else fallback
  def expensive(): String =
    println("computing the fallback")
    "expensive"
  println(orElse("cheap", expensive()))
```

Prints:

```text
cheap
```

Note what did *not* happen: "computing the fallback" was never printed, even
though `expensive()` is written as an ordinary argument. The `=>` in
`fallback: => String` makes the parameter **by name** — the argument is not
evaluated at the call, only where the body reads the name, and once per read.

In Python or JavaScript you get this by passing a function and calling it:

```python
def or_else(v, fallback):        # Python
    return v if v else fallback()
or_else("cheap", lambda: expensive())
```

```javascript
const orElse = (v, fallback) => v || fallback();   // JavaScript
orElse("cheap", () => expensive());
```

Scala's by-name parameter is the same mechanism with the `() =>` and the `()`
hidden: the *caller* writes a plain expression, the *callee* writes a plain name.
That is why it looks like an ordinary argument but behaves like a closure, and
why reading a by-name parameter twice runs the argument twice. Chapter 5 (§5.9)
has the rule and the one case where protoScala evaluates eagerly anyway.

## 2.6 Closures

The counter idiom every JavaScript developer has written:

Fixture: [`tests/conformance/tutorial/02-python-js-closure-counter.scala`](../../tests/conformance/tutorial/02-python-js-closure-counter.scala)

```scala
def makeCounter(): () => Int =
  var count = 0
  () =>
    count += 1
    count
@main def run(): Unit =
  val next = makeCounter()
  val a = next()
  val b = next()
  println(a.toString + " " + b + " " + next())
```

Prints:

```text
1 2 3
```

`makeCounter` returns a function (its result type `() => Int` reads "a
function with no arguments returning an `Int`"). The returned function
captures `count`, and every call updates the same variable — exactly like a
JavaScript closure over a `let`, or a Python closure with `nonlocal count`.
Scala needs no `nonlocal`: a captured `var` is always shared.

The lambda body spans two lines: after `() =>` at the end of a line, the
indented lines are its body, and its value is the last one, `count`.

## 2.7 Numbers: integers never overflow

JavaScript has one number type, a 64-bit double, so large integers silently
lose precision. Python has arbitrary-precision integers. protoScala follows
Python:

Fixture: [`tests/conformance/tutorial/02-python-js-big-integers.scala`](../../tests/conformance/tutorial/02-python-js-big-integers.scala)

```scala
// Like Python, integers never overflow (D1). This is 2 to the power 100.
@main def run(): Unit =
  var result: BigInt = 1
  var i = 0
  while i < 100 do
    result = result * 2
    i += 1
  println(result)
```

Prints:

```text
1267650600228229401496703205376
```

Scala on the JVM has fixed-width `Int` (32 bits) and `Long` (64 bits) that
wrap around on overflow, and a separate `BigInt`. In protoScala all three are
one integer that grows as needed (deviation D1); `BigInt` is still accepted as
a type name, so programs written for the JVM read naturally. Decimal numbers
are `Double`, the same IEEE double JavaScript and Python use.

## 2.8 Types as annotations

Fixture: [`tests/conformance/tutorial/02-python-js-type-annotations.scala`](../../tests/conformance/tutorial/02-python-js-type-annotations.scala)

```scala
// Types are annotations, like TypeScript or Python type hints; they are not checked (D4).
def add(a: Int, b: Int): Int = a + b
@main def run(): Unit =
  val total: Int = add(2, 3)
  println(total)
```

Prints:

```text
5
```

`a: Int` and `: Int` after the parameter list look like TypeScript, or Python
type hints. In Scala on the JVM they are checked by the compiler before the
program runs. protoScala has no static checker: annotations are parsed and
erased, like Python type hints at run time (deviation D4). A mistake the Scala
compiler would reject is reported when the offending line runs instead —
chapter 3 shows one. Most local `val`s need no annotation at all; `val total =
add(2, 3)` is just as good.

## 2.9 Classes and traits

Fixture: [`tests/conformance/tutorial/02-python-js-classes.scala`](../../tests/conformance/tutorial/02-python-js-classes.scala)

```scala
trait Animal:
  def name: String
  def sound: String
  def speak = name + " says " + sound

class Dog(val name: String) extends Animal:
  def sound = "woof"

class Cat(val name: String) extends Animal:
  def sound = "meow"

@main def run(): Unit =
  println(new Dog("Rex").speak + "; " + new Cat("Tom").speak)
```

Prints:

```text
Rex says woof; Tom says meow
```

Two things are compressed here that Python and JavaScript spell out.

`class Dog(val name: String)` is the constructor **and** the field
declaration: no `def __init__(self, name): self.name = name`, no
`constructor(name) { this.name = name }`. The `val` in front of the parameter
is what makes it a public field.

`trait Animal` is an interface that may also contain code. `name` and `sound`
are declared without a body — every animal must supply them — while `speak`
has one and calls both. Python gets this with an abstract base class or a
mixin, JavaScript with a mixin function over `prototype`; Scala has one
keyword for it. A class can extend one class and any number of traits.

Chapter 6 covers the rest: singletons (`object`), companions, `apply`,
mutable fields, privacy and what happens when two traits define the same
method.

## 2.10 Pattern matching

Fixture: [`tests/conformance/tutorial/02-python-js-match.scala`](../../tests/conformance/tutorial/02-python-js-match.scala)

```scala
case class Circle(r: Double)
case class Rect(w: Double, h: Double)

def describe(shape: Any): String = shape match
  case Circle(r) => "circle:" + r
  case Rect(w, h) => "rect:" + w + "x" + h
  case _ => "unknown"

@main def run(): Unit =
  println(describe(Circle(3.0)) + " " + describe(Rect(2.0, 5.0)) + " " + describe(42))
```

Prints:

```text
circle:3.0 rect:2.0x5.0 unknown
```

A `case class` is a data shape. You get the constructor without `new`
(`Circle(3.0)`), a readable `toString`, and — unlike any JavaScript object and
unlike an ordinary Python class — `==` that compares the *contents*. It is
Python's `@dataclass(frozen=True)`, in one word.

`match` then takes the shape apart. `case Rect(w, h)` checks the kind and
binds both fields in one line. Python 3.10's `match`/`case` was modelled on
this; JavaScript has no equivalent and you write
`if (s instanceof Rect) { const {w, h} = s; … }`.

Two rules to carry away: a `match` is an **expression**, so it *is* the body
of `describe`, and only the first matching case runs — there is no
fall-through and no `break`. Chapter 7 has every pattern form.

## 2.11 Comprehensions

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

This is Python's `[x * x for x in xs if x % 2 == 0]`, with the source first
and the result last, and it is JavaScript's
`xs.filter(x => x % 2 === 0).map(x => x * x)` — literally, because the
compiler rewrites the `for` into that chain (chapter 9, §9.3). `yield` is not
Python's generator `yield`: here it simply names the expression that produces
each element.

A `for` without `yield` needs `do` and is a plain loop:
`for (x <- xs) do println(x)`. Chapter 9 covers nesting, guards, patterns and
what happens when you iterate over an `Option` instead of a `List`.

## 2.12 Collections: `dict`, `set`, `list`, `range`

Fixture: [`tests/conformance/tutorial/02-python-js-collections.scala`](../../tests/conformance/tutorial/02-python-js-collections.scala)

```scala
@main def run(): Unit =
  val names = Vector("Ada", "Alan")          // Python list / JavaScript Array
  val ages = Map("Ada" -> 36, "Alan" -> 41)  // Python dict / JavaScript Map
  val tags = Set("pioneer", "pioneer")       // Python set / JavaScript Set
  val withGrace = ages + ("Grace" -> 30)     // no ages[k] = v: a new map
  println(names(0) + " " + ages("Alan") + " " + tags.size + " " +
    ages.size + " " + withGrace.size + " " + (for i <- 0 until 3 yield i * i))
```

Prints:

```text
Ada 41 1 2 3 List(0, 1, 4)
```

The names map almost one to one:

| Python | JavaScript | protoScala |
|---|---|---|
| `dict` | `Map` | `Map`, written `Map("a" -> 1)` |
| `set` | `Set` | `Set` |
| `list` | `Array` | `Vector` for indexed access, `List` for a sequence |
| `range(n)` | — | `0 until n` (`1 to n` includes the bound) |
| `d[k]`, `xs[i]` | `m.get(k)`, `xs[i]` | `d(k)`, `xs(i)` — parentheses, not brackets |

Two differences are worth the paragraph each.

**`list` is two types here, and neither is Python's.** A `Vector` is the
indexed container you are used to; a `List` is a sequence you usually build and
walk from the front. Neither can be modified — the difference from Python is
not the shape but that `xs[0] = v`, `append` and `sort()` do not exist. You
write `xs.updated(0, v)`, `xs :+ v` and `xs.sorted`, and each answers a new
collection while the old one stays valid. `Vector` is the name to reach for
when you would have written `list` or `Array`.

**A map is immutable, and that is cheap.** `ages + ("Grace" -> 30)` answers a
*new* map with three entries; `ages` still has two, as the output shows. In
Python that would be `{**ages, "Grace": 30}` and would copy the whole
dictionary. Here the two maps share nearly all of their internal structure, so
the new one costs a path, not a copy. That is why nothing in Scala has to
defend itself against a collection changing underneath it — including two
threads reading the same map at once (chapter 8, §8.9 has the long version).

One habit to change immediately: a Python `dict` preserves insertion order and
a Scala `Map` promises **no** order at all. If the order of your output
matters, sort: `ages.toList.sortBy(_._1)`. Every example in this tutorial does.

Chapter 8 covers all of it, plus `Option` as a collection of at most one, and
`Either`/`Try` for computations that can fail.

## 2.13 Strings: f-strings and template literals

Fixture: [`tests/conformance/tutorial/02-python-js-interpolation.scala`](../../tests/conformance/tutorial/02-python-js-interpolation.scala)

```scala
@main def run(): Unit =
  val name = "Ada"
  val total = 12.5
  println(s"$name spent ${total * 2} in all" + " | " + f"$name spent $total%.2f each")
```

Prints:

```text
Ada spent 25.0 in all | Ada spent 12.50 each
```

`s"…"` is the f-string and the template literal you already write. The letter
in front of the quote names the interpolator, and there are three:

| Python | JavaScript | protoScala |
|---|---|---|
| `f"Hi {name}"` | `` `Hi ${name}` `` | `s"Hi $name"` |
| `f"{a + b}"` | `` `${a + b}` `` | `s"${a + b}"` |
| `f"{x:.2f}"` | `x.toFixed(2)` | `f"$x%.2f"` |
| `f"{n:05d}"` | `String(n).padStart(5, "0")` | `f"$n%05d"` |
| `r"C:\new"` | a `String.raw` template | `raw"C:\new"` |
| `"{}".format(x)` | — | `"%s".format(x)` |

`${expr}` is JavaScript's bracket exactly; `$name` is the short form for a
single name, which Python does not have and which is what you will write most.
`f"…"` adds a `printf` specifier after a hole — `%.2f`, `%05d`, `%,d`,
`%-10s` — so a column of aligned output takes one line. `raw"…"` leaves
backslashes alone, like a Python `r"…"` string.

Chapter 10 has the full specifier table, `stripMargin` for multi-line text, and
the `String` methods.

## 2.14 Concurrency without a GIL

Python has a global interpreter lock, so two threads never run bytecode at the
same time; JavaScript has one event loop per worker and copies anything you
send between workers. protoScala has neither. Threads are real OS threads, two
of them run Scala at the same time on two cores, and a message passed between
them is a pointer to an immutable value — no copy, no pickle, no structured
clone.

You rarely write threads directly. The unit of concurrency is an **actor**: a
state plus a function that handles one message at a time.

```text
val counter = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
counter ! 1                  // tell:  queue a message, return immediately
val total = (counter ? 1).await   // ask: queue a message, get a Future of the reply
```

The runtime handles one message of an actor at a time, however many threads
send to it, so the state inside a handler needs no lock. `await` inside a
handler does not block a worker: the runtime saves the handler's call chain,
runs something else, and resumes it when the answer arrives.

[Chapter 13](13-actors-and-futures.md) covers actors, the three priority
bands, futures and their combinators, what happens when a handler fails, and
`Thread`/`System`.

## 2.15 Errors: `raise`/`except` and `throw`/`catch`

Fixture: [`tests/conformance/tutorial/02-python-js-exceptions.scala`](../../tests/conformance/tutorial/02-python-js-exceptions.scala)

```scala
@main def run(): Unit =
  def parse(s: String): String =
    try
      s.toInt.toString
    catch
      case e: NumberFormatException => "cannot parse '" + s + "'"
    finally
      ()
  var log = ""
  try
    throw new IllegalStateException("x")
  catch
    case e: IllegalStateException => log = "cleaned up"
  println(parse("abc") + " | " + log)
```

```text
cannot parse 'abc' | cleaned up
```

| Python | JavaScript | protoScala |
|---|---|---|
| `raise ValueError("x")` | `throw new Error("x")` | `throw new IllegalArgumentException("x")` |
| `except ValueError as e:` | `catch (e) { if (…) }` | `case e: IllegalArgumentException =>` |
| `finally:` | `finally { }` | `finally` |
| `else:` clause | — | none |

Two things are different in kind, not just in spelling. A `try` **has a value**,
so you write `val n = try … catch …` instead of assigning inside both branches.
And a `catch` clause is a **pattern**, so the test and the extraction of the
exception's fields are one thing — Python needs an `if` inside the `except` body
and a bare `raise` to put the value back when the `if` does not match.
[Chapter 11](11-exceptions.md) is the whole story.

## 2.16 Enumerations, and the thing Python does not have

Fixture: [`tests/conformance/tutorial/02-python-js-enums.scala`](../../tests/conformance/tutorial/02-python-js-enums.scala)

```scala
enum Colour:
  case Red, Green, Blue
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
@main def run(): Unit =
  val described = Tree.Leaf(3) match
    case Tree.Leaf(n)    => "leaf " + n
    case Tree.Node(_, _) => "node"
  println(Colour.Red.toString + " " + Colour.Red.ordinal + " " + Colour.values.length +
    " | " + described)
```

```text
Red 0 3 | leaf 3
```

`Colour` is Python's `Enum` with `ordinal` where Python has `value`, and
`Colour.values` where Python has `list(Colour)`. `Tree` is the other thing: a
closed set of alternatives **that carry different data**, which TypeScript
approximates with a discriminated union and JavaScript has no form for at all.
The `match` tests which case it is and pulls the data out in one step, with no tag
field to maintain. [Chapter 12](12-enums-and-sealed-hierarchies.md) is the whole
story.

## 2.17 Keyword arguments

Fixture: [`tests/conformance/tutorial/02-python-js-keyword-arguments.scala`](../../tests/conformance/tutorial/02-python-js-keyword-arguments.scala)

```scala
def box(width: Int, height: Int, depth: Int = 1): String =
  "w=" + width + " h=" + height + " d=" + depth
@main def run(): Unit = println(box(height = 3, width = 2))
```

```text
w=2 h=3 d=1
```

This is Python's call, character for character, defaults included. In JavaScript
the same thing is an options object — `box({ height: 3, width: 2 })` — with the
destructuring and the defaults written by hand in the function; here the
parameter list *is* the interface, and a name you did not declare is an error
rather than a silently ignored property.

The arguments are evaluated where they are written, in source order, however the
names reorder them. [Chapter 5](05-functions-and-closures.md) §5.10 has the rest,
including where a mistake is reported.

## 2.18 Modules: the second file

A protoScala module is a **`.scala` file**, reached by its path with dots where
a directory separator would be, and its top level **runs when it is imported**,
once — exactly as in both of your languages. Put this in `util/Strings.scala`:

```scala
def shout(s: String): String = s.toUpperCase + "!"
def initials(s: String): String = s.split(" ").map(w => w.substring(0, 1)).mkString(".")
val greeting: String = "hello"
```

and beside it:

Fixture: [`tests/conformance/tutorial/02-python-js-modules-import.scala`](../../tests/conformance/tutorial/02-python-js-modules-import.scala)

```scala
import util.Strings
@main def run(): Unit =
  println(Strings.shout("hello") + " " + Strings.greeting)
```

```text
HELLO! hello
```

There is nothing to export and nothing to declare: whatever you write at the
top level of the file is what an importer can reach. There is also no package
system, no `__init__.py` and no `package.json` — a module is found by looking
for the file, in the importing file's own directory first, then in each
colon-separated entry of `PROTOSCALA_PATH`, then in the working directory.

The forms map almost one to one onto what you already write:

| Python | JavaScript | protoScala |
|---|---|---|
| `import util.strings` | `import * as Strings from "./util/Strings.js"` | `import util.Strings` |
| `import numpy as np` | `import np from "numpy"` | `import util.Strings as S` |
| `from util.strings import shout` | `import { shout } from "./util/Strings.js"` | `import util.Strings.{shout}` |
| `from util.strings import initials as short` | `import { initials as short } from …` | `import util.Strings.{initials as short}` |
| `from util.strings import *` | — | `import util.Strings.*` (or `._`) |

Fixture: [`tests/conformance/tutorial/02-python-js-modules-from-import.scala`](../../tests/conformance/tutorial/02-python-js-modules-from-import.scala)

```scala
import util.Strings.{shout, initials as short}
@main def run(): Unit =
  println(shout("hello") + " " + short("Ada Lovelace"))
```

```text
HELLO! A.L
```

**The one form with no counterpart.** A selector may name a **type**, and then
the imported name works everywhere a type works — in a constructor call, in a
type test, and, the interesting one, in a pattern:

Fixture: [`tests/conformance/tutorial/02-python-js-modules-a-type.scala`](../../tests/conformance/tutorial/02-python-js-modules-a-type.scala)

With `util/Shapes.scala` containing `case class Point(x: Int, y: Int)` and a
`def area(p: Point): Int`:

```scala
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))
```

```text
12
```

In Python, `from shapes import Point` binds **one** thing: the class object.
You call it, or you hand it to `isinstance`. Here the same import binds *two*
things under one name — the value that `Point(3, 4)` calls, and the type that
`case Point(x, y) =>` consults — and the position decides which one an
occurrence means. That is possible because an `import` is resolved while the
importing file is **compiled**, so the class's field list is known before the
`match` is turned into code. It is the ordinary way data structures move
between files here, and it has no equivalent in either language you know.

There is one more prefix worth recognising even though you cannot use it yet:
`import py.numpy as np` routes to another *runtime* in the same process, not to
a file. The routing works; no runtime registers the `py` alias today, so it
stops with a clear message.
[Chapter 15](15-modules-and-polyglot-interop.md) is the whole story — the five
forms, where files are found, importing types, the polyglot prefixes, and what
each failure prints.

## 2.19 Where to go next

- [Chapter 4](04-values-and-expressions.md): literals, operators (which are
  methods), strings, equality, `if` and `while` in detail.
- [Chapter 5](05-functions-and-closures.md): everything about `def`, lambdas,
  closures, recursion and `lazy val`.
- [Chapter 6](06-classes-objects-and-traits.md): classes, constructors,
  singletons and companions, traits and how they stack.
- [Chapter 7](07-case-classes-and-pattern-matching.md): case classes, tuples,
  `Option`, and every form of pattern.
- [Chapter 9](09-for-comprehensions.md): `for … yield`, what it is rewritten
  into, and the placeholder `_`.
- [Chapter 11](11-exceptions.md) and
  [chapter 12](12-enums-and-sealed-hierarchies.md): exceptions, and enums as
  algebraic data types.
- [Chapter 13](13-actors-and-futures.md): actors, futures and concurrency
  without a GIL.
- [Chapter 14](14-repl-and-tooling.md): the REPL, for trying each idea
  interactively.
- [Chapter 15](15-modules-and-polyglot-interop.md): modules, the five import
  forms, and importing from another language's runtime.
