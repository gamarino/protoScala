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

> **What runs today (protoScala 0.1.0).** Chapters 1, 2, 3, 4, 5 and 14
> describe what the binary runs: values and expressions, `val`/`var`/`lazy
> val`/`def`, `if`/`while`, lambdas and closures, recursion, integer and
> string arithmetic, `println` and the REPL, in both brace and indentation
> syntax. Classes, pattern matching, collections, for-comprehensions, string
> interpolation, exceptions, actors and modules are planned; their chapters
> are written together with the phase that implements them. Every runnable
> snippet in the tutorial is a conformance fixture under
> `tests/conformance/tutorial/`, run with the test suite.

## How to use this tutorial

**If you are a Python or JavaScript developer.** Read the chapters in order.
Chapter 2 is written for you: it maps `const`/`let`, conditional expressions,
arrow functions, closures, indentation-based blocks and arbitrary-precision
integers onto their Scala counterparts. Chapter 3 you can skim — it documents
departures from a language you do not know yet.

**If you are a Scala programmer.** Skim chapter 2 and read chapter 3
carefully: it lists every departure from Scala 3 on the JVM — no static
typechecker, no implicits, integers that never overflow, no Java interop —
each with its `D<n>` id, plus the provisional behaviours chosen in Phase 1.
Then use chapters 4, 5 and 14 as a reference for the details.

## Chapters

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Introduction](tutorial/01-introduction.md) | What protoScala is and is not. Building it. A first program. |
| 2 | [For the Python or JavaScript developer](tutorial/02-for-the-python-or-javascript-developer.md) | `@main`, `val`/`var`, expressions, indentation and braces, lambdas, closures, big integers, types as annotations. |
| 3 | [For the Scala developer](tutorial/03-for-the-scala-developer.md) | What is identical to Scala 3. The departures catalogue (D-ids). What is missing. What is new. |
| 4 | [Values and expressions](tutorial/04-values-and-expressions.md) | Literals, integers and doubles, operators as methods, strings, equality, `if`/`while`, `Unit`. |
| 5 | [Functions and closures](tutorial/05-functions-and-closures.md) | `def`, currying, varargs, lambdas, closures, local recursion, `lazy val`, parameterless `def`. |
| 6 | Classes, objects and traits | *Planned* (Phase 2). |
| 7 | Case classes and pattern matching | *Planned* (Phase 2). |
| 8 | Collections | *Planned* (Phase 3). |
| 9 | For-comprehensions | *Planned* (Phase 2). |
| 10 | Strings and interpolation | *Planned* (Phase 3). |
| 11 | Exceptions | *Planned* (Phase 4). |
| 12 | Enums and sealed hierarchies | *Planned* (Phase 4). |
| 13 | Actors and futures | *Planned* (Phase 5). |
| 14 | [The REPL and tooling](tutorial/14-repl-and-tooling.md) | The REPL, multi-line input, commands, running scripts, error messages, `--disassemble`. |

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
line. The REPL session of chapter 14 is replayed by `tests/cli/tutorial-repl.sh`.
Both run with `ctest`, so the tutorial cannot drift from the implementation.
