# 6. Classes, Objects and Traits

> **Implementation status.** Everything in this chapter runs today. What is
> not implemented: a template nested in a **`class`** or a **`trait`**, a local
> class inside a block and an anonymous class (`new T { ... }`) — all three capture
> the enclosing instance (D80), and the message says what is allowed:
> `classes, traits and objects must be defined at the top level of a file or in an
> object`. Nesting in an **`object`** works (§6.9), as do multiple constructor
> parameter lists (§6.9, D84), qualified super calls (§6.7, D76) and extension
> methods (§6.8). Access modifiers are advisory except `private`,
> which is enforced at run time (D5). Methods cannot be overloaded (D31), and
> Scala 3's *universal apply* — writing `Temp(1.0)` for a plain class with no
> companion — needs an explicit companion `apply` here (§6.3).

A class describes objects: their fields, their methods, and what they inherit.
An `object` describes exactly one of them. A `trait` describes a slice of
behaviour that classes mix in. Together they are the whole object model of
Scala — there is no separate notion of interface, module, static member or
namespace.

## 6.1 Classes

Fixture: [`tests/conformance/tutorial/06-classes-point.scala`](../../tests/conformance/tutorial/06-classes-point.scala)

```scala
class Point(val x: Int, var y: Int):
  def sum = x + y
  def moved(dx: Int) = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"

@main def run(): Unit =
  val p = new Point(3, 4)
  println(p.toString + " " + p.sum + " " + p.moved(1))
```

Prints:

```text
Point(3, 4) 7 Point(4, 4)
```

The parameter list after the class name *is* the constructor, and the body of
the class is the constructor body: every statement in it runs, in source
order, when an instance is created. What a constructor parameter becomes
depends on the keyword in front of it:

| Written | Becomes |
|---|---|
| `val x: Int` | a public field, readable from anywhere, not reassignable |
| `var y: Int` | a public field with a setter, `p.y = 9` |
| `x: Int` (no keyword) | visible inside the class only, not a member |
| `private val x: Int` | a field only the class (and its companion) can read (§6.5) |

Methods are `def`s in the body, exactly as in chapter 5, and they see the
constructor parameters as if they were in scope — because they are.
`override def toString` replaces the default rendering; without it,
`p.toString` is `Point@` followed by an identity hash.

**For Python and JavaScript readers.** `class Point(val x: Int, var y: Int)`
is `__init__`/`constructor` *and* the field declarations, in one line: there
is no `self.x = x` to write, and no `this.x = x`. `new Point(3, 4)` creates an
instance; `p.x` reads a field and `p.sum` calls a method. Python's `@property`
and JavaScript's `get`/`set` have no counterpart here because they need none:
a `def` with no parameter list already reads like a field
(`def sum = x + y`, used as `p.sum`), so you can turn a field into a
computation later without touching the callers. That is Scala's *uniform
access principle*.

**For Scala readers.** An instance of a class that declares no `var` field is
an immutable protoCore object, rebuilt field by field as the constructor runs.
A `this` that escapes the constructor before the last field is stored
therefore denotes an *earlier version* of the object — it lacks the later
fields and is not `==` to the finished instance. That is deviation D28; see
[chapter 3, §3.2](03-for-the-scala-developer.md#provisional-deviations-phase-2).

Auxiliary constructors work as in Scala (`def this(n: Int) = this(n, 1)`), and
they are chosen by their *number* of parameters, never by their types (D31).

## 6.2 Mutable state

Fixture: [`tests/conformance/tutorial/06-classes-counter.scala`](../../tests/conformance/tutorial/06-classes-counter.scala)

```scala
class Counter:
  var count = 0
  def inc(): Unit = count += 1

@main def run(): Unit =
  val c = new Counter
  c.inc()
  c.inc()
  val two = c.count
  c.count += 10
  println(two.toString + " " + c.count)
```

Prints:

```text
2 12
```

A class with no parameters needs no parentheses: `class Counter:` and
`new Counter`. A `var` field is readable and writable from outside, and
`c.count += 10` is `c.count = c.count + 10`, which in turn is
`c.count_=(c.count.+(10))` — assignment to a field is a method call on the
setter that `var` generates. You can write the setter yourself (`def
count_=(v: Int): Unit = ...`) when a field needs validation.

`val c = new Counter` makes the *binding* `c` unchangeable, not the object:
`c` cannot be pointed at another counter, but the counter it points at keeps
mutating. This is `const` in JavaScript, and it is the distinction Python
makes between rebinding a name and mutating the object it names.

Instances of a class that declares a `var` keep their identity across
mutations: `c` before `c.inc()` and `c` after it are the same object, so a
reference stored elsewhere sees the update. (For a class with no `var` at all,
D28 above explains why the constructor is the one place where that is not yet
true.)

## 6.3 Objects and companions

Fixture: [`tests/conformance/tutorial/06-classes-objects.scala`](../../tests/conformance/tutorial/06-classes-objects.scala)

```scala
class Temp(val celsius: Double):
  override def toString = "Temp(" + celsius + ")"

object Temp:
  var made = 0
  def apply(c: Double): Temp =
    made += 1
    new Temp(c)
  def fromF(f: Double): Temp = apply((f - 32) * 5 / 9)

@main def run(): Unit =
  println(Temp(21.0).toString + " " + Temp.fromF(212.0) + " " + Temp.made)
```

Prints:

```text
Temp(21.0) Temp(100.0) 2
```

`object Temp` declares one object and names it. It is created lazily, the
first time anything touches it, and there is never a second one. Scala has no
`static` keyword because it does not need one: what other languages put on the
class, Scala puts in the object.

An `object` that shares its name with a class in the same file is its
**companion**. A companion and its class see each other's `private` members,
which is how `apply` can reach a private constructor or a private field.

`Temp(21.0)` is not special syntax: `expr(args)` on any value means
`expr.apply(args)` — the *universal `apply` rule* (DESIGN §5.1). Since `Temp`
here is the companion object, `Temp(21.0)` is `Temp.apply(21.0)`, which the
counter `made` proves ran twice: once directly and once through `fromF`.

> In Scala 3, `Temp(21.0)` would also work with *no* companion at all
> (universal apply methods synthesise one). protoScala does not synthesise it
> for a plain class yet: either write the companion `apply`, as above, or use
> `new`. Case classes (chapter 7) always get their `apply` for free.

**For Python and JavaScript readers.** An `object` is the module-level
singleton you write in Python as a module full of functions, or in JavaScript
as an exported object literal — but it is a real object, so it can extend a
class, mix in traits and be passed around. `apply` is Python's `__call__`: it
makes the object callable.

## 6.4 Traits, abstract members and linearization

A trait declares members without necessarily defining them:

Fixture: [`tests/conformance/tutorial/06-classes-abstract.scala`](../../tests/conformance/tutorial/06-classes-abstract.scala)

```scala
trait Shape:
  def area: Double
  def name: String
  def describe = name + " " + area

class Circle(r: Double) extends Shape:
  def area = 3.14 * r * r
  def name = "circle"

class Square(side: Double) extends Shape:
  def area = side * side
  def name = "square"

@main def run(): Unit =
  println(new Circle(2.0).describe + " " + new Square(2.0).describe)
```

Prints:

```text
circle 12.56 square 4.0
```

`def area: Double` with no `=` is *abstract*: `Shape` promises it exists and
leaves it to the implementors. `describe` is concrete and calls both abstract
members — the template-method pattern, with no ceremony. A class that extends
`Shape` and forgets `area` is rejected before it runs
(`class Circle needs to be abstract, since def area is not defined`), and a
trait or abstract class cannot be instantiated
(`Shape is a trait; it cannot be instantiated`, and `A is abstract; it cannot
be instantiated` for an abstract class).

Implementing an abstract member needs no `override`. Redefining a member that
already has a body *does*, exactly as in Scala 3 — that is what `override def
toString` in §6.1 was for.

A class may extend one class and any number of traits. When several of them
define the same member, Scala decides which wins by **linearization**:

Fixture: [`tests/conformance/tutorial/06-classes-traits.scala`](../../tests/conformance/tutorial/06-classes-traits.scala)

```scala
abstract class A:
  def who: String = "A"
trait T1 extends A:
  override def who = "T1>" + super.who
trait T2 extends A:
  override def who = "T2>" + super.who
class B extends A:
  override def who = "B>" + super.who
class C extends B with T1 with T2

@main def run(): Unit = println(new C().who)
```

Prints:

```text
T2>T1>B>A
```

The linearization of `C` is

```text
L(C) = C, T2, T1, B, A, AnyRef, Any
```

— the class itself first, then its parents **right to left**, with shared
ancestors kept at their *last* occurrence. A member is looked up along that
list, and the first definition found wins: `new C().who` starts at `T2`.

Inside a trait, `super.who` does not mean "the superclass". It means **the
next definition of `who` after this trait in the linearization of the object
the call is running on** — which is not known when the trait is compiled. That
is what makes traits *stackable*: `T2` calls `T1`, `T1` calls `B`, `B` calls
`A`. Writing `class C extends B with T2 with T1` instead would print
`T1>T2>B>A` from the very same trait bodies.

Traits may take parameters (`trait Greeter(val greeting: String)`), have
`val`/`var` fields and run initialisation code, and the initialisers run in
linearization order, left to right along the parents — `A`, `B`, `T1`, `T2`,
`C` for the example above.

**For Python and JavaScript readers.** A trait is a mixin. Python's multiple
inheritance and its C3 MRO are the closest analogue, and Scala's
linearization is the same idea with one difference worth memorising: Python
reads bases **left to right**, Scala reads them **right to left**, so the
*last* trait listed is the one that wins. In JavaScript, traits replace the
`Object.assign(Base.prototype, mixin)` idiom, and `super.who` replaces the
manual `mixin.who.call(this)` chaining. Unlike a Python mixin, a Scala trait
can leave members abstract and be type-checked against — and unlike a
JavaScript mixin, stacking two traits that both override the same method
composes them instead of silently dropping one.

## 6.5 Privacy

Fixture: [`tests/conformance/tutorial/06-classes-private.scala`](../../tests/conformance/tutorial/06-classes-private.scala)

```scala
class Account(private val balance: Int):
  def canPay(amount: Int) = amount <= balance

@main def run(): Unit =
  val a = new Account(100)
  println(a.canPay(50))
  println(a.balance)
```

It prints `true` and then stops with:

```text
06-classes-private.scala:8: error: NoSuchMethodError: value balance is not a member of Account
```

`private` works: `canPay` reads `balance` from inside the class, and the
outside world cannot. But protoScala has no static type checker (D4), so the
violation is caught **when the line runs**, not when the file is compiled.
Scala 3 rejects the same program at compile time with *"value balance cannot
be accessed as a member of (a: Account)"*. That is deviation D5: `private` is
enforced, `protected` and the qualified forms (`private[p]`) are parsed and
treated as public.

## 6.6 `apply`, `update` and functions as objects

Fixture: [`tests/conformance/tutorial/06-classes-apply.scala`](../../tests/conformance/tutorial/06-classes-apply.scala)

```scala
class Pair:
  var first = 0
  var second = 0
  def apply(i: Int): Int = if i == 0 then first else second
  def update(i: Int, v: Int): Unit = if i == 0 then first = v else second = v

@main def run(): Unit =
  val p = new Pair
  p(1) = 7
  val scale = (n: Int) => n * 10
  println(p(0).toString + " " + p(1) + " " + scale(1))
```

Prints:

```text
0 7 10
```

Two rewrites make this work, and they are the same two Scala uses for every
collection in the standard library:

| Written | Means |
|---|---|
| `p(i)` | `p.apply(i)` |
| `p(i) = v` | `p.update(i, v)` |

There is no built-in indexing operator: `xs(0)` on a `List` and `p(0)` on your
own class go through exactly the same rule.

The last line closes the circle. `scale` is a lambda, and a lambda is an
object whose `apply` runs its body — so `scale(1)` is `scale.apply(1)`, the
same rewrite again. Functions are objects, objects with `apply` are functions,
and nothing in the language distinguishes them.

## 6.7 `super[T].m`: naming the ancestor

`super.m` takes the next definition of `m` in the linearization after the class
the call is written in (§6.4). `super[T].m` names the ancestor **explicitly**:

Fixture: [`tests/conformance/tutorial/06-classes-super-qualified.scala`](../../tests/conformance/tutorial/06-classes-super-qualified.scala)

```scala
trait Greeter:
  def greet: String = "hello"
trait Loud extends Greeter:
  override def greet: String = "HELLO"
class Polite extends Loud:
  override def greet: String = "good day"
  def asLoud: String = super[Loud].greet
@main def run(): Unit =
  val p = new Polite()
  println((if p.asLoud == "HELLO" then 2 else 0).toString + " " +
    (if p.greet == "good day" then 9 else 0))
```

```text
2 9
```

The search starts **at** `T`, so `super[T].m` finds `T`'s own `m` when `T` defines
one and the `m` that `T` inherits when it does not — which is what "the member `m`
as seen from `T`" means. `super[T]` inside `T` itself would call itself, so it is
a compile error, and an ancestor that is not in the receiver's linearization at all
is a `NoSuchMethodError` naming the spelling that failed.

Scala requires `T` to be a **direct** parent; protoScala accepts any ancestor in
the linearization (**D76**), because it keeps no direct-parent list — a class's
description carries the flattened linearization. It is permissiveness, not a
semantic mismatch: no program scalac accepts behaves differently here.

## 6.8 Extension methods

An `extension` adds a method to a type you do not own:

Fixture: [`tests/conformance/tutorial/06-classes-extension-methods.scala`](../../tests/conformance/tutorial/06-classes-extension-methods.scala)

```scala
extension (n: Int) def triple: Int = n * 3
extension (s: String) def shout: String = s.toUpperCase
class Point(val x: Int, val y: Int)
extension (p: Point) def manhattan: Int = p.x + p.y
@main def run(): Unit =
  println(2.triple.toString + " " + "shout".shout + " " + new Point(2, 3).manhattan)
```

```text
6 SHOUT 5
```

Several methods at once is a **collective** extension, written with the members on
the following indented lines or in braces — and, as in Scala, with no `:` before
them:

```scala
extension (n: Int)
  def plus(m: Int): Int = n + m
  def times(m: Int): Int = n * m
```

What the declaration does here is install the method as a member of the receiver
type's **prototype**, which has two consequences worth knowing.

The first is that dispatch is on the **runtime** value, not the static type
(**D6**): a value reached through `Any` answers an extension too, and
`(2: Any).asInstanceOf[Int].triple` is `6`. Scala resolves an extension
statically, so a value typed `Any` would not find it there.

The second is that an extension is **global and session-wide** (**D82**). There is
no import to scope it with — `import` is parsed and ignored until UMD arrives — so
a method defined at the top level is visible to every piece of code that runs
after it, including inside definitions written later in the same file. An
extension on a builtin type mutates that type for the whole session (**D83**), so
two files cannot define conflicting extensions of the same name on the same type.
For the same reason a collision with a member the type **already** has is refused:

```text
error: extension: String already has a member named 'length'
```

Silently shadowing a builtin method would be unrecoverable within a session.
Re-running the *same* extension definition replaces it, so a reloaded script or a
repeated REPL line is fine.

One thing extensions bring with them: a **custom string interpolator** is just an
extension on `StringContext` (chapter 10, §10.8).

## 6.9 Templates inside an `object`

A `class`, `trait` or `object` may be written inside an `object`, which is how you
group a small family of types under one name:

Fixture: [`tests/conformance/tutorial/06-classes-templates-in-an-object.scala`](../../tests/conformance/tutorial/06-classes-templates-in-an-object.scala)

```scala
object Geometry:
  case class P(x: Int, y: Int)
  object Origin:
    val distance: Int = 5
@main def run(): Unit =
  println(Geometry.P(1, 2).toString + " " + Geometry.Origin.distance)
```

```text
P(1,2) 5
```

Nesting is arbitrarily deep, the name is `Geometry.P` from outside and plain `P`
inside `Geometry`, and the `toString` shows the simple name — all as in Scala.

What is **not** supported (**D80**): a template nested in a `class` or a `trait`, a
local class inside a block, and the anonymous-class form `new T { … }`. Each of
those captures the enclosing instance, which needs a per-instance class, and that
is a different feature; the roadmap records it for a later phase.

A constructor may also be written with several parameter lists, which
**concatenate** into one flat list (**D84**):

Fixture: [`tests/conformance/tutorial/06-classes-ctor-parameter-lists.scala`](../../tests/conformance/tutorial/06-classes-ctor-parameter-lists.scala)

```scala
class Rect(val w: Int)(val h: Int)
@main def run(): Unit =
  val r = new Rect(1)(2)
  println(r.w + r.h)
```

```text
3
```

So `new Rect(1)(2)` and `new Rect(1, 2)` are the same call here, where scalac
accepts only the curried spelling. Partial application of a constructor does not
exist.

## 6.10 For Scala developers

What differs from Scala 3 on the JVM, beyond the erased types of D4:

- **D28 — construction of immutable instances.** An instance of a class with
  no `var` field is rebuilt as each field is stored, so a `this` that escapes
  the constructor early (registered in a global, captured by a lambda in the
  class body) is an earlier, incomplete version of the object.
- **D30 — uninitialised fields.** Reading a field whose initialiser has not
  run yet raises `NoSuchMethodError`, where Scala quietly yields `0` or
  `null`. The error surfaces an initialisation-order bug instead of hiding it.
- **D31 — no overloading.** Two members of one template may not share a name.
  Auxiliary constructors are selected by their number of parameters, never by
  their types. Types are erased (D4), so there is nothing to dispatch on.
- **Top level or inside an `object`.** A `class`, `trait` or `object` nested in
  an `object` is lifted with a qualified name (§6.9); one nested in a `class` or a
  `trait`, a local class inside a block and an anonymous class `new T { … }` are
  rejected, because each captures the enclosing instance (D80).
- **`new` is still needed** for a plain class without a companion `apply`
  (§6.3). Case classes and companions behave as in Scala 3.
- **`==` on a plain class is identity**, exactly as in Scala, unless you
  override `equals`; and the default `hashCode` is an identity hash. Only case
  classes and tuples get structural equality (chapter 7).
- **The default `toString`** of a plain instance is `Name@<identity hash>`;
  for an `object` it prints `O@…` where the JVM prints `O$@…`.
- **`super[T].m` accepts any ancestor** (D76), **extensions are global** (D82,
  D83), **templates nest only in an `object`** (D80) and **constructor parameter
  lists concatenate** (D84) — §6.7 to §6.9 above.

Everything else in this chapter — the linearization algorithm, the
right-to-left rule, stackable `super`, trait parameters, trait initialisation
order, `override` requirements, the uniform access principle, `apply` and
`update` — matches Scala 3.9, and every fixture above was checked against it.
