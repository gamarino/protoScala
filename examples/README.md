# Examples

Runnable programs. Each one is checked by `ctest -R cli/examples`, which
compares what it prints against the value recorded here, so nothing in this
directory can rot unnoticed.

| Example | Run it | What it shows |
|---|---|---|
| [`hello.scala`](hello.scala) | `protoscala examples/hello.scala` | `@main`, `println` — the smallest complete program |
| [`fib.scala`](fib.scala) | `protoscala examples/fib.scala [n]` | recursion, `@main` arguments, `String*` |
| [`log-report/`](log-report/) | `protoscala examples/log-report/Main.scala [width]` | the worked example: modules and imports, an `enum`, case classes and pattern matching, `Try` and `catch`, `Map` aggregation, `for … yield`, interpolation, and actors with `?` and `await` |

`hello.scala` and `fib.scala` are single files that each show one thing.
`log-report/` is the other kind: several files that show the pieces working
together. Its walkthrough is
[docs/tutorial/worked-example.md](../docs/tutorial/worked-example.md).
