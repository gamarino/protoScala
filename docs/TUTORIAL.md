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

> **What runs today (protoScala 0.3.0).** Chapters 1, 2, 3, 4, 5, 6, 7, 9, 13
> and 14 describe what the binary runs: values and expressions,
> `val`/`var`/`lazy val`/`def`, `if`/`while`, lambdas and closures, recursion,
> integer and string arithmetic, `println` and the REPL — and, since Phase 2,
> the whole object model: **classes, objects and companions, traits with
> Scala's linearization and stackable `super`, case classes and case objects,
> tuples, `Option`, `List`, pattern matching and for-comprehensions**, in both
> brace and indentation syntax; and, since Phase 5, **actors with three
> priority bands, futures with a cooperative `await`, `Thread` and `System`**.
> Collections beyond `List`, string interpolation, exceptions, `enum`,
> extension methods and modules are planned; their chapters are written together with the phase that implements
> them. Every runnable snippet in the tutorial is a conformance fixture under
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
Chapter 3 you can skim — it documents departures from a language you do not
know yet.

**If you are a Scala programmer.** Skim chapter 2 and read chapter 3
carefully: it lists every departure from Scala 3 on the JVM — no static
typechecker, no implicits, integers that never overflow, no Java interop —
each with its `D<n>` id, plus the provisional behaviours chosen in Phases 1
and 2 (D28–D34: construction of immutable instances, type tests,
uninitialised fields, no overloading, tuple arity, eager `getOrElse`, and the
arity of `{ case … }`). Then use chapters 4, 5, 6, 7, 9 and 14 as a reference
for the details; each of chapters 6, 7 and 9 ends with a section listing what
differs from Scala 3 in its area.

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
| 8 | Collections | *Planned* (Phase 3). |
| 9 | [For-comprehensions](tutorial/09-for-comprehensions.md) | `for … yield` and `for … do`, the rewrite to `flatMap`/`withFilter`/`map`, patterns and value definitions, `Option` and your own types, the placeholder `_`. |
| 10 | Strings and interpolation | *Planned* (Phase 3). |
| 11 | Exceptions | *Planned* (Phase 4). |
| 12 | Enums and sealed hierarchies | *Planned* (Phase 4). |
| 13 | [Actors and futures](tutorial/13-actors-and-futures.md) | Actors, telling and asking, priority bands, futures and their combinators, cooperative `await`, handler failures, threads and time, tuning. |
| 14 | [The REPL and tooling](tutorial/14-repl-and-tooling.md) | The REPL, multi-line input, commands, classes at the prompt, running scripts, error messages, `--disassemble`. |

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
