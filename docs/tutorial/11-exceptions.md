# 11. Exceptions

> **Implementation status.** Everything in this chapter runs today: `throw`, the
> `Throwable` hierarchy, `try`/`catch`/`finally` in both syntaxes, `catch` as a
> full pattern match, the translation of every failure the runtime raises on its
> own, `Try` as the value-oriented alternative, and exceptions across an actor
> turn and a suspended `await`. What is not implemented: stack traces, `getCause`
> beyond `null`, `scala.util.control.NonFatal` and `ControlThrowable`, and a
> `Class[_]` value — `getClass` answers the class's simple name as a `String`
> (D86).

An exception is the answer to a question a function cannot answer. `"abc".toInt`
has no `Int` to give; `1 / 0` has no quotient; a configuration file with no
`port` key has no port. A language has two ways to say so: return a value that
says "no answer" — `Option`, `Either`, `Try` — or abandon the computation and let
an enclosing handler decide. protoScala has both, and this chapter is about the
second.

```text
throw new IllegalStateException("the file is missing")
```

## 11.1 `throw`, and what may be thrown

`throw e` abandons the current computation and looks for a handler, unwinding
one frame at a time until it finds one. `e` must be a **`Throwable`**: that is
Scala's rule and protoScala keeps it, so `case e: Exception` and `e.getMessage`
always mean something.

Fixture: [`tests/conformance/tutorial/11-exceptions-throw-and-catch.scala`](../../tests/conformance/tutorial/11-exceptions-throw-and-catch.scala)

```scala
@main def run(): Unit =
  try
    throw new IllegalStateException("the file is missing")
  catch
    case e: IllegalStateException => println("caught: " + e.getMessage)
```

Prints:

```text
caught: the file is missing
```

The braced spelling is the same program:

```scala
try {
  throw new IllegalStateException("the file is missing")
} catch {
  case e: IllegalStateException => println("caught: " + e.getMessage)
}
```

`throw null` raises a `NullPointerException`, as in Scala. Throwing something
that is not a `Throwable` raises an `IllegalArgumentException` naming what
arrived — *"throw expects a Throwable, got Int"*. In Scala that is a compile
error; here types are erased (D4), so it is detected when the `throw` runs. Late
**detection** is the price of a late-binding platform; a silent failure would not
be, which is why the message names both what was expected and what arrived.

## 11.2 `try`/`catch` is an expression

`try` is not a statement. It has a value, and both branches contribute to it:

Fixture: [`tests/conformance/tutorial/11-exceptions-try-is-an-expression.scala`](../../tests/conformance/tutorial/11-exceptions-try-is-an-expression.scala)

```scala
def parse(s: String): Int =
  try s.toInt
  catch case e: NumberFormatException => -1
@main def run(): Unit =
  println(parse("42").toString + " " + parse("x"))
```

```text
42 -1
```

That is why a `try` reads well as the right-hand side of a `val`, and why you
rarely need a `var` to carry the result out of a handler.

## 11.3 A `catch` clause is a pattern match

This is the part Scala programmers under-use and the part a Python programmer has
no analogue for. A `catch` body is not a list of types — it is **the same
pattern-match cascade** `match` compiles, over the exception value. Constructor
patterns, guards and type patterns all work, in one handler:

Fixture: [`tests/conformance/tutorial/11-exceptions-catch-is-a-pattern-match.scala`](../../tests/conformance/tutorial/11-exceptions-catch-is-a-pattern-match.scala)

```scala
case class HttpError(code: Int, path: String) extends Exception(path)
@main def run(): Unit =
  val out =
    try throw HttpError(404, "/etc/nope")
    catch
      case HttpError(404, p)          => "not found: " + p
      case HttpError(c, _) if c >= 500 => "server error " + c
      case e: Exception                => "other: " + e.getMessage
  println(out)
```

```text
not found: /etc/nope
```

Python needs an `if` inside the `except` body to do the same, and a separate
`raise` to put the value back when the `if` does not match. Here a clause that
does not match simply is not chosen, and **if no clause matches the exception
continues outward** rather than being replaced. That is the behaviour to hold on
to: a handler you did not write for is a handler that does not run.

An exception class of your own is an ordinary class:

Fixture: [`tests/conformance/tutorial/11-exceptions-user-defined.scala`](../../tests/conformance/tutorial/11-exceptions-user-defined.scala)

```scala
class ConfigError(msg: String, val line: Int) extends Exception(msg)
@main def run(): Unit =
  try throw new ConfigError("missing key 'port'", 7)
  catch case e: ConfigError => println(e.toString + " " + e.line)
```

```text
ConfigError: missing key 'port' 7
```

`toString` is `<class>: <message>`, which is what Scala prints too.

Scala also allows `catch h`, with `h` a function. protoScala accepts it and
rewrites it to the single case `case e => h(e)` (D85), which is what `catch` of a
*total* function means. A genuine `PartialFunction` would rethrow in Scala and
raise a `MatchError` here, so prefer `case` clauses.

## 11.4 `finally`

A `finally` body runs whichever way the `try` is left: normally, by an
exception, or by a `return`.

Fixture: [`tests/conformance/tutorial/11-exceptions-finally.scala`](../../tests/conformance/tutorial/11-exceptions-finally.scala)

```scala
@main def run(): Unit =
  try
    print("opened ")
    try
      throw new RuntimeException("boom")
    finally
      print("closed ")
  catch
    case e: RuntimeException => println("caught")
```

```text
opened closed caught
```

The cleanup runs **before** the enclosing handler, because the exception is still
on its way out when the `finally` fires. Nested cleanups run innermost first, and
a `return` inside a `try … finally` runs every enclosing cleanup before it leaves
the method.

A `finally` body's value is discarded — `try 1 finally 2` is `1` — and a
`finally` that itself throws **replaces** the in-flight exception (D72). Scala
does the same and warns about it; protoScala has no warnings (D4), so treat a
throwing cleanup as a bug.

## 11.5 What the runtime raises on its own

A failure inside the runtime is not a special kind of error: it becomes a real
exception value of the class its name means, which your `catch` clauses see like
any other.

Fixture: [`tests/conformance/tutorial/11-exceptions-from-the-runtime.scala`](../../tests/conformance/tutorial/11-exceptions-from-the-runtime.scala)

```scala
@main def run(): Unit =
  def classOf(body: () => Any): String =
    try { body(); "no error" } catch case e: Throwable => e.getClass
  def report(body: () => Any): String =
    try { body(); "no error" } catch case e: Throwable => e.toString
  println(report(() => 1 / 0) + " | " + classOf(() => List(1)(9)) + " | " +
    classOf(() => "abc".toInt) + " | " + report(() => None.get))
```

```text
ArithmeticException: / by zero | IndexOutOfBoundsException | NumberFormatException | NoSuchElementException: None.get
```

The full table:

| What failed | The class you catch |
|---|---|
| integer division or modulo by zero | `ArithmeticException` |
| an index outside a collection | `IndexOutOfBoundsException` |
| an index outside a string | `StringIndexOutOfBoundsException` |
| `"abc".toInt` | `NumberFormatException` |
| `None.get`, `List().head` | `NoSuchElementException` |
| a `match` with no matching case | `MatchError` |
| a failed `asInstanceOf` | `ClassCastException` |
| a member that does not exist | `NoSuchMethodError` (an `Error`) |
| a method call on `null` | `NullPointerException` |
| a bad argument to a runtime method | `IllegalArgumentException` |
| an operation the runtime refuses | `UnsupportedOperationException` |
| recursion past the native stack guard | `StackOverflowError` (an `Error`) |
| the heap ceiling | `OutOfMemoryError` (an `Error`) |
| anything else from protoCore | `RuntimeException` |

The hierarchy is the JVM's without the `java.lang.` prefix (D73), and it is the
small one below — there is no `java` namespace to qualify a name with (D8):

```text
Throwable
├── Exception
│   ├── RuntimeException
│   │   ├── ArithmeticException
│   │   ├── ClassCastException
│   │   ├── IllegalArgumentException
│   │   │   └── NumberFormatException
│   │   ├── IllegalStateException
│   │   ├── IndexOutOfBoundsException
│   │   │   └── StringIndexOutOfBoundsException
│   │   ├── MatchError
│   │   ├── NoSuchElementException
│   │   ├── NullPointerException
│   │   ├── UninitializedFieldError
│   │   └── UnsupportedOperationException
│   └── InterruptedException
└── Error
    ├── NoSuchMethodError
    ├── OutOfMemoryError
    └── StackOverflowError
```

An **`Error` is not an `Exception`**, which matters when you write a broad
handler:

Fixture: [`tests/conformance/tutorial/11-exceptions-error-is-not-an-exception.scala`](../../tests/conformance/tutorial/11-exceptions-error-is-not-an-exception.scala)

```scala
def deep(n: Int): Int = 1 + deep(n + 1)
@main def run(): Unit =
  try
    try println(deep(0))
    catch case e: Exception => println("wrongly caught")
  catch case e: StackOverflowError => println("an Error, not an Exception")
```

```text
an Error, not an Exception
```

Fixture: [`tests/conformance/tutorial/11-exceptions-hierarchy.scala`](../../tests/conformance/tutorial/11-exceptions-hierarchy.scala)

```scala
@main def run(): Unit =
  val e = new ArithmeticException("/ by zero")
  println(e.isInstanceOf[RuntimeException].toString + " " + e.isInstanceOf[Exception] + " " +
    e.isInstanceOf[Throwable] + " " + e.isInstanceOf[Error])
```

```text
true true true false
```

One thing is deliberately **not** catchable: a defect in protoScala itself. A
compiler or VM bug surfaces as `protoscala: internal error: …` and no
`catch { case e: Throwable => … }` can intercept it (D74). A bug in the language
must never be masked by a program written in it.

## 11.6 `Try`, when you want a value instead

`Try` turns the same failure into a value you can pass around:

Fixture: [`tests/conformance/tutorial/11-exceptions-try-the-value.scala`](../../tests/conformance/tutorial/11-exceptions-try-the-value.scala)

```scala
@main def run(): Unit =
  val ok = Try("42".toInt)
  val bad = Try("x".toInt)
  println(ok.toString + " | " + bad.recover(e => -1).map(_ => "recovered").getOrElse("?") +
    " | " + ok.getOrElse(0) + " " + bad.getOrElse(-1))
```

```text
Success(42) | recovered | 42 -1
```

`Failure` carries the `Throwable` itself, so `recover` and `recoverWith` receive
the exception and can pattern-match on it exactly as a `catch` clause does.
Chapter 8 has the rest of the surface.

Which to reach for: a `Try` (or an `Option`, or an `Either`) when the *caller* is
expected to handle the failure and the type should say so; a `throw` when the
failure is genuinely exceptional and most callers should not have to mention it.

## 11.7 Exceptions, actors and `await`

An exception in an actor handler fails **that one message** and leaves the actor
alive with its previous state. The ask's future carries the exception value
itself, so the caller can pattern-match it:

Fixture: [`tests/conformance/tutorial/11-exceptions-in-an-actor.scala`](../../tests/conformance/tutorial/11-exceptions-in-an-actor.scala)

```scala
@main def run(): Unit =
  val counter = Actor.spawn(0) { (s, m) =>
    if m == 0 then throw new IllegalStateException("cannot count zero") else (s + m, s + m)
  }
  val bad = counter ? 0
  while !bad.isCompleted do ()
  val name = bad.value.get match
    case Failure(e) => e.getClass
    case Success(v) => "unexpected"
  val alive = (counter ? 5).await
  println(name + " " + (if alive == 5 then "the actor is alive" else "lost"))
```

```text
IllegalStateException the actor is alive
```

The error is also reported on stderr, so a failure is never silent.

A `catch` body and a `finally` body are **ordinary code**, which means they may
`await`. And an `await` on a future that **failed** raises the carried exception
*at the `await`'s own call site*, so an enclosing `try` catches it and the rest of
the handler still runs:

Fixture: [`tests/conformance/tutorial/11-exceptions-await-a-failed-future.scala`](../../tests/conformance/tutorial/11-exceptions-await-a-failed-future.scala)

```scala
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("cannot count zero") }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      out = (failer ? 1).await.toString
    catch
      case e: IllegalStateException => out = "recovered: " + e.getMessage
    (s, out)
  }
  println((caller ? 1).await)
```

```text
recovered: cannot count zero
```

One limit, and it is a real one: a **suspension is not an abandonment**. While an
actor is parked on a future, the `finally` of the `try` it suspended inside has
not been reached and does not run. If that future never completes, it never
runs (D75): the shutdown diagnostic reports how many actors are parked, and that
is the only notice. Scala has no equivalent situation, and making it otherwise
would mean running arbitrary user code during shutdown.

## 11.8 For the Python or JavaScript developer

| Python | JavaScript | protoScala |
|---|---|---|
| `raise ValueError("x")` | `throw new Error("x")` | `throw new IllegalArgumentException("x")` |
| `try: … except ValueError as e: …` | `try { … } catch (e) { … }` | `try … catch case e: IllegalArgumentException => …` |
| `finally:` | `finally { }` | `finally` |
| `except (A, B):` | `if (e instanceof A \|\| …)` | `case e: A => …` then `case e: B => …` |
| `except A as e: if e.code == 404: …` | the same `if` | `case A(404, p) => …` — a pattern, not a body |
| no value | no value | `try` has a value |
| `else:` clause | — | none: put the code after the `try` |
| exception **groups** (3.11) | `AggregateError` | none |
| `raise … from e` | `{ cause: e }` | `getCause` is always `null` |

Two differences worth internalising. First, a `try` is an **expression** — the
Python idiom of assigning inside both branches is unnecessary. Second, a `catch`
clause is a **pattern**, so the test and the extraction are one thing; there is
no `except` body that has to re-examine the exception and re-`raise` when it turns
out not to be the one it wanted.

## 11.9 What differs from Scala 3 in this area

- **D72** — a `finally` body that itself throws replaces the in-flight exception.
  Scala does the same but warns; protoScala has no warnings.
- **D73** — exception class names are unqualified (`ArithmeticException`, never
  `java.lang.ArithmeticException`), and the hierarchy is the small one of §11.5.
  `catch { case e: java.io.IOException => }` does not compile: there is no `java`
  namespace (D8).
- **D74** — a protoScala defect (a compiler or VM bug) is deliberately **not**
  catchable, so `catch { case e: Throwable }` can never mask one.
- **D75** — an actor suspended on a future that never completes never runs the
  `finally` of the `try` it suspended inside.
- **D85** — `catch someFunction` is accepted and rewritten to
  `case e => someFunction(e)`.
- **D86** — `Throwable.getClass` returns the class's simple name as a `String`;
  protoScala has no `Class[_]` values.
- **D87** — the cleanup a `return` runs is excluded from its own `try`'s handler
  range, which is what makes a throwing cleanup on a `return` path propagate
  correctly. What is not implemented is the multi-level variant: with two or more
  nested `try` constructs, an inner handler may see an exception raised by an
  outer cleanup during a `return`.
- There are **no stack traces**, no `NonFatal`, and no `ControlThrowable`. An
  uncaught exception reports `file:line: error: <Class>: <message>` and exits 1.

Also retired in this area: `Failure` used to carry a `RuntimeError` case class
rather than a `Throwable`, and an `await` on a failed future used to abandon the
rest of the handler. Both are gone; the current behaviour is what §11.6 and §11.7
show.

---

Next: [12. Enums and sealed hierarchies](12-enums-and-sealed-hierarchies.md)
