# The protoScala Tutorial

> **A dual-audience tutorial.** It teaches protoScala to two audiences at
> once. If you come from **Python or JavaScript**, it introduces Scala from
> first principles — expressions, immutable values, functions, case classes,
> pattern matching, actors — with a constant bridge back to concepts you
> already know. If you come from **Scala on the JVM**, it is a precise, honest
> catalogue of where protoScala agrees with and departs from the Scala 3 you
> know.

The authoritative language reference is [LANGUAGE.md](LANGUAGE.md); the live
tracker of what works and what deviates is [STATUS.md](STATUS.md); the
cross-runtime story is [INTEROP.md](INTEROP.md).

> **What runs today (protoScala 0.0.1).** Nothing yet — the chapters are
> written phase by phase, together with the features they describe. Each
> chapter that mentions a feature not yet implemented opens with a note
> listing it.

## How to use this tutorial

**If you are a Python or JavaScript developer.** Read the chapters in order.
Chapter 2 is written for you: it maps `let`/`const`, functions, arrow
functions, classes, dictionaries, `async`/`await` and indentation-based
syntax onto their Scala counterparts. Chapter 3 you can skim.

**If you are a Scala programmer.** Skim Chapter 2 and read Chapter 3
carefully: it lists every departure from Scala 3 on the JVM (no static
typechecker, no implicits, integers that never overflow, no Java interop,
native actors instead of Akka) and why. Then read the chapters on features
that have no JVM equivalent: actors with priority bands and cooperative
`await` (12) and in-memory polyglot imports (13).

## Chapters

| # | Chapter | Written with |
|---|---|---|
| 1 | Introduction — what protoScala is and is not | Phase 1 |
| 2 | For the Python or JavaScript developer | Phase 1, extended every phase |
| 3 | For the Scala developer — departures catalogue (D-ids) | Phase 1, extended every phase |
| 4 | Values and expressions | Phase 1 |
| 5 | Functions and closures | Phase 1 |
| 6 | Classes, traits and objects | Phase 2 |
| 7 | Case classes and pattern matching | Phase 2 |
| 8 | For-comprehensions | Phase 2 |
| 9 | Collections | Phase 3 |
| 10 | `Option`, `Either` and `Try` | Phase 3 |
| 11 | Exceptions, enums and stackable traits | Phase 4 |
| 12 | Actors and futures | Phase 5 |
| 13 | Modules and polyglot interop | Phase 6 |
| 14 | The REPL and tooling | Phase 1, completed in Phase 6 |
| 15 | Worked example — a small concurrent, polyglot application | Phase 6 |

## Keeping the tutorial honest

Every runnable snippet in a chapter also exists as a conformance fixture under
`tests/conformance/tutorial/NN-<chapter>-<name>.scala` with an `// EXPECT:`
directive, so the tutorial cannot drift from the implementation.
