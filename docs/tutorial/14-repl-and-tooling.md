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
val greeting = Hello
scala> def shout(s: String) = s.toUpperCase + "!"
def shout
scala> shout(greeting)
val res0 = HELLO!
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

Unlike the Scala REPL, echoes carry no types: types are erased (D4).

## 14.3 Multi-line input

An input is evaluated as soon as it is complete. The REPL keeps reading
continuation lines while it is not:

- a line ending in `=` (or another token that cannot end an expression, such
  as `+` or `=>`) continues on the next line, and the indented lines that
  follow form the body;
- an open brace, parenthesis or bracket continues until it is closed, as with
  `sumTo` above;
- an empty line at the continuation prompt forces evaluation of what has been
  typed so far.

One pitfall: `if … then …` is already a complete expression at the end of a
line, so the REPL evaluates it before you can type `else` on the next line
(the `else` then fails to parse on its own). Put `else` on the same line, or
wrap the whole `if` in braces.

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
