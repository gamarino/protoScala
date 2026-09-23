# 14. The REPL and Tooling

> **Implementation status.** The local REPL, script running, `--disassemble`
> and `--version` work today. Editor integration and packaging arrive in
> Phase 6.

## 14.1 Starting the REPL

Run `protoscala` with no arguments. It prints a banner and the primary prompt
`scala>`; while an input is incomplete it shows the continuation prompt
`     |` (aligned under the primary prompt). On a terminal the REPL uses
readline, so the arrow keys edit the line and recall history.

## 14.2 A session

A short session. Each input is typed at `scala>`; the lines starting with
`val` or `def` right after an input are the REPL's echo:

```text
scala> val greeting = "Hello"
val greeting = "Hello"
scala> def shout(s: String) = s.toUpperCase + "!"
def shout
scala> shout(greeting)
val res0 = "HELLO!"
scala> def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)
def fact
scala> fact(20)
val res1 = 2432902008176640000
scala> def sumTo(n: Int): Int = {
     |   var total = 0
     |   var i = 1
     |   while i <= n do { total += i; i += 1 }
     |   total
     | }
def sumTo
scala> sumTo(100)
val res2 = 5050
scala> :quit
```

This exact session is replayed by `tests/cli/tutorial-repl.sh` on every test
run, so the echoes above are checked against the binary.

The echo format:

| Input | Echo |
|---|---|
| `val x = …` | `val x = <value>` |
| `var x = …` | `var x = <value>` |
| `lazy val x = …` | `lazy val x` (the value is not computed yet) |
| `def f…` | `def f` |
| an expression | `val resN = <value>`, numbered from `res0`; the result can be used later as `resN` |
| an expression of value `()` | nothing |

A `String` value is shown in double quotes (`val res0 = "HELLO!"`), as the
Scala 3 REPL does; other values print as `toString` does. Only inputs that
bind a result use up a number: an input that fails, or whose value is `()`,
leaves the next `resN` free. Unlike the Scala REPL, echoes carry no types:
types are erased (D4).

*Redefinitions.* Defining a name again shadows the earlier definition, as in
the Scala REPL: code compiled before keeps the binding it saw.

```text
scala> val x = 1
val x = 1
scala> def f = x
def f
scala> val x = 2
val x = 2
scala> f
val res0 = 1
```

The new definition may be of another kind (a `def` redefined as a `val`, a
`val` as a `lazy val`) without affecting earlier code. An input that fails
defines nothing: after `val z = 1 / 0` reports its `ArithmeticException`, `z`
is still undefined (or keeps its earlier value).

## 14.3 Multi-line input

An input is evaluated as soon as it is complete. The REPL keeps reading
continuation lines, at the `     |` prompt, while:

- the input is not complete yet: a line ending in `=` (or another token that
  cannot end an expression, such as `+` or `=>`), an open brace, parenthesis
  or bracket, a `then` or `do` still waiting for its body;
- the last line is indented deeper than the input's first line, or ends with
  a token that opens a block (`=`, `then`, `do`, `else`, `:`, ...), so more
  indented lines may follow.

The input ends on an empty line, at the end of input, or when a line returns
to the first line's column. A line at that column that continues the
construct — `else`, `end while`, `then`, `do`, a closing brace — is part of
the input; any other line starts the next input. So a loop can be typed line
by line:

```text
scala> var i = 0
var i = 0
scala> while i < 3 do
     |   println(i)
     |   i += 1
     |
0
1
2
scala> def next(x: Int) =
     |   val y = x
     |   y + 1
     |
def next
```

An empty line at the continuation prompt also forces evaluation of an input
that is not complete, which then reports the parse error.

One pitfall remains: `if c then a` written on one line is already a complete
expression, so the REPL evaluates it before you can type `else` on the next
line. Put `else` on the same line, or break the line after `then`.

## 14.4 Commands

Commands are typed at the primary prompt:

| Command | Effect |
|---|---|
| `:help` | show the commands |
| `:quit`, `:q` | exit (Ctrl-D also exits) |
| `:load <file>` | run a file in the current session; its definitions stay available |

In an interactive session the input history is saved to
`~/.protoscala_history` and reloaded next time. When input is piped in (as in
`tests/cli/tutorial-repl.sh`), history is neither read nor written.

## 14.5 Running scripts

`protoscala file.scala [args...]` runs a file: first its top-level
definitions and statements, in order, then its `@main` method. Arguments after
the file name are passed to `@main` when it declares `args: String*`:

Fixture: [`tests/conformance/tutorial/14-repl-script-args.scala`](../../tests/conformance/tutorial/14-repl-script-args.scala)

```scala
@main def run(args: String*): Unit =
  println("args: " + args.length)
```

Prints:

```text
args: 0
```

Run without arguments it prints `args: 0`; `protoscala 14-repl-script-args.scala a b`
prints `args: 2`.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | the program ran to completion |
| 1 | a parse, compile or run-time error (or a file that cannot be opened) |
| 2 | an unknown command-line option |

## 14.6 Error messages

Errors go to standard error, in one of two forms.

Parse and compile errors carry the line and column:

```text
e1.scala:2:1: error: unexpected end of input: expression expected but 'EOF' found
```

Run-time errors carry the line and the error class (without the `java.lang.`
prefix — provisional, chapter 3):

```text
e2.scala:3: error: ArithmeticException: / by zero
```

In the REPL the file name is `<console>`, and an error does not end the
session.

## 14.7 `--disassemble` and `--version`

`protoscala --version` prints the version (`protoScala X.Y.Z`) and exits.
`protoscala --help` lists the options.

`protoscala --disassemble file.scala` compiles the file without running it and
prints its bytecode, one function per block, with the source line of each
instruction. For a file containing `def f(x: Int) = x + 1`, the entry for `f`
reads:

```text
  function f arity=1 locals=0 stack=2 captures=0
    0000  L1  PUSH_LOCAL 0
    0001  L1  PUSH_CONST 0 ; 1
    0002  L1  ADD 0
    0003  L1  RETURN 0
```

It is the first thing to look at when a program behaves unexpectedly: it shows
what the compiler understood, before any question of run-time behaviour.

## 14.8 Classes at the REPL

Classes, objects, traits and case classes can be defined at the prompt, and a
template that spans several lines is read like any other multi-line input
(§14.3): the indented lines continue it, and an empty line ends it.

```text
scala> case class Point(x: Int, y: Int)
// defined case class Point
scala> val p = Point(1, 2)
val p = Point(1,2)
scala> p.copy(y = 5)
val res0 = Point(1,5)
scala> trait Shape:
     |   def area: Double
     |
// defined trait Shape
scala> class Sq(s: Double) extends Shape:
     |   def area = s * s
     |
// defined class Sq
scala> new Sq(3.0).area
val res1 = 9.0
```

This session is replayed by `tests/cli/tutorial-repl.sh` on every test run, so
the echoes above are checked against the binary.

A definition of a class, trait or object echoes `// defined <kind> <name>`
rather than a value, as the Scala 3 REPL does, and the companion a case class
synthesises is not announced separately. Values then print with the user's own
`toString`, so `p` shows as `Point(1,2)`.

Redefinition follows the rule of §14.2: a second `case class Point(x: Int)`
shadows the first, and values created before it keep their original class, so
an old `p` still prints `Point(1,2)`. A *pattern* typed afterwards, though,
names the new class — `p match { case Point(a, b) => … }` then reports
`wrong number of arguments for pattern Point: expected 1, found 2`. Re-create
the value after redefining its class. The wider set of cases — `object` with a mutable field, `match` at the prompt,
pattern `val`s, `for … yield` — is exercised by `tests/cli/repl-classes.sh`.
