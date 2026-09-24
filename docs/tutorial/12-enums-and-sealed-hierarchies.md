# 12. Enums and Sealed Hierarchies

> **Implementation status.** Everything in this chapter runs today: `enum` with
> simple and parameterised cases, `ordinal`, `values`, `valueOf`, `fromOrdinal`,
> methods in the enum body, enums as algebraic data types, and `sealed trait`
> hierarchies. What is not implemented: `derives` (parsed and ignored, D79),
> exhaustiveness checking (D4 — a missing case is a run-time `MatchError`), and
> `values` answers a `List` rather than an `Array` (D77).

An `enum` names the alternatives of a type. In its simplest form it is what every
language calls an enumeration; in its full form it is an **algebraic data type**,
and that is the form worth learning, because it is what a `match` was designed to
take apart.

```text
enum Colour:
  case Red, Green, Blue
```

## 12.1 Simple cases

Fixture: [`tests/conformance/tutorial/12-enums-simple.scala`](../../tests/conformance/tutorial/12-enums-simple.scala)

```scala
enum Colour:
  case Red, Green, Blue
@main def run(): Unit =
  println(Colour.Red.toString + " " + Colour.Red.ordinal + " " + Colour.Blue + " " +
    Colour.Blue.ordinal + " " + Colour.values.length)
```

Prints:

```text
Red 0 Blue 2 3
```

Three things fall out of the declaration:

- each case is a **value**, named `Colour.Red`. It prints as `Red`, compares by
  identity, and there is exactly one of it;
- each case has an **`ordinal`**, its position in the declaration, counting from
  zero;
- the enum's companion carries **`values`**, the cases in declaration order.

The qualifier is required, as in Scala: `Red` on its own is not in scope unless
you are inside the enum's own body or its companion.

Fixture: [`tests/conformance/tutorial/12-enums-need-the-qualifier.scala`](../../tests/conformance/tutorial/12-enums-need-the-qualifier.scala)

```scala
enum Colour:
  case Red, Green
@main def run(): Unit = println(Red)
```

```text
12-enums-need-the-qualifier.scala:6:33: error: Not found: Red
```

The braced spelling is the same declaration:

```scala
enum Colour {
  case Red, Green, Blue
}
```

## 12.2 Looking a case up

Fixture: [`tests/conformance/tutorial/12-enums-values-and-lookup.scala`](../../tests/conformance/tutorial/12-enums-values-and-lookup.scala)

```scala
enum Colour:
  case Red, Green, Blue
@main def run(): Unit =
  val missing =
    try Colour.valueOf("Purple").toString
    catch case e: IllegalArgumentException => "no such case"
  println(Colour.values.map(_.toString).mkString(",") + " | " + Colour.valueOf("Green") +
    " | " + Colour.fromOrdinal(2) + " | " + missing)
```

```text
Red,Green,Blue | Green | Blue | no such case
```

- `values` — every case, in declaration order. A `List` here, an `Array` in Scala
  (D77): the elements and the order are the same, only the container differs.
- `valueOf(name)` — the case with that name, or an `IllegalArgumentException`
  saying `enum Colour has no case with name: Purple` (scalac's own sentence).
- `fromOrdinal(n)` — the case with that ordinal, or a `NoSuchElementException`
  saying `enum Colour has no case with ordinal: 9`.

`values` and `valueOf` exist only when **every** case is a singleton, which is
also Scala's rule: an enum with a parameterised case has no list of values to
give. `fromOrdinal` always exists and covers the singleton cases.

## 12.3 Matching on an enum

Fixture: [`tests/conformance/tutorial/12-enums-match.scala`](../../tests/conformance/tutorial/12-enums-match.scala)

```scala
enum Colour:
  case Red, Green, Blue
def describe(c: Colour): String = c match
  case Colour.Red   => "warm"
  case Colour.Green => "cool"
  case Colour.Blue  => "cool"
@main def run(): Unit =
  println(describe(Colour.Red) + " " + describe(Colour.Green) + " " + describe(Colour.Blue))
```

```text
warm cool cool
```

Scala checks that a `match` on a sealed type covers every case and warns when it
does not. protoScala **cannot**: types are erased (D4), so there is nothing at
compile time that knows the scrutinee is a `Colour`. A missing case is found when
it is reached:

Fixture: [`tests/conformance/tutorial/12-enums-exhaustiveness-is-not-checked.scala`](../../tests/conformance/tutorial/12-enums-exhaustiveness-is-not-checked.scala)

```scala
enum Shape:
  case Circle, Square
@main def run(): Unit =
  val s: Shape = Shape.Square
  println(s match { case Shape.Circle => "round" })
```

```text
12-enums-exhaustiveness-is-not-checked.scala:8: error: MatchError: Square (of class Square)
```

This is the single biggest thing to keep in mind when you bring Scala habits
here: **the compiler will not remind you of a case you forgot.** Write a final
`case _ =>` where a default is meaningful, and a test for every branch where it
is not.

## 12.4 Cases that carry data

A case may take parameters of its own, and a different case may take different
ones. That makes the enum an **algebraic data type** — the shape a `match` exists
for:

Fixture: [`tests/conformance/tutorial/12-enums-as-an-adt.scala`](../../tests/conformance/tutorial/12-enums-as-an-adt.scala)

```scala
enum Tree:
  case Leaf(n: Int)
  case Node(left: Tree, right: Tree)
def sum(t: Tree): Int = t match
  case Tree.Leaf(n)       => n
  case Tree.Node(l, r)    => sum(l) + sum(r)
def depth(t: Tree): Int = t match
  case Tree.Leaf(_)    => 1
  case Tree.Node(l, r) => 1 + (if depth(l) > depth(r) then depth(l) else depth(r))
@main def run(): Unit =
  val t = Tree.Node(Tree.Leaf(1), Tree.Node(Tree.Leaf(2), Tree.Leaf(3)))
  println(sum(t).toString + " " + depth(t))
```

```text
6 3
```

A parameterised case is a **case class**, so it gets everything chapter 7
described: a constructor without `new`, a `toString`, structural `equals`, a
`hashCode` and an extractor for pattern matching. A singleton case is a **case
object**. Ordinals count every case, parameterised ones included:
`Tree.Leaf(1).ordinal` is 0 and `Tree.Node(…).ordinal` is 1.

The whole enum can also take parameters, which every case then passes up:

Fixture: [`tests/conformance/tutorial/12-enums-with-parameters.scala`](../../tests/conformance/tutorial/12-enums-with-parameters.scala)

```scala
enum Colour(val rgb: Int):
  case Red extends Colour(0xFF0000)
  case Blue extends Colour(0x0000FF)
@main def run(): Unit =
  println(Colour.Red.rgb.toString + " " + Colour.Blue.rgb)
```

```text
16711680 255
```

And the enum body may carry ordinary members, which every case inherits:

Fixture: [`tests/conformance/tutorial/12-enums-with-methods.scala`](../../tests/conformance/tutorial/12-enums-with-methods.scala)

```scala
enum Colour:
  case Red, Green
  def shout: String = toString.toUpperCase
@main def run(): Unit =
  println(Colour.Red.shout + " " + (Colour.Red == Colour.Red))
```

```text
RED true
```

## 12.5 `sealed trait`, the other spelling

An `enum` is sugar. What it becomes is a `sealed abstract class` with one child
per case and a companion, which is exactly what you can write by hand:

Fixture: [`tests/conformance/tutorial/12-enums-sealed-trait.scala`](../../tests/conformance/tutorial/12-enums-sealed-trait.scala)

```scala
sealed trait Shape
case class Circle(r: Double) extends Shape
case class Square(side: Double) extends Shape
def area(s: Shape): Double = s match
  case Circle(r)    => 3.14 * r * r
  case Square(side) => side * side
@main def run(): Unit =
  println(area(Circle(2.0)).toString + " " + area(Square(2.0)))
```

```text
12.56 4.0
```

Which to reach for:

- **`enum`** when the cases are simply the alternatives of one type. It is
  shorter, it gives you `values`, `ordinal`, `valueOf` and `fromOrdinal`, and the
  cases are namespaced under the enum.
- **`sealed trait` with case classes** when a case needs its own type parameters,
  its own extra parents, or a name that should not be namespaced — or when the
  cases are spread over concepts rather than being one closed list.

Neither form is checked for exhaustiveness here (D4), so the choice is about
expressiveness, not safety.

## 12.6 For the Python or JavaScript developer

A simple `enum` is Python's `Enum` with `ordinal` where Python has `value`:

| Python | protoScala |
|---|---|
| `class Colour(Enum): RED = 0; GREEN = 1` | `enum Colour: case Red, Green` |
| `Colour.RED.value` | `Colour.Red.ordinal` |
| `Colour.RED.name` | `Colour.Red.toString` |
| `list(Colour)` | `Colour.values` |
| `Colour["RED"]` | `Colour.valueOf("Red")` |
| `Colour(0)` | `Colour.fromOrdinal(0)` |

In TypeScript a simple `enum` is an `enum` and an ADT is a **discriminated
union**:

```ts
type Tree = { kind: "leaf"; n: number } | { kind: "node"; left: Tree; right: Tree }
```

and you take it apart with a `switch` on `kind`. The protoScala version needs no
tag field and no `switch` on a string: the case *is* the tag, and the `match`
extracts the data in the same breath as it tests. JavaScript has nothing at all —
an ADT there is a hand-rolled object with a `type` string.

That is the part worth the most of your attention. A simple enumeration you have
met before; a closed set of alternatives *that carry different data*, matched by
shape, is the thing this chapter is really about, and it is how a tree, a parsed
document, a state machine and a result-or-error are all written here.

## 12.7 What differs from Scala 3 in this area

- **D77** — `values` answers a `List`, where Scala answers an `Array`. The
  elements and their order are identical; matching Scala would need an `Array`
  type this dialect does not have.
- **D79** — a `derives` clause is parsed and **ignored**, as on every other
  template (D3): protoScala has no type classes to derive. `enum` type parameters
  are parsed and erased like every other type.
- **D4** — exhaustiveness is not checked, for `enum` or for `sealed trait`.
- An `enum` case that overrides a member of the enum class is not supported; the
  member belongs to the enum.

`Priority`, the actor priority band of chapter 13, used to be three integers on an
object; it is a real `enum` now, and that deviation is retired.

---

Next: [13. Actors and futures](13-actors-and-futures.md)
