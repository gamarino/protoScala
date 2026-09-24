# A worked example: a log report

> Part of [the protoScala tutorial](../TUTORIAL.md). Every chapter before this
> one shows a feature on its own, with a fixture small enough to fit in a
> paragraph. **This is the one place where the pieces are shown working
> together**, in a program that has a job to do rather than a point to make.
>
> The program is [`examples/log-report/`](../../examples/log-report/), it runs
> unchanged from a clean checkout, and `tests/cli/examples.sh` compares its
> output against the line printed below.

## The problem

A log file arrives. Each line names a severity, a subsystem and a message,
separated by spaces:

```text
INFO auth user ada signed in
WARN cache eviction rate 0.83 above threshold
ERROR db connection refused by replica 2
```

Some lines are not like that, because logs are written by programs under stress
and read by programs that were not there when it happened. A truncated line, a
severity nobody recognises, a line with a level and nothing after it: all of
them are normal, and none of them should stop the report.

We want one line of output — how many lines of each severity, in severity
order, plus how many lines could not be read at all:

```text
Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2
```

That single deterministic line is what makes the example testable, and it is
the reason the sample log is committed rather than generated: an example whose
output nobody checks is an example that has already rotted.

## The shape of the solution

Four files and one sample:

```text
examples/log-report/
├── Main.scala           the @main: fan out, fold back, print
├── report/Levels.scala  the severity scale
├── report/Lines.scala   the record and the parser
├── report/Sample.scala  the log, as text
├── sample.log           the log, as a file
└── README.md
```

The work splits into three jobs that do not need to know much about each other.
**What the severities are and how they order** is one decision, and it lives in
`Levels.scala`. **What a parsed line is, and what happens when a line does not
parse** is another, and it lives in `Lines.scala`. **How the work is spread out
and put back together** is the third, and that is `Main.scala`.

The parsing is fanned out over a pool of actors, folded back into one actor
that owns the counts, and read once at the end. That is more machinery than
fourteen lines of log deserve, and it is deliberate: the interesting question
is not whether actors are fast here (they are not — the pool costs more than
the work), it is whether the answer stays the same when the work is spread out.
`tests/cli/examples.sh` runs the program with fan widths 1, 3 and 7 and demands
the same line from all three.

### Running it

```bash
protoscala examples/log-report/Main.scala
```

```text
Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2
```

No build tool, no classpath, no `PROTOSCALA_PATH`. `Main.scala` writes
`import report.Levels`, and a module path is resolved against the **importing
file's own directory** first, then against `PROTOSCALA_PATH`, then against the
working directory. The first of those is what makes an example directory
self-contained: the program finds its own modules because they are next to it,
not because the shell was standing in the right place. It runs identically from
the repository root, from your home directory or from `/`.

---

## `report/Levels.scala` — the scale

```scala
enum Level:
  case Debug, Info, Warn, Error

  def label: String = toString.toUpperCase
  def atLeast(other: Level): Boolean = this.ordinal >= other.ordinal

def fromLabel(label: String): Level =
  Level.values.find(l => l.label == label) match
    case Some(l) => l
    case None    => throw new IllegalArgumentException("unknown level '" + label + "'")
```

The whole file exists so that **the severity order is written down exactly
once**, and an `enum` is the cheapest way to write it down: the declaration
order *is* the order, `ordinal` reads it back, and `values` hands it over as a
list. The report at the end of `Main.scala` iterates `Level.values` and
therefore prints its columns in severity order without naming a single level.
Adding a `Fatal` case here adds a column to the report and changes nothing else
— a property worth a file of its own, because the alternative is a comparator
in one place, a list of names in another, and a slow divergence between them.

`label` is derived from the case name rather than written out per case, so a
new case is readable from the log the moment it is declared. That is the kind
of derivation worth taking even when it saves only four lines: the two things
that must agree — the case and the text in the log — now cannot disagree.

`atLeast` is a method on the enum rather than an expression at a call site for
the same reason. That the ordinals increase with severity is a fact about this
declaration, and a caller that writes `a.ordinal >= b.ordinal` has taken a copy
of it. [Chapter 12](12-enums-and-sealed-hierarchies.md) §12.4 covers methods in
an enum body; §12.5 covers the `sealed trait` spelling, which is what to reach
for when a case needs its own parents or its own name.

`fromLabel` is where the file is allowed to fail. It could have answered an
`Option[Level]` and pushed the decision outwards; it throws instead, because the
caller is `Lines.parse`, which is already collecting failures into a `Try`, and
one failure channel is easier to reason about than two. The exception carries
the offending label, so a malformed line could be reported individually if the
program ever wanted to.

Note what the enum is **not** given: exhaustiveness checking. The `match` in
`fromLabel` is on an `Option`, and the one in the tally actor is on a message;
neither is checked, and a case forgotten in either is a run-time `MatchError`
(D4). That is the standing trade of this dialect, and §12.3 states it plainly.

## `report/Lines.scala` — the record and the parser

```scala
import Levels.{Level, fromLabel}

case class Line(level: Level, source: String, message: String)

def parse(raw: String): Try[Line] = Try {
  val fields = raw.split(" ").toList.filter(f => f.nonEmpty)
  if fields.length < 3 then
    throw new IllegalArgumentException("expected LEVEL SOURCE MESSAGE, got '" + raw.trim + "'")
  Line(fromLabel(fields.head), fields(1), fields.drop(2).mkString(" "))
}
```

Two decisions in nine lines.

**The import is `Levels`, not `report.Levels`.** This file sits in `report/`, and
resolution starts at the importing file's directory, so `report.Levels` would
look for `report/report/Levels.scala` and miss. A sibling module is imported by
its bare name. This is the one place where the rule has a visible consequence,
and it is worth internalising: a module path is relative to the file that writes
it, not to the program's entry point and not to the shell.

**`Line` is a `case class`, not a tuple or a list of fields.** The tally actor's
handler writes `case Line(level, _, _)`, which is only possible because the
record has a name and an extractor; with a three-element list it would be
writing `fields(0)` and hoping. Naming the three fields costs one line and buys
a pattern that says what it matches.

**`parse` answers `Try[Line]` rather than throwing.** Inside the block anything
may throw freely — a short line throws directly, an unknown label throws inside
`fromLabel` — and `Try` turns the first throw into a `Failure` carrying the
exception itself. The reason to return a value rather than propagate is that
*this file does not know what a bad line costs*. In a report it costs a
counter; in a validator it would abort the run. The parser's job is to say
precisely what happened; the policy belongs to the caller, and putting it there
is what lets the same parser serve both.

The `split` here is a plain substring split, not a regular expression (D70:
there is no regex engine in this dialect), so runs of spaces are handled by
splitting on one space and filtering the empties out. That is less elegant than
a pattern and, for a fixed-shape log line, entirely sufficient.

## `report/Sample.scala` — the log, and an honest workaround

```scala
val text: String = """
INFO auth user ada signed in
…
ERROR
"""

def lines: List[String] = text.split("\n").toList.filter(l => l.trim.nonEmpty)
```

protoScala 0.5.0 has **no file I/O**. A program cannot open `sample.log`, so a
worked example that reads a log has to carry the log inside itself. The example
ships it twice: `sample.log` is the real file, which is what a reader should
look at and what a future version of this example will actually open, and
`report/Sample.scala` is the copy the program parses.

Duplication that nobody checks is duplication that drifts, so this one is
checked. `tests/cli/examples.sh` extracts the text between the two `"""` markers
and diffs it against `sample.log`; if they differ, `ctest` fails and names the
difference. That is the general shape of the compromise worth making when a
platform is missing something: take the workaround, write down why, and make the
part that can rot fail loudly when it does.

The sample contains two lines that do not parse — `*** truncated by logrotate
***` and a bare `ERROR` — and they are there so that the `Try` in the parser and
the `catch` in `Main.scala` are code that runs rather than code that decorates.
An error path with no test is not an error path.

## `Main.scala` — fanning out and folding back

### The parser pool

```scala
val width = if args.isEmpty then 3 else args(0).toInt

val parsers = for i <- (0 until width).toList yield
  Actor.spawn(i) { (id, line) => (id, parse(line.toString)) }

val answers = for pair <- Sample.lines.zipWithIndex yield
  parsers(pair._2 % width) ? pair._1
```

Two `for … yield` comprehensions, and the second one is where the concurrency
happens. `?` queues a message and returns a `Future` **immediately**, so the
comprehension does not wait for anything: by the time it finishes, every line is
in some parser's mailbox and every parser is working. The list it yields is a
list of promises, in the order the lines were read.

The parsers hold their own index as state and never use it. An actor must have a
state, and an index is the most honest thing to give a worker that has none —
giving it a counter it never reads would suggest it mattered.

Round-robin rather than chunking, because chunking by position would make the
distribution depend on how the log happens to be laid out. Neither matters at
fourteen lines; the habit matters.

### The tally

```scala
val tally = Actor.spawn(Map[String, Int]()) { (counts, msg) =>
  msg match
    case Line(level, _, _) =>
      val key = level.label
      (counts.updated(key, counts.getOrElse(key, 0) + 1), ())
    case Report => (counts, counts)
}
```

One actor owns the counts. That is the whole concurrency design, and it is why
there is no lock anywhere in this program: the runtime guarantees that only one
message of an actor is being handled at a time, so `counts.updated(…)` cannot
interleave with another `counts.updated(…)`. The `Map` is immutable, so the
handler does not mutate the state — it returns the next one, and the runtime
installs it.

The handler matches on the message, which is what lets one actor carry a small
protocol: a `Line` is absorbed and answered with `()`, a `Report` is answered
with the counts. `case object Report` is the whole definition of that second
message — a value with a name is all a protocol marker needs to be, and using a
string `"report"` instead would put the marker in the same space as the data.

Counting under `level.label` rather than under `level` itself is deliberate.
The key is what the report looks the count up by, and a string key keeps the
map's behaviour obvious rather than resting on how case objects hash.

### The fold, and the malformed lines

```scala
var malformed = 0
var i = 0
while i < answers.length do
  try tally ! answers(i).await.get
  catch case e: IllegalArgumentException => malformed = malformed + 1
  i = i + 1
```

This loop is where the program becomes deterministic again. The parsers ran in
whatever order the pool gave them, but the answers are *awaited in source
order*, so the tallies are queued in source order too — and because messages on
one priority band are handled in the order they were queued, the counts are
built in a fixed order regardless of how the parsing interleaved. Determinism
here is not an accident of a small input; it is a property of where the `await`
is.

`.get` on a `Failure` re-raises the parser's own exception, which is what turns
the `Try` back into something a `catch` can handle. That is the deliberate seam
between the two error styles chapter 11 describes: a `Try` while the failure is
being carried across a boundary, an exception at the point where a decision is
finally made about it. Here the decision is "add one to `malformed` and keep
going", and it is made once, in one place.

`while` and two `var`s rather than a `fold`, because `await` inside a native
higher-order method such as `List.foreach` is exactly the case D43 does not
support. This loop runs on the main thread, where `await` simply blocks and the
restriction does not apply — but writing it as a loop keeps the example honest
about the shape that works everywhere, including inside an actor.

### The report

```scala
val counts = (tally ? Report).await

val columns = for l <- Level.values yield s"$l ${counts.getOrElse(l.label, 0)}"
println(s"${columns.mkString(" | ")} | malformed $malformed")
```

The `Report` ask is sent after every tally, on the same priority band, so it is
handled after every tally: the map it returns has seen every line. Had it gone
out on `Priority.High` it could have overtaken the queue, and the report would
have been a snapshot of whenever it happened to be served.

The last two lines are the only place the output format is decided, and they
name no level. `Level.values` supplies both the set and the order, `getOrElse(…, 0)`
supplies the zero for a level the log never used, and interpolation puts them
together. A severity that appears in `Levels.scala` and never in the log still
gets its column, which is usually what a report should do — a missing `Error`
row and an `Error 0` row say very different things.

---

## The same program on the JVM

For the Scala reader, the differences are mostly things that are *absent*.

**No build tool.** There is no `build.sbt`, no `project/`, no `target/`. The
program is run by pointing the interpreter at a file, in the way you would run
a Python script. Nothing is compiled ahead of time and nothing is packaged.

**No classpath, and modules instead of packages.** On the JVM, `report/Levels.scala`
would start with `package report`, and the connection between the directory and
the package name would be a convention that the build tool enforces by putting
`target/classes` on a classpath. Here the file path *is* the module path, and it
is resolved from the importing file. `import Levels` inside `report/Lines.scala`
is not a package-local shortcut — it is a different path, relative to a
different directory, and it means "the file next to me". A module is also an
**object** (D91): the file's top-level definitions become its members, which is
why `import report.Levels.Level` reads like importing from an object
rather than from a package.

**A module runs when it is imported** (D90), like Python's, not lazily on first
access. It runs exactly once per file, however many paths reach it.

**No static types.** Every type annotation in this example is documentation.
`case class Line(level: Level, source: String, message: String)` accepts
anything in any position; `Level` in that signature is never checked. The
`match` in the tally handler is not checked for exhaustiveness either (D4) —
a message that is neither a `Line` nor `Report` is a run-time `MatchError`,
where scalac would have warned about the case you left out. In exchange there is no type
inference to fight and no variance annotation to get right, and a `sealed trait`
whose cases carry different shapes needs no ceremony at all. The trade is real
and it goes both ways: write the test you would otherwise have got from the
compiler.

**No Akka, no `ExecutionContext`, no `Await.result`.** `Actor.spawn` is in the
prelude, the pool starts on the first spawn, `?` gives a plain `Future`, and
`await` takes no timeout. There is no supervision: a handler that throws fails
that one message and leaves the actor alive with its previous state.
[Chapter 13](13-actors-and-futures.md) lists the whole surface.

**Integers do not overflow**, so a count is never wrong because it got large.
And there is no file I/O at all, which is the one absence this example had to
work around rather than around which it could design.

## The same program in Python

For the Python or JavaScript reader, three things are genuinely different.

**Actors instead of threads or `asyncio`.** The parser pool is not a
`ThreadPoolExecutor` and not an event loop. There is no GIL — two parsers run at
the same time on two cores — and there is no `async`/`await` colouring, because
`parse` is an ordinary function that knows nothing about concurrency. What
replaces the lock around the counters is not a lock: the tally actor handles one
message at a time by construction, so the `Map` update in its handler has no
other writer to race with. The Python equivalent of that guarantee is a
`threading.Lock` you must remember to take; here you cannot forget it, because
there is nothing to take.

A message is a pointer, not a copy. `parsers(i) ? line` hands the parser the
very same string object, and the parser hands back the very same `Line`. Nothing
is pickled and nothing is structured-cloned, which is safe only because values
are immutable.

**Immutability by default.** `counts.updated(key, n)` does not modify `counts` —
it answers a new map that shares almost all of its structure with the old one.
The habit that breaks first coming from Python is `d[k] = v`; the habit that
replaces it is returning the new value, which is exactly what an actor handler
is asked to do. `Sample.lines` is a `List`, not a Python list: you cannot append
to it, and `for l <- Level.values yield …` is a comprehension rather than a loop
that mutates an accumulator.

**`match` instead of an `isinstance` chain.** The tally handler's

```scala
msg match
  case Line(level, _, _) => …
  case Report            => …
```

is what Python 3.10's `match` was modelled on, and what a chain of

```python
if isinstance(msg, Line):
    level = msg.level
elif msg is REPORT:
    ...
```

was before it. The pattern tests the shape and binds the parts in one step, and
`_` says out loud which parts are not used. Two things to watch: an unmatched
message raises `MatchError` at run time and nothing warns you at compile time
(D4), and a case class's `==` is structural, as `@dataclass`'s is, while a bare
`case object` compares by identity.

**Failures are values until you decide otherwise.** `Try(...)` is a `try/except`
that hands you the outcome instead of forcing you to handle it where it
happened — closer to returning `(ok, err)` than to catching. And `Try` is not
`Optional`: a `Failure` carries the exception itself, so the `catch` in
`Main.scala` sees the very `IllegalArgumentException` the parser raised, with
its message intact.

---

## What to take from it

- **One decision, one place.** The severity order is in `Levels.scala`, the
  failure policy is in the loop in `Main.scala`, the output format is in the
  last two lines. Each of them can be changed without reading the others.
- **Let the caller decide what a failure costs.** `parse` answers a `Try`; the
  report decides that a bad line is worth a counter.
- **Put the `await` where determinism is needed.** The work ran out of order;
  the answers were consumed in order, and that was enough.
- **Check the thing that can rot.** The expected report is in
  `tests/cli/examples.sh`, the duplicated log is diffed there, and three fan
  widths must agree. An example nobody runs is documentation with no evidence.

---

Back to [the tutorial index](../TUTORIAL.md) · the program itself is
[`examples/log-report/`](../../examples/log-report/).
