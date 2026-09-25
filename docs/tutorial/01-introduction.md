# 1. Introduction

This chapter says what protoScala is and what it is not, shows how to build
it, and runs a first program. Everything in it runs on the current binary.

## 1.1 What protoScala is

protoScala is a **dynamic dialect of Scala 3** that runs on
[protoCore](https://github.com/numaes/protoCore), the object kernel shared by
protoPython, protoJS, protoST and protoClojure. It reads ordinary Scala 3
source — with braces or with significant indentation — and runs it directly.

It is deliberately *not* a Java Virtual Machine and *not* a port of `scalac`:

- there is **no JVM**, no `.class` files, no sbt or Maven, and no Java library;
- there is **no static typechecker**: type annotations are parsed and then
  erased, so a type error surfaces when the program runs (deviation D4,
  chapter 3);
- integers **never overflow**: `Int`, `Long` and `BigInt` are one
  arbitrary-precision integer (D1).

What it offers instead is the direction set out in
[DESIGN.md §1](../DESIGN.md): start-up fast enough for scripts and REPL work,
immutable data backed by protoCore's persistent structures, real concurrency
without a global interpreter lock, and in-memory interop with the other
protoCore languages. Phase 1 delivers the core language and the REPL; the rest
arrives phase by phase (see [ROADMAP.md](../ROADMAP.md)).

## 1.2 Building

protoScala needs a C++20 compiler, CMake 3.20 or later, libreadline, and a
built protoCore in a sibling directory (or an installed one passed with
`-DPROTO_CORE_PREFIX=<prefix>`):

```bash
# protoCore, once
cd ../protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

# protoScala
cd ../protoScala
cmake -B build_release -S .
cmake --build build_release
ctest --test-dir build_release
./build_release/protoscala --version
```

The rest of the tutorial writes `protoscala` for `./build_release/protoscala`.

## 1.3 Your first program

Save this as `hello.scala`:

Fixture: [`tests/conformance/tutorial/01-introduction-hello.scala`](../../tests/conformance/tutorial/01-introduction-hello.scala)

```scala
@main def hello(): Unit =
  println("Hello, protoScala!")
```

Prints:

```text
Hello, protoScala!
```

Run it with `protoscala hello.scala`. The `@main` annotation marks the entry
point: after the file's top-level definitions are set up, protoscala calls the
`@main` method. The body is indented under the `=`, the Scala 3 way; braces
work too (chapter 2, §2.4).

Older Scala material writes the entry point as an object instead, and that works
here too:

Fixture: [`tests/conformance/tutorial/01-introduction-hello-app.scala`](../../tests/conformance/tutorial/01-introduction-hello-app.scala)

```scala
object Hello extends App:
  println("Hello, protoScala!")
```

Initialising the object *is* the program: nothing references `Hello`, and its body
runs. `App` is deprecated in Scala 3 in favour of `@main`, so prefer `@main` in
new code; a file may use one or the other, never both (D104).

## 1.4 A program with a definition

Fixture: [`tests/conformance/tutorial/01-introduction-first-program.scala`](../../tests/conformance/tutorial/01-introduction-first-program.scala)

```scala
def answer(): Int = 6 * 7
@main def run(): Unit =
  println("The answer is " + answer())
```

Prints:

```text
The answer is 42
```

`def answer(): Int = 6 * 7` defines a method whose body is a single
expression; `: Int` is its declared result type, which protoScala reads and
erases. `"The answer is " + answer()` concatenates a string and an integer,
exactly as in Java, JavaScript or Scala on the JVM.

## 1.5 A first look at the REPL

Run `protoscala` with no arguments to get an interactive session:

```text
protoScala X.Y.Z REPL — :help for commands, :quit or Ctrl-D to exit
scala>
```

(`X.Y.Z` is the version the binary reports with `--version`.)

Type definitions and expressions and they are evaluated as soon as they are
complete. Chapter 14 walks through a whole session.

## 1.6 What works today and where to look

Phase 1 covers values and expressions, `val`/`var`/`lazy val`/`def`,
`if`/`while`, lambdas and closures, recursion, integer, double and string
arithmetic, `println`, `@main` and the REPL. Classes, pattern matching,
collections, for-comprehensions, string interpolation, exceptions, actors and
modules come in later phases.

- [STATUS.md](../STATUS.md) is the live tracker of what works and of every
  deviation from Scala 3 (the `D<n>` ids).
- [LANGUAGE.md](../LANGUAGE.md) is the language reference.
- [DESIGN.md](../DESIGN.md) explains how the runtime is built.

Next: [chapter 2](02-for-the-python-or-javascript-developer.md) if you come
from Python or JavaScript, [chapter 3](03-for-the-scala-developer.md) if you
come from Scala.
