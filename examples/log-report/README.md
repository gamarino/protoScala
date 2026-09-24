# log-report

A small, complete program that turns a log file into one line of report. It
exists because every other example, and every tutorial chapter, shows one
feature at a time; this one shows them together.

## Run it

```bash
protoscala examples/log-report/Main.scala
```

```text
Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2
```

There is no build step, no classpath and no `PROTOSCALA_PATH`. `Main.scala`
writes `import report.Levels`, and an import is resolved against the importing
file's own directory first, so the example is self-contained wherever you run
it from.

An optional argument sets how many parser actors the work is fanned out over:

```bash
protoscala examples/log-report/Main.scala 7
```

```text
Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2
```

The number must not change the answer — that is the point of the exercise, and
`tests/cli/examples.sh` checks widths 1, 3 and 7 for exactly this reason.

## What it demonstrates

| Feature | Where |
|---|---|
| modules and imports | `Main.scala` imports three sibling modules; `report/Lines.scala` imports `report/Levels.scala` by its bare name |
| an `enum` with methods | `report/Levels.scala` — `Level` carries `label` and `atLeast`, and its declaration order fixes the report's column order |
| case classes and pattern matching | `Line(level, source, message)`, matched in the tally actor's handler |
| `Try` | `Lines.parse` answers `Try[Line]`, so a bad line is a value, not a crash |
| `catch` | `Main.scala` calls `.get` on the result and catches the parser's own exception |
| `Map` aggregation | the tally actor's state is an immutable `Map[String, Int]` |
| `for … yield` | building the parser pool and the list of pending answers |
| string interpolation | the report line itself |
| actors, `?` and `await` | `?` fans every line out to a parser; `await` folds the answers back in source order |

## The files

| File | Role |
|---|---|
| `Main.scala` | the `@main`: fans the parse out, folds the counts back, prints the report |
| `report/Levels.scala` | the severity scale: the cases, their labels, their order |
| `report/Lines.scala` | `case class Line` and the parser that answers `Try[Line]` |
| `report/Sample.scala` | the log as text (see below) |
| `sample.log` | the input, committed so the output is deterministic |

## Why the log is in two places

protoScala 0.5.0 has **no file I/O**: a program cannot open `sample.log` for
itself. So the log ships twice — as the real file, which is what a reader
should look at, and as a string in `report/Sample.scala`, which is what the
program actually parses.

That duplication is a hazard, so it is checked rather than trusted:
`tests/cli/examples.sh` diffs the text between the two `"""` markers in
`report/Sample.scala` against `sample.log` and fails if they have drifted. Edit
`sample.log`, copy the lines into the module, and the expected report in
`tests/cli/examples.sh` will tell you what changed.

## The malformed lines

`sample.log` contains two lines that do not parse, and they are there on
purpose: without them the `Try` in the parser and the `catch` in `Main.scala`
would be decoration rather than code that runs.

```text
*** truncated by logrotate ***
ERROR
```

The first has an unrecognised severity; the second has a severity and nothing
else. Both raise an `IllegalArgumentException` inside `Try`, both come back as
a `Failure`, and both land in the `malformed 2` at the end of the report.

## The walkthrough

[docs/tutorial/worked-example.md](../../docs/tutorial/worked-example.md)
explains why each file is written the way it is, and what the same program
would look like on the JVM and in Python.
