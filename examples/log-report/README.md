# log-report

A small, complete program that turns a log file into one line of report. It
exists because every other example, and every tutorial chapter, shows one
feature at a time; this one shows them together.

## Run it

```bash
cd examples/log-report
protoscala Main.scala
```

```text
Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2
```

There is no build step, no classpath and no `PROTOSCALA_PATH`. `Main.scala`
writes `import report.Levels`, and an import is resolved against the importing
file's own directory first, so the **modules** are found wherever you run it
from.

The **log** is different: it is data, and its path is resolved against the
working directory, here exactly as on the JVM. So from anywhere else, name it:

```bash
protoscala examples/log-report/Main.scala examples/log-report/sample.log
```

Name a path with nothing at it and the program stops with
`FileNotFoundException: <path> (No such file or directory)` rather than
reporting an empty log.

A second argument sets how many parser actors the work is fanned out over:

```bash
protoscala examples/log-report/Main.scala examples/log-report/sample.log 7
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
| `report/Sample.scala` | opens the log and hands back its non-blank lines |
| `sample.log` | the input, committed so the output is deterministic |

## The log is in one place

It was in two. protoScala 0.6.0 had no file I/O, so the log shipped twice — as
the real file, which is what a reader should look at, and again as a string in
`report/Sample.scala`, which is what the program actually parsed — and
`tests/cli/examples.sh` diffed the two so they could not drift apart.

Track F removed the need for all of it. `report/Sample.scala` is now three lines
that open the file:

```scala
def lines(path: String): List[String] =
  Source.fromFile(path).getLines().filter(l => l.trim.nonEmpty)
```

Edit `sample.log` and the report follows; nothing has to be copied and there is
nothing left to keep in step. What `tests/cli/examples.sh` checks instead is that
the file really is being read: it copies `sample.log`, appends one more `ERROR`
line to the copy, runs the program against the copy, and requires a **different**
report. A program that had gone back to carrying its own text would produce the
same one.

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
