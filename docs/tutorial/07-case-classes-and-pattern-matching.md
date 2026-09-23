# 7. Case Classes and Pattern Matching

> **Implementation status.** Everything in this chapter runs today: case
> classes and case objects with their synthesised `apply`, `unapply`, `copy`,
> `equals`, `hashCode`, `toString` and `Product` members; tuples `Tuple2` to
> `Tuple22`; `Option`/`Some`/`None`; `match` with every pattern form listed in
> §7.5; and user-written extractors. What is not implemented yet: `enum` and
> exhaustiveness checking of a `sealed` hierarchy (Phase 4 — with types erased
> there is nothing to check, D4), and named arguments anywhere except `copy`
> (Phase 4).

Case classes and `match` are the pair that makes Scala feel unlike Python or
JavaScript. A case class is a data shape with structural equality and a
readable `toString`; `match` takes such a shape apart. Together they replace
the visitor pattern, `instanceof` chains, dictionaries-as-records, and most
uses of `null`.

## 7.1 Case classes

Fixture: [`tests/conformance/tutorial/07-case-basics.scala`](../../tests/conformance/tutorial/07-case-basics.scala)

```scala
case class Point(x: Int, y: Int)

@main def run(): Unit =
  val p = Point(1, 2)
  println(p.toString + " " + (p == Point(1, 2)) + " " + p.copy(y = 5) + " " + (p.x + p.y))
```

Prints:

```text
Point(1,2) true Point(1,5) 3
```

One word, `case`, adds all of this to a class:

| Member | What it gives you |
|---|---|
| `Point(1, 2)` | a companion `apply`, so `new` is unnecessary |
| `Point.unapply(p)` | the extractor `match` uses (§7.4) |
| `p.x`, `p.y` | every constructor parameter is a public `val` |
| `p == q` | **structural** equality: same class, equal fields |
| `p.hashCode` | structural, and bit-identical to the JVM's |
| `p.toString` | `Point(1,2)` |
| `p.copy(y = 5)` | a new instance with some fields replaced |
| `productArity`, `productElement(i)`, `productPrefix`, `_1`… | the `Product` members |
| `p.canEqual(q)` | the equality-cooperation hook |

`copy` is the way to "modify" an immutable value: it takes the fields you name
and keeps the rest. It is also the one place where **named arguments** work
before Phase 4 — `f(b = 1)` on an ordinary method is still rejected with
`named arguments are not implemented yet`.

`==` deserves a sentence of its own. On a plain class it is identity (chapter
6, §6.7); on a case class it is structural, and it follows Scala's rule
exactly: `a == b` requires `b` to be an instance of `a`'s class *and*
`b.canEqual(a)` to hold, so a subclass can refuse equality with its parent.
`hashCode` is Scala 3's synthesised MurmurHash3, seed and all, so
`Point(1, 2).hashCode` is `-694993394` here and on the JVM.

**For Python and JavaScript readers.** A case class is Python's
`@dataclass(frozen=True)` — `__init__`, `__eq__`, `__hash__`, `__repr__` and
`replace()` for free — or a TypeScript `readonly` record with structural
equality that JavaScript does not have at all. Where JavaScript makes you
write `{...p, y: 5}` and Python `replace(p, y=5)`, Scala writes
`p.copy(y = 5)`.

## 7.2 Tuples

Fixture: [`tests/conformance/tutorial/07-case-tuples.scala`](../../tests/conformance/tutorial/07-case-tuples.scala)

```scala
@main def run(): Unit =
  val person = ("Ada", 36)
  val (name, age) = person
  println(person.toString + " " + name + " " + age + " " + (person == ("Ada", 36)))
```

Prints:

```text
(Ada,36) Ada 36 true
```

A tuple is an anonymous record. `("Ada", 36)` has type `(String, Int)`, its
fields are `_1` and `_2`, and it prints as `(Ada,36)` — no space after the
comma, which surprises everyone once.

`val (name, age) = person` is a **pattern** on the left of `=`: it takes the
tuple apart and binds both names. That is the same machinery as §7.4, used in
a definition instead of a `match`.

Tuples are case classes here, not a separate kind of value: `Tuple2` to
`Tuple22` are ordinary case classes, so `==`, `hashCode` and `toString` come
from §7.1 and agree with the JVM. `(1, "a").hashCode` is `1971805870` on both.
There is no `Tuple23`: more than 22 elements is a compile error (D32), where
Scala 3 falls back to `TupleXXL`.

> Under the hood this is deliberate. protoCore has a `ProtoTuple` type, but it
> interns every node and interned tuples live for the whole process, so a
> transient `(a, b)` would never be collected. Scala tuples are therefore
> modelled as what they already are in Scala — case classes (DESIGN §4.6).

**For Python and JavaScript readers.** `val (name, age) = person` is Python's
`name, age = person` and JavaScript's `const [name, age] = person`. The
difference is that a Scala tuple has a fixed arity and each position has its
own type, so `(String, Int)` is a record, not a list.

## 7.3 `Option`: a value that may be missing

Fixture: [`tests/conformance/tutorial/07-case-option.scala`](../../tests/conformance/tutorial/07-case-option.scala)

```scala
def half(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None

@main def run(): Unit =
  println(half(8).toString + " " + half(3) + " " + half(8).getOrElse(0) + " " + half(3).getOrElse(0))
```

Prints:

```text
Some(4) None 4 0
```

`Option[A]` has exactly two cases: `Some(value)` and `None`. A function that
may not have an answer returns an `Option` instead of `null`, and the caller
cannot forget to handle the empty case — there is no field to dereference on
`None`.

`Option` is written in protoScala itself, in `lib/prelude.scala`, as a sealed
hierarchy of the case class `Some` and the case object `None`. It carries
`isEmpty`, `isDefined`, `nonEmpty`, `get`, `getOrElse`, `orElse`, `map`,
`flatMap`, `filter`, `withFilter`, `foreach`, `contains`, `exists` and
`toList`. Because it has `map`, `flatMap` and `withFilter`, it works in a
for-comprehension (chapter 9, §9.5).

> **Deviation D33.** `getOrElse(default)` evaluates its default **eagerly**.
> In Scala the parameter is by-name, so `Some(1).getOrElse(expensive())` never
> runs `expensive()`; here it does. By-name parameters arrive in Phase 4;
> until then, guard an expensive default with `if o.isEmpty then … else o.get`.
> Chapter 3 has the fixture.

**For Python and JavaScript readers.** `Option` is what `None` and `undefined`
try to be and fail at, because `None`/`undefined` are values of *every* type
and nothing forces you to check. `Some(4)` is a box; `half(3)` gives you an
empty box, not a hole in your program.

## 7.4 Algebraic data types and `match`

Fixture: [`tests/conformance/tutorial/07-case-adt.scala`](../../tests/conformance/tutorial/07-case-adt.scala)

```scala
sealed trait Expr
case class Num(n: Int) extends Expr
case class Add(l: Expr, r: Expr) extends Expr
case class Mul(l: Expr, r: Expr) extends Expr

def eval(e: Expr): Int = e match
  case Num(n) => n
  case Add(l, r) => eval(l) + eval(r)
  case Mul(l, r) => eval(l) * eval(r)

def show(e: Expr): String = e match
  case Num(n) => n.toString
  case Add(l, r) => "(" + show(l) + " + " + show(r) + ")"
  case Mul(l, r) => "(" + show(l) + " * " + show(r) + ")"

@main def run(): Unit =
  val e = Add(Num(2), Mul(Num(3), Num(4)))
  println(eval(e).toString + " " + show(e))
```

Prints:

```text
14 (2 + (3 * 4))
```

This is the shape most Scala programs are built from. A `sealed trait` names
a set of alternatives; the case classes under it are those alternatives. The
data has no behaviour: `eval` and `show` are two of many functions over the
same tree, and adding a third changes nothing in the data.

A `match` is an **expression**: it produces the value of the branch that
matched, so `def eval(e: Expr): Int = e match …` needs no `return` and no
temporary. Cases are tried **top to bottom** and the first that matches wins,
so order is significant — a `case _` first would swallow everything.
`case Add(l, r)` does two things at once: it tests that `e` is an `Add`, and
it binds `l` and `r` to its fields, by calling the `unapply` the `case`
keyword synthesised.

`sealed` in Scala 3 tells the compiler that the alternatives are all in this
file, so it can warn you when a `match` forgets one. protoScala parses
`sealed` and accepts it as documentation, but it erases types (D4), so there
is **no exhaustiveness check**: a forgotten case surfaces as a `MatchError`
when a value reaches it (§7.7). Until Phase 4, a `case _` that raises a clear
error is a cheap substitute.

## 7.5 A tour of patterns

Fixture: [`tests/conformance/tutorial/07-case-patterns.scala`](../../tests/conformance/tutorial/07-case-patterns.scala)

```scala
def kind(v: Any): String = v match
  case 0 => "zero"
  case n: Int if n < 10 => "small:" + n
  case h :: t if t.length == 1 => "list:" + h + "+" + t.head
  case (_, _: Int) => "pair"
  case (s: String, _) => "tuple:" + s
  case "hello" | "hi" => "text"
  case _ => "other"

@main def run(): Unit =
  println(List(0, 3, List(1, 2), ("k", 5), ("a", "b"), "hi", 3.5).map(kind).mkString(" "))
```

Prints:

```text
zero small:3 list:1+2 pair tuple:a text other
```

Read the cases against the seven values: `0` matches the literal; `3` is an
`Int` and passes the guard; `List(1, 2)` splits into head `1` and tail
`List(2)`; `("k", 5)` is a pair whose second element is an `Int`, so it wins
over the case below it; `("a", "b")` falls through to the next tuple pattern;
`"hi"` matches an alternative; `3.5` reaches the wildcard.

Every pattern form protoScala supports:

| Pattern | Matches | Example |
|---|---|---|
| Literal | that exact value (`==`) | `case 0`, `case "hi"`, `case true`, `case 'a'` |
| `null` | the null reference | `case null` |
| Wildcard `_` | anything, binds nothing | `case _` |
| Variable | anything, binds it (lower-case name) | `case n => n + 1` |
| Typed `x: T` | a value of class `T`, binds `x` | `case n: Int` |
| Type test `_: T` | a value of class `T`, binds nothing | `case _: Point` |
| Constructor `C(p1, p2)` | a `C` whose fields match, recursively | `case Add(l, r)` |
| Tuple `(p1, p2)` | a tuple of that arity, element-wise (≤ 22, D32) | `case (s, n)` |
| Cons `h :: t` | a non-empty list: head and tail | `case h :: t` |
| Sequence `List(p1, p2)` | a list of exactly that length | `case List(a, b)` |
| Sequence tail `List(p, rest*)` | a list starting with `p`; `rest` is the remainder | `case List(1, rest*)` |
| Bound sequence tail `rest @ _*` | the same, with an explicit name | `case List(_, rest @ _*)` |
| Alternative `p1 \| p2` | either; may not bind variables | `case "hello" \| "hi"` |
| Binder `x @ p` | what `p` matches, and binds the whole to `x` | `case q @ Point(a, b) if a == b` |
| Stable identifier | equality with an existing `val` (upper-case name) | `val Max = 10; case Max` |
| Back-quoted name | equality with an existing lower-case `val` | ``case `target` `` |
| Extractor `E(p…)` | whatever `E.unapply` accepts (§7.6) | `case Even(half)` |
| Guard `case p if cond` | `p`, and only when `cond` holds | `case n: Int if n < 10` |

Two rules catch everyone once. A lower-case name in a pattern is a **new
binding**, not a comparison — `case target =>` matches everything and shadows
the outer `target`; write `` case `target` `` to compare. And an upper-case
name is a **comparison** with an existing value, not a binding — `case Max =>`
compares against the `val Max`. Binding the same name twice in one pattern is
rejected (`duplicate pattern variable: x`), as is binding a variable inside an
alternative (`Illegal variable x in pattern alternative`).

Patterns are not confined to `match`. They also work on the left of a `val`
(`val (name, age) = person`, §7.2), in a generator of a for-comprehension
(chapter 9, §9.4), and as a function literal (`{ case (a, b) => a + b }`).

## 7.6 Extractors

Fixture: [`tests/conformance/tutorial/07-case-extractors.scala`](../../tests/conformance/tutorial/07-case-extractors.scala)

```scala
object Even:
  def unapply(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None
object Split:
  def unapply(s: String): Option[(String, String)] =
    val i = s.indexOf("=")
    if i < 0 then None else Some((s.substring(0, i), s.substring(i + 1)))

def num(n: Int) = n match
  case Even(half) => "even:" + half
  case _ => "odd"
def kv(s: String) = s match
  case Split(k, v) => k + "->" + v
  case _ => "none"

@main def run(): Unit = println(num(10) + " " + num(3) + " " + kv("a=b") + " " + kv("ab"))
```

Prints:

```text
even:5 odd a->b none
```

Pattern matching is not limited to case classes: any object with an `unapply`
method is a pattern. The protocol is Scala 3's:

| `unapply` returns | The pattern | Binds |
|---|---|---|
| `Option[T]` | `case E(x)` | one value |
| `Option[(A, B, …)]` | `case E(a, b, …)` | one per tuple element |
| `Boolean` | `case E()` | nothing — a named test |

`Some(...)` means "matched, here is what I extracted"; `None` means "not this
one, try the next case". That is how `case Even(half)` both tests a number and
computes half of it, and how `Split` turns a `String` into a key and a value
without a regular expression. A case class gets its `unapply` for free — this
is the same mechanism, written by hand.

The extractor's parameter type is not checked before `unapply` runs (types are
erased, D4/D29): if a value of the wrong shape reaches `case Even(_)`, the
failure appears inside `unapply`, not as a silent non-match.

## 7.7 When nothing matches

Fixture: [`tests/conformance/tutorial/07-case-match-error.scala`](../../tests/conformance/tutorial/07-case-match-error.scala)

```scala
@main def run(): Unit =
  val r = 5 match
    case 1 => "one"
  println(r)
```

It stops with:

```text
07-case-match-error.scala:3: error: MatchError: 5 (of class Int)
```

A `match` that runs out of cases raises a `MatchError` carrying the value and
its class. Scala on the JVM reports the same error with the JVM's class name —
`5 (of class java.lang.Integer)` — because there `Int` is boxed; protoScala
names the Scala class the value actually has.

## 7.8 For Python and JavaScript developers

Python 3.10 added `match`/`case`, and it was modelled on this one, so the
shapes will look familiar:

| Scala | Python | JavaScript |
|---|---|---|
| `e match { case … }` | `match e: case …` | `switch (e) { case … }` |
| `case Add(l, r) =>` | `case Add(l, r):` | manual `e instanceof Add && …` |
| `case (s, n) =>` | `case (s, n):` | `const [s, n] = e` |
| `case n: Int if n < 10 =>` | `case int(n) if n < 10:` | an `if` inside the case |
| `case "hello" \| "hi" =>` | `case "hello" \| "hi":` | fall-through `case`s |

Three differences matter:

1. **`match` is an expression.** It has a value, so it can be the whole body
   of a method or the right-hand side of a `val`. Python's `match` and
   JavaScript's `switch` are statements that must assign to something.
2. **There is no fall-through and no `break`.** The first matching case runs,
   and only that one. JavaScript's `switch` runs everything after the match
   until a `break`; that mistake is impossible here.
3. **Patterns destructure.** `case Add(l, r)` gives you the fields, already
   named. JavaScript's `switch` compares with `===` and leaves you to pull the
   pieces out yourself.

Compared with Python, the real gain is that the alternatives are a declared
set (`sealed trait Expr`) rather than a convention, and that a case class is
a type rather than a dictionary shape. Compared with JavaScript, almost
everything you would write as a chain of `typeof`/`instanceof`/property
sniffing becomes one `match`.

## 7.9 For Scala developers

- **D29 — type tests follow the runtime representation.** There is one integer
  type and one floating type (D1, D2), so `3L.isInstanceOf[Int]` is `true`
  here and `false` on the JVM, and `case n: Int` matches a value written as a
  `Long`. `asInstanceOf` never converts — `(1: Any).asInstanceOf[Double]`
  throws rather than widening — and `null.asInstanceOf[Int]` is `null`. Chapter
  3 has the fixture.
- **`isInstanceOf`/`asInstanceOf` test the class only.** Type arguments are
  erased, so `xs.isInstanceOf[List[String]]` tests `List`, exactly as the JVM
  does, but without the unchecked warning.
- **D32 — tuples stop at 22.** `Tuple23` and a tuple pattern of more than 22
  elements are compile errors (`tuples of more than 22 elements are not
  supported`), where Scala 3 uses `TupleXXL`.
- **D34 — `{ case … }` takes exactly one argument.** In Scala 3 a
  pattern-matching function literal adapts to the expected arity, so
  `val add: (Int, Int) => Int = { case (a, b) => a + b }` compiles and `add(1,
  2)` is `3`. Here the literal is always a one-parameter function, and calling
  it with two arguments raises `IllegalArgumentException: wrong number of
  arguments`. Write `{ (a: Int, b: Int) => a + b }`, or pass the pair as a
  tuple. Chapter 3 has the fixture.
- **No exhaustiveness check** on a `sealed` hierarchy (D4), and no `enum`
  until Phase 4.
- **Named arguments only in `copy`** until Phase 4.

Everything else — structural `equals` with `canEqual`, JVM-identical
`hashCode`, `copy`, the `Product` members, the pattern forms of §7.5, the
`unapply` protocol, `MatchError` — matches Scala 3.9, and every fixture in
this chapter was run against it.
