# The protoScala Tutorial

> **A dual-audience tutorial.** It teaches protoScala to two audiences at
> once. If you come from **Python or JavaScript**, it introduces Scala 3 from
> first principles — expressions, immutable values, functions and closures,
> and later classes, pattern matching and actors — with a constant bridge back
> to concepts you already know. If you come from **Scala on the JVM**, it is a
> precise, honest catalogue of where protoScala agrees with and departs from
> the Scala 3 you know.

The authoritative language reference is [LANGUAGE.md](LANGUAGE.md). The live
tracker of what works and of every deviation is [STATUS.md](STATUS.md). How the
runtime is built is in [DESIGN.md](DESIGN.md); the cross-runtime story is in
[INTEROP.md](INTEROP.md).

> **What runs today (protoScala 0.6.0).** Every chapter describes what the binary
> runs: values and expressions, `val`/`var`/`lazy val`/`def`, `if`/`while`,
> lambdas and closures, recursion, integer and string arithmetic, `println` and
> the REPL — and the whole object model: **classes, objects and companions,
> traits with Scala's linearization and stackable `super`, case classes and case
> objects, tuples, `Option`, `List`, pattern matching and for-comprehensions**,
> in both brace and indentation syntax; **actors with three priority bands,
> futures with a cooperative `await`, `Thread` and `System`**; **the collection
> library — `List`, `Vector`, `Range`, `Map`, `Set`, `Either` and `Try` —
> string interpolation and the `String` method surface**; **`try`/`catch`/
> `finally` and `throw` with pattern-matched handlers, the `Throwable`
> hierarchy, `enum` and sealed hierarchies, named and default arguments,
> extension methods (and the custom string interpolators they bring),
> `super[T].m`, templates nested in an `object`, and multiple constructor
> parameter lists**; and, new in 0.6.0, **modules: a `.scala` file reached by its
> path, all five `import` forms, a selector that names a type — so a case class
> from another file can be destructured in a `match` here — and the four
> polyglot prefixes `py.`, `js.`, `st.` and `clj.` routed to protoCore's
> provider registry**. Chapter 15 covers all of it, including the honest limit:
> the prefixes route, but no runtime in the family registers `py`, `js` or `clj`
> yet, so those imports stop with `no provider registered for '<alias>'`. Also
> new: **file input and output** — `scala.io.Source` for reading, and a
> four-operation `FileIO` object for writing, because Scala's writer is
> `java.io.PrintWriter` and there is no Java here. Chapter 16 covers it. Newest of
> all: **the `Predef` surface** — `assert`, `require`, `assume` and `???`, with
> Scala's exception types and Scala's exact messages (chapter 11 §11.6), and
> **`object Main extends App`** as an entry point beside `@main` (chapter 1 §1.3).
> Every runnable snippet in the tutorial is a conformance fixture under
> `tests/conformance/tutorial/`, run with the test suite.

## How to use this tutorial

**If you are a Python or JavaScript developer.** Read the chapters in order.
Chapter 2 is written for you: it maps `const`/`let`, conditional expressions,
arrow functions, closures, indentation-based blocks and arbitrary-precision
integers onto their Scala counterparts, and its last three sections introduce
classes and traits, pattern matching and comprehensions before chapters 6, 7
and 9 take each one in full. Those three are the ones with no close
counterpart in the languages you know, so they carry the most bridging prose:
chapter 6 compares traits with Python's MRO and JavaScript mixins, chapter 7
compares `match` with Python's `match` and JavaScript's `switch`, and chapter
9 compares `for … yield` with list comprehensions and `flatMap` chains.
Chapter 8 then maps `dict`, `set`, `list` and `range()` onto `Map`, `Set`,
`List`/`Vector` and `Range`, and chapter 10 maps f-strings and template
literals onto `s"…"` and `f"…"`. Chapter 15 is the one to read when your
program outgrows a single file: it maps Python's `import`/`from … import` and
JavaScript's `import`/`require` onto protoScala's five forms, and covers the one
form neither language has — importing a **type**, which is what lets a case
class defined in another file be destructured in a `match` here. Chapter 3 you
can skim — it documents departures from a language you do not know yet.

**If you are a Scala programmer.** Skim chapter 2 and read chapter 3
carefully: it lists every departure from Scala 3 on the JVM — no static
typechecker, no implicits, integers that never overflow, no Java interop —
each with its `D<n>` id, plus the provisional behaviours chosen in Phases 1
and 2 (D28–D34: construction of immutable instances, type tests,
uninitialised fields, no overloading, tuple arity, eager `getOrElse`, and the
arity of `{ case … }`), and those chosen in Phase 3 (D54–D71: the
interpolation strategy, the `f` conversion set, `Map`/`Set` iteration order,
sequence hashing, `Range` bounds, `sorted` without an `Ordering`, no
`collect`, no `Seq`/`Iterable`, and `split`). Then use chapters 4, 5, 6, 7, 8,
9, 10 and 14 as a reference for the details; each of chapters 6, 7, 8, 9 and 10
ends with a section listing what differs from Scala 3 in its area. Read chapter
15 early rather than last: a module here is a **file**, not a package — there is
no `package` clause and no classpath — and its D90–D96 are the choices that
follow from that, including a module's top level running at import and an
`import` being hoisted to its whole unit. Chapter 16 is the other one to read
early: reading a file is `scala.io.Source` and behaves like it, writing is not a
`PrintWriter` and says why, and its D97-D102 are the choices that follow.

## Chapters

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Introduction](tutorial/01-introduction.md) | What protoScala is and is not. Building it. A first program. |
| 2 | [For the Python or JavaScript developer](tutorial/02-for-the-python-or-javascript-developer.md) | `@main`, `val`/`var`, expressions, indentation and braces, lambdas, closures, big integers, types as annotations, and a first look at classes, `match` and comprehensions. |
| 3 | [For the Scala developer](tutorial/03-for-the-scala-developer.md) | What is identical to Scala 3. The departures catalogue (D-ids). What is missing. What is new. |
| 4 | [Values and expressions](tutorial/04-values-and-expressions.md) | Literals, integers and doubles, operators as methods, strings, equality, `if`/`while`, `Unit`. |
| 5 | [Functions and closures](tutorial/05-functions-and-closures.md) | `def`, currying, varargs, lambdas, closures, local recursion, `lazy val`, parameterless `def`. |
| 6 | [Classes, objects and traits](tutorial/06-classes-objects-and-traits.md) | Classes and constructors, mutable fields, `object` and companions, traits, abstract members, linearization and stackable `super`, privacy, `apply` and `update`. |
| 7 | [Case classes and pattern matching](tutorial/07-case-classes-and-pattern-matching.md) | Case classes, tuples, `Option`, algebraic data types, every pattern form, extractors, `MatchError`. |
| 8 | [Collections](tutorial/08-collections.md) | `List` and its full surface, `Vector`, `Range`, `Map`, `Set`, `Option` as a collection, `Either` and `Try`, conversions, and what immutability buys. |
| 9 | [For-comprehensions](tutorial/09-for-comprehensions.md) | `for … yield` and `for … do`, the rewrite to `flatMap`/`withFilter`/`map`, patterns and value definitions, `Option` and your own types, the placeholder `_`. |
| 10 | [Strings and interpolation](tutorial/10-strings-and-interpolation.md) | Literals and triple-quoted strings, `s"…"`, `raw"…"`, `f"…"` and its specifier table, `stripMargin`, `format`, and the `String` method surface. |
| 11 | [Exceptions](tutorial/11-exceptions.md) | `throw` and the `Throwable` hierarchy, `try`/`catch` as an expression, `catch` as a pattern match, `finally`, what the runtime raises on its own, `assert`/`require`/`assume`/`???` and which exception each raises, `Try` as the value-oriented alternative, exceptions across an actor turn and a suspended `await`. |
| 12 | [Enums and sealed hierarchies](tutorial/12-enums-and-sealed-hierarchies.md) | `enum` with simple and parameterised cases, `ordinal`/`values`/`valueOf`/`fromOrdinal`, enums as algebraic data types, `sealed trait` hierarchies, and why exhaustiveness is not checked. |
| 13 | [Actors and futures](tutorial/13-actors-and-futures.md) | Actors, telling and asking, priority bands, futures and their combinators, cooperative `await`, handler failures, threads and time, tuning. |
| 14 | [The REPL and tooling](tutorial/14-repl-and-tooling.md) | The REPL, multi-line input, commands, classes at the prompt, running scripts, error messages, `--disassemble`. |
| 15 | [Modules and polyglot interop](tutorial/15-modules-and-polyglot-interop.md) | A module is a file, the five import forms, where modules are found, importing a type, the four polyglot prefixes and what each needs, named arguments across the boundary, and what each failed import prints. |
| 16 | [Reading and writing files](tutorial/16-reading-and-writing-files.md) | `Source.fromFile`, `getLines()` and `mkString`, how lines are counted, the `FileIO` writing surface, every failure and the class it raises, and the two deliberate divergences from Scala's `Source`. |
| — | [**Worked example**](tutorial/worked-example.md) | One complete program, `examples/log-report/`, rather than one feature at a time: three files and two imports, an `enum` with methods, case classes and pattern matching, `Try` and a `catch` for a malformed line, `Map` aggregation, `for … yield`, interpolation, and actors fanned out with `?` and folded back with `await`. |

## Running the examples

```bash
$ protoscala file.scala [args...]       # run a file: top-level statements, then @main
$ protoscala                            # interactive REPL
$ protoscala --version                  # print the version
$ protoscala --disassemble file.scala   # print the compiled bytecode
```

## Keeping the tutorial honest

Every runnable snippet in a chapter is, verbatim, the body of a conformance
fixture `tests/conformance/tutorial/NN-<chapter>-<name>.scala` (NN is the
chapter number), and the output printed under it is the fixture's `// EXPECT:`
line. A snippet shown as failing is a fixture with an `// EXPECT-ERROR:`
directive, and the message quoted under it is the one the binary prints. The
REPL sessions of chapter 14 are replayed by `tests/cli/tutorial-repl.sh`.
Both run with `ctest`, so the tutorial cannot drift from the implementation.
