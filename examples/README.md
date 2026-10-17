# Examples

Runnable programs. Each one is checked by `ctest -R cli/examples`, which
compares what it prints against the value recorded here, so nothing in this
directory can rot unnoticed.

| Example | Run it | What it shows |
|---|---|---|
| [`hello.scala`](hello.scala) | `protoscala examples/hello.scala` | `@main`, `println` — the smallest complete program |
| [`fib.scala`](fib.scala) | `protoscala examples/fib.scala [n]` | recursion, `@main` arguments, `String*` |
| [`column.scala`](column.scala) | `./column.scala N` | a filter run as a command: the `#!/usr/bin/env protoscala` line, `StdIn.readLine`, a usage error on standard error with exit status 2 |
| [`sum.scala`](sum.scala) | `./sum.scala` | a filter: the total of the integers on standard input, exit status 1 on a bad line; `printf 'pump-1 12\npump-2 30\n' \| ./column.scala 2 \| ./sum.scala` prints 42 |
| [`log-report/`](log-report/) | `protoscala examples/log-report/Main.scala [width]` | the worked example: modules and imports, an `enum`, case classes and pattern matching, `Try` and `catch`, `Map` aggregation, `for … yield`, interpolation, and actors with `?` and `await` |

`hello.scala` and `fib.scala` are single files that each show one thing.
`column.scala` and `sum.scala` are executable scripts (make them executable
with `chmod +x`) meant to be chained in a pipeline; `ctest -R cli/shebang`
runs them that way.
`log-report/` is the other kind: several files that show the pieces working
together. Its walkthrough is
[docs/tutorial/worked-example.md](../docs/tutorial/worked-example.md).
