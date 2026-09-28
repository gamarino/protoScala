# protoScala

> **A dynamic Scala 3 dialect on the protoCore object kernel — fast start-up, persistent immutable data by default, GIL-free native actors, and a polyglot module system built on protoCore's Unified Module Discovery.**

protoScala is a language runtime of the [protoCore](https://github.com/numaes/protoCore) family, alongside [protoST](https://github.com/gamarino/protoST) (Smalltalk), [protoClojure](https://github.com/gamarino/protoClojure) (Clojure), [protoJS](https://github.com/gamarino/protoJS) (JavaScript) and [protoPython](https://github.com/gamarino/protoPython) (Python).

It is **not** a JVM replacement: there is no JVM, no sbt/Maven, no Java interop and no static typechecker. It runs Scala 3 source — braces or significant indentation — with types parsed and erased, on a runtime that aims to:

- **start fast and small** — a < 25 ms, ~20 MB RSS *target*, so Scala is viable for scripts and REPL-driven work. The **time half is met by median and straddles by worst sample**; the **memory half is not met**. `cold-start.sh` exits 0: 23.32 ms script / 23.72 ms REPL on the installed 0.6.0 binary at load ≈ 1.9 (31 runs, all verified, 2026-09-27), and 21.63 / 22.32 ms in the quiet-window run of 2026-09-26 (378 runs at 0.55 busy CPUs). But the tail is wide — the slowest of those 31 runs was 37.9 ms — and `run_benchmarks.py`, which judges per sample rather than per median, still reads **script STRADDLES**. RSS measures **≈ 24.1 MB** against the ~20 MB target. See [Performance](#performance);
- map **case classes and functional collections** onto protoCore's persistent, structurally shared data;
- run **native actors without a GIL**: messages are pointers to immutable data, mailboxes are lock-free with three priority bands, and `await` inside an actor suspends cooperatively instead of blocking a thread;
- **load modules through protoCore's Unified Module Discovery.** `import util.Shapes` loads a `.scala` module and its classes are usable as *types* — `new Point(1, 2)`, `case Point(x, y)` — because an import is resolved when the file is compiled. protoScala also *registers itself* as a UMD provider (`provider:scala`), and the four family prefixes `py.`, `js.`, `st.` and `clj.` route to their providers. Across that boundary there is no serialization and no adapter, by construction: a value is a protoCore object on both sides, and a named argument arrives in the callee's `keywordParameters` unchanged — and `import st.<module>` now proves it across a real runtime boundary, with the same object's address printed from both runtimes. **What no runtime in the family registers is a `py`, `js` or `clj` provider**, so `import py.numpy as np` compiles, routes, and reports `ImportError: no provider registered for 'py'` — see [What polyglot interop does and does not do today](#what-polyglot-interop-does-and-does-not-do-today).

protoScala is also a **platform validation project**: protoCore aims to be a solid base for implementing any language, and each new language exposes missing capabilities. Those capabilities are added to protoCore as part of this project — the first two are `ProtoMap`, a persistent map with GC-traced object keys, and `ProtoMPSCQueue`, a lock-free GC-traced actor mailbox; both also serve protoClojure and protoST.

## A flavour of the language

Every line below runs on 0.6.0 today.

```scala
case class Increment(by: Int)
case object GetValue

val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}

counter ! Increment(10)
println((counter ? GetValue).await)          // 10
println(Future(6 * 7).await)                 // 42 — Future takes its body by name

val squares = for x <- List(1, 2, 3) yield x * x
println(squares)                              // List(1, 4, 9)

val stock = Map("apples" -> 3, "pears" -> 0)
val restocked = stock + ("pears" -> 12)       // a new Map; `stock` is unchanged
println(restocked.toList.sortBy(_._1))        // List((apples,3), (pears,12))

val name = "Ada"
println(s"Hello, $name!")                     // Hello, Ada!
println(f"pi is ${3.14159}%.2f")              // pi is 3.14

def factorial(n: Int): Int = if n == 0 then 1 else n * factorial(n - 1)
println(factorial(100).toString.length)       // 158 — integers never overflow

enum Shape:
  case Circle(r: Double)
  case Square(side: Double)

def area(s: Shape): Double = s match
  case Shape.Circle(r)    => 3.14 * r * r
  case Shape.Square(side) => side * side
println(area(Shape.Square(2.0)))              // 4.0

def volume(width: Int, height: Int = 1, depth: Int = 1): Int = width * height * depth
println(volume(depth = 3, width = 2))         // 6 — named and default arguments

extension (n: Int) def squared: Int = n * n
println(3.squared)                            // 9

println(try "x".toInt catch case e: NumberFormatException => -1)   // -1
```

## protoScala in 10 minutes — for Scala programmers

Install a package (`.deb` or `.tar.gz` — neither needs `LD_LIBRARY_PATH`), or
build from source; then run a script or start the REPL:

```
$ protoscala hello.scala
$ protoscala
scala> 1 + 1
val res0 = 2
```

Five things are different, and you will hit all five in the first ten minutes.

**1. There is no static typechecker.** Type annotations are parsed and erased, so
a type error is not a compile error:

```scala
def add(a: Int, b: Int): String = a + b
println(add(1, 2))                            // 3 — no complaint about String
println("abc".lenght)                         // NoSuchMethodError at run time
```

The second line fails when it runs, naming the method and the receiver. That is
the trade: the platform is late-binding, so detection is late, but it is never
silent.

**2. Integers never overflow.** `Int`, `Long` and `BigInt` are one arbitrary-
precision type (D1):

```scala
println(2147483647 + 1)                       // 2147483648, not -2147483648
def factorial(n: Int): Int = if n == 0 then 1 else n * factorial(n - 1)
println(factorial(30))                        // 265252859812191058636308480000000
```

**3. There are no implicits and no givens** (D3). `given`, `using` and implicit
conversions are refused at the point they are written:

```scala
given x: Int = 1                              // error: implicits and givens are
                                              // not supported (D3)
```

Type classes, `Ordering`, `ExecutionContext` and everything built on them are
therefore absent. Extension methods exist and are session-wide (D82).

**4. There is no Java, and the exception hierarchy is unqualified names.**

```scala
println(new java.io.File("x"))                // error: Not found: type java.io.File

try throw new IllegalStateException("bad")
catch case e: Exception => println(e.getMessage)    // bad
```

`catch { case e: java.io.IOException => }` does not compile: there is no `java`
namespace (D8, D73). Write `catch case e: IOException` — the classes are there,
under the JVM's names with the prefixes dropped.

Where you feel the absence is **writing a file**. Reading is `scala.io.Source`
and behaves as you expect; there is no `PrintWriter` and no `Files`, so writing is
protoScala's own four operations (D102):

```scala
FileIO.write("notes.txt", "one\ntwo\n")
val lines = Source.fromFile("notes.txt").getLines()   // a List[String], not an Iterator
println(lines.mkString(" | "))                        // one | two
```

**5. Modules instead of packages.** There is no `package` clause and no
classpath. A `.scala` file *is* a module, reached by its path, and an import is
resolved when the file is compiled — which is why an imported class is usable as a
type:

```scala
import util.Shapes.{Point, area}
println(area(Point(3, 4)))                    // 12
```

The full catalogue of departures, keyed to the `D<n>` ids, is
[tutorial chapter 3](docs/tutorial/03-for-the-scala-developer.md).

## protoScala in 10 minutes — for Python and JavaScript developers

Install a package, or build from source; then run a script or start the REPL:

```
$ protoscala hello.scala
$ protoscala
scala> 1 + 1
val res0 = 2
```

Six things are new if you are coming from Python or JavaScript.

**1. `val` and immutability by default.** `val` is a binding you cannot reassign —
`const`, or a name you agree not to rebind. Collections go further: they are
persistent, so an "update" returns a *new* collection and the old one is
unchanged and still cheap, because the two share their structure:

```scala
val stock = Map("a" -> 1)
val more = stock + ("b" -> 2)
println((stock.size, more.size))              // (1,2) — `stock` never changed
```

There is no defensive copying to remember, and no aliasing bug to find.

**2. Almost everything is an expression.** `if`, `match` and a block all have a
value, so there is no separate ternary and no "assign in every branch":

```scala
println(if 2 > 1 then "yes" else "no")        // yes
```

**3. `match` replaces the chain of `isinstance`.** It destructures while it
tests:

```scala
case class P(x: Int, y: Int)
val p = P(1, 2)
println(p.copy(y = 9))                        // P(1,9)
println(p match { case P(x, _) => x })        // 1
```

**4. Actors instead of threads and `async`.** An actor owns its state; you send
it a message and it processes one at a time, on real OS threads with no global
lock. `?` asks and returns a future; `await` waits for one:

```scala
val counter = Actor.spawn(0)((state, msg) => (state + msg, state + msg))
counter ! 5
println((counter ? 0).await)                  // 5
println(Future(6 * 7).await)                  // 42
```

Inside an actor, `await` *suspends the turn* rather than blocking the thread, so
an actor waiting on a future does not consume one.

**5. Integers with no ceiling, and no floats pretending to be integers.**

```scala
println(2147483647 + 1)                       // 2147483648
```

**6. Reading a file, which you will try early.** There is no `with` and no
`open()`; there is `Source`, and a missing file raises rather than returning
`None` or `undefined`:

```scala
FileIO.write("shopping.txt", "milk\nbread\n")
println(Source.fromFile("shopping.txt").getLines().mkString(", "))   // milk, bread
println(try Source.fromFile("gone.txt").mkString
        catch case e: IOException => e.getClass)                     // FileNotFoundException
```

`getLines()` gives you a `List[String]` with no trailing empty entry, which is the
bug `"…".split("\n")` leaves you in Node. [Tutorial chapter
16](docs/tutorial/16-reading-and-writing-files.md) puts all of it beside `open()`
and `fs.readFileSync`.

The bridge from Python and JavaScript, concept by concept, is
[tutorial chapter 2](docs/tutorial/02-for-the-python-or-javascript-developer.md).

## What polyglot interop does and does not do today

The headline is a real mechanism and a partial reach, and it is worth being exact
about which is which, because the interesting half is the part that is missing
from *other* repositories rather than from this one.

**What you can run right now.** A protoScala module is a `.scala` file reached by
its path. Put this in `util/Shapes.scala`:

```scala
case class Point(x: Int, y: Int)
def area(p: Point): Int = p.x * p.y
```

and this beside it:

```scala
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))   // 12
```

`Point` is usable as a **type** — in `new`, in a type test, in an extractor
pattern — because an `import` is resolved when the file is compiled, not when it
runs. Every form works: `import util.Shapes`, `… as S`, `… .{a, b as c}`,
`… .*`, `… ._`. Modules are found in the importing file's directory, then on
`PROTOSCALA_PATH`, then in the working directory, and a miss names every path it
tried.

`import` also means what it means in ordinary Scala — taking names out of
something already in scope, with no file involved:

```scala
enum Colour:
  case Red, Green
import Colour.*
@main def run(): Unit = println(Red)   // Red
```

The rule that tells the two apart: the longest dotted prefix of the path that
names something **already in scope** wins and the import reads its members;
otherwise the path is a file to load. That is the shape Scala's own resolution
has, where a definition in scope shadows a package of that name. See
[LANGUAGE.md §3.2](docs/LANGUAGE.md).

**What routes but has nowhere to go.** The four family prefixes are wired to
protoCore's provider registry:

```
$ cat try.scala
import py.numpy as np
@main def run(): Unit = println(np)

$ protoscala try.scala
try.scala:1:1: error: ImportError: no provider registered for 'py'. Install the
runtime that provides it, or point PROTOSCALA_PROVIDERS at its plug-in
```

That message is the current, honest state of `import py.numpy as np`, and it is
unchanged by Track Y: `st` is the prefix that now has a provider, `py`, `js` and
`clj` still do not. protoScala routes the prefix; what is missing is the provider.
protoPython registers the UMD aliases `native`, `python_stdlib`, `compiled` and
`hpy` — none of them `py`. Two things block it beyond the alias, both read from
protoPython's source rather than assumed: its module machinery resolves the
environment from a **thread-local** (`PythonEnvironment::fromContext` returns
`s_threadEnv`) rather than from the caller's context, and its ProtoSpace is a
process singleton by design (`PythonEnvironment::getProcessSpace`, a function-local
static, "L-Shape: one per process") — which a co-resident protoScala `Session`,
owning its own space, contradicts. That is not a provider-local fix like protoST's;
it is DESIGN R5's question. protoJS and protoClojure register no provider at all.
The coordination is tracked as **Track Y** in [docs/ROADMAP.md](docs/ROADMAP.md)
and it is cross-repository work, not a protoScala change.

**What is proved rather than promised.** The boundary itself is built and
exercised. `protoscala` loads provider plug-ins by `dlopen` from
`PROTOSCALA_PROVIDERS` and from `<prefix>/lib/protoscala/providers`, and
`tests/conformance/25-interop/` drives a real one: a module is imported through a
prefix, a member is selected from it, a **named argument crosses the boundary**
and arrives in the callee's `keywordParameters` with no adapter — including one
whose parameter name is too long to embed in a pointer word, which is where a
naive implementation fails silently — and a C++ exception thrown inside the
provider is caught at the Scala call site as a `RuntimeException` with its message
intact. There is no serialization anywhere in that path, because a value is a
protoCore object on both sides.

**A cross-runtime import works, and nothing is copied.** `import st.<module>`
from protoScala loads a protoST module and its values are usable. Phase 6 measured
this as a miss and diagnosed it correctly: `ModuleProvider::tryLoad(path, ctx)`
receives the *caller's* context, and protoST resolved its own runtime from
`ctx->space` — a space it does not own when the caller is another runtime. The fix
is in the provider and needed **no protoCore change**: a provider is an object with
its own state, so it takes its runtime from that state and uses `ctx` only to
allocate the result in the caller's context.

What that buys is the property the rest of this section claims, now verified
across a real runtime boundary rather than a plug-in of protoScala's own:

```
NO-COPY PROOF
  protoScala space   = 0x7ffec21cb4c0
  protoST space      = 0x57d25533c440
  Counter via Scala  = 0x743e4cf7e9c0  getHash(scalaCtx) = 127810928110016
  Counter via ST     = 0x743e4cf7e9c0  getHash(stCtx)    = 127810928110016
```

Two object spaces, one object. That is
`tests/unit/protost_interop.cpp` (`umd/protost-interop`, built whenever protoST is
found beside this tree) reading the same protoST class twice — once out of the
namespace protoScala received, once out of protoST's own globals, each through its
own context with its own interned symbol — and printing both addresses so the run
can be reproduced. Cloning the value at the boundary turns that test red.

protoScala also *is* a provider: it registers `provider:scala` so another runtime
can import a `.scala` module. What is demonstrated is one importer, on one thread,
reading values; §6 of [docs/INTEROP.md](docs/INTEROP.md) lists what is not, and R5
in [docs/STATUS.md](docs/STATUS.md) is still the maintainer's call.

## Project status

**Phase 6 complete (version 0.6.0); Phase 7 shipped as a first cut, with the
version deliberately not bumped — not production ready, open for community
review.** Phase 5 was implemented before Phases 3 and 4, so the minor version went
0.2.0 → 0.3.0 (actors) → 0.4.0 (collections) → 0.5.0 (exceptions, enums,
arguments and extensions) → 0.6.0 (modules, UMD and packaging).

**Phase 7 is the C++ transpiler, and it is what §"Producing a UMD module" and
§Building below are documenting**: `protoscalac`, `libprotoScala.so.1`,
`CompiledModuleProvider`, and the cross-runtime call — a protoScala function
becomes a `proto::ProtoMethod`, which any runtime in the family can call, and
`interop/foreign-call` proves it from a translation unit that names no protoScala
symbol. It is called a first cut because two of its own done-when rows are unmet:
the mutation matrix is partial, and **the start-up regression is not measured**
([docs/ROADMAP.md](docs/ROADMAP.md) §Phase 7 lists both, and what is still open).
Everything it added sits under `## [Unreleased]` in
[CHANGELOG.md](CHANGELOG.md) for that reason. Until 2026-09-27 this section said
"every phase the roadmap named is now closed" and the string "Phase 7" appeared
nowhere in this file.

What remains beyond the phases is listed in
[docs/ROADMAP.md](docs/ROADMAP.md) as tracks. **Track Y**
delivered the first working cross-runtime import (`import st.<module>`); what it
did not deliver is `import py.numpy`, which needs work in protoPython rather than
here. **Track F** added file input and output, so a program can read its own
input; the worked example now opens `sample.log` instead of carrying a second copy
of it. The binary runs Scala 3 scripts and offers a REPL,
in both brace and significant-indentation syntax:

- `val`/`var`/`lazy val`/`def`, `if`/`while`, lambdas and closures, placeholder
  syntax (`_ + 1`), recursion (with `StackOverflowError` instead of a crash),
  `println`;
- **classes** with `val`/`var`/plain constructor parameters, auxiliary
  constructors, `extends`/`with`, abstract members, `override`, `final`,
  `sealed`, `private` (enforced as a lookup restriction);
- **objects and companions**, **case classes** and **case objects** with
  `apply`, `unapply`, `equals`, `hashCode` (bit-equal to the JVM's),
  `toString`, `copy`, `productArity`/`productElement`/`productPrefix`,
  `_1`..`_N`;
- **traits** with Scala's linearization installed as protoCore parent chains,
  trait parameters, and `super` — stackable traits included;
- **tuples** `Tuple2`..`Tuple22`, the universal `apply` rule for receivers that
  have an `apply` (a case class, or a companion that defines one), `update`,
  generated setters and method values;
- **pattern matching** with every pattern of the design (literals, wildcards,
  variables, typed, constructor, tuple, `::`, `List(a, rest*)`, alternatives,
  binders, stable identifiers, custom extractors, guards), pattern `val`s and
  `{ case ... }` literals, `isInstanceOf`/`asInstanceOf`;
- **by-name parameters** `x: => T` on a `def`, a method or a plain constructor
  parameter: the argument is not evaluated at the call and runs once per read in
  the body (D53 records where the compiler cannot resolve the call site);
- **for-comprehensions** (generators, guards, value definitions, patterns,
  `yield` and `do`) over `List`, `Option` and any class with
  `map`/`flatMap`/`withFilter`/`foreach`, with a lazy `withFilter`;
- **the collections**: the full `List` surface, `Vector`, `Range`, and `Map` and
  `Set` on protoCore's `ProtoMap` with GC-traced keys — all immutable, all
  sharing structure, so `m + (k -> v)` is a new map that shares almost
  everything with the old one. `List(1,2) == Vector(1,2) == (1 to 2)`, and all
  three hash alike, so a `Map` keyed by one is found by another;
- **`Option`/`Some`/`None`, `Either`/`Left`/`Right` and `Try`** from a prelude
  written in protoScala, with the combinators you expect (`map`, `flatMap`,
  `fold`, `getOrElse`, `recover`, `toEither`, …);
- **string interpolation**: `s"…"`, `f"…"` with the printf specifiers, and
  `raw"…"`;
- **actors and futures, without a GIL**: `Actor.spawn(state)(handler)`, `!`,
  `?`, three priority bands, `Actor.stats`; `Future` with `await`, `map`,
  `flatMap`, `recover` and `Future(e)` — the body is taken by name; a worker
  pool of real OS threads
  with lock-free mailboxes and ready stacks; and an `await` inside a handler
  that **suspends cooperatively** — the worker is released, so an actor that
  asks another actor completes even with a single worker;
- `Try`/`Success`/`Failure`, `Thread.start`/`join` and
  `System.nanoTime`/`currentTimeMillis`/`getenv`;
- **exceptions**: `try`/`catch`/`finally` and `throw`, with a `catch` body that is
  a full pattern match, a twenty-class `Throwable` hierarchy in the prelude, and
  every failure the runtime raises translated into a real exception value of the
  class its name means — including across an actor turn and a suspended `await`,
  so `try { f.await } catch { … }` works;
- **`enum` and sealed hierarchies**: simple and parameterised cases, `ordinal`,
  `values`, `valueOf`, `fromOrdinal`, and enums as algebraic data types;
- **named and default arguments** for methods, constructors, case-class `apply`
  and `copy`, function values and local functions — bound in the callee through
  protoCore's own keyword convention, which is the same one a foreign callee will
  receive them by (`docs/INTEROP.md` §7);
- **extension methods**, dispatched on the receiver's runtime prototype, and the
  **custom string interpolators** they bring;
- **`super[T].m`**, templates nested in an `object`, and multiple constructor
  parameter lists;
- **file input and output**: `scala.io.Source` for reading — `fromFile`,
  `fromString`, `getLines()`, `mkString`, `close()`, with Scala's own line-splitting
  rules verified against `scalac` — and `FileIO.write`/`append`/`exists`/`delete`
  for writing, which is protoScala's own surface because Scala's is
  `java.io.PrintWriter` (D97–D102). Every failure raises the class the JVM raises,
  with a message naming the path: a missing file, a directory where a file was
  expected, no permission, and bytes that are not valid UTF-8.

```scala
val counter = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
counter ! 1                      // tell: queue a message, return at once
println((counter ? 41).await)    // ask:  a Future of the reply  ->  42

val sink = Actor.spawn(0) { (state, msg) => state + msg }   // no reply to give
println(Future(6 * 7).await)     // Future takes its body by name  ->  42
```

Everything in the list above is implemented; the sentence that used to stand here
said otherwise and had outlived four phases. For the exact boundary — what is
implemented, what is not, and every deviation with its `D<n>` id — see
[docs/STATUS.md](docs/STATUS.md); [docs/ROADMAP.md](docs/ROADMAP.md) lists what
remains, as tracks rather than phases. The largest absences today are a `py`, `js`
or `clj` provider, nested classes in a `class` or `trait`, and anything about files
beyond reading and writing one whole text file (no directories, no binary files, no
streaming).

### Conformance: what is measured, and against whose tests

**protoScala's own suite.** **2313 registered ctest cases** as of **2026-09-27**,
against protoCore 2.5.0 (`df8406a3`). Reproduce with
`ctest --test-dir build_release -N | tail -1` and
`ctest --test-dir build_release < /dev/null`; the figure moves with every fixture
added, and with protoCore's rule list, so prefer the command to the number. The
result is not a claim from one manual run: continuous integration
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) builds protoCore, protoST
and protoScala from clean on `ubuntu-24.04` and runs the suite on every push. On
the current `main` it registered the same **2313**, ran **2310** — the three
clock-dependent cases are excluded there and run in a second job — and reported
**0 failed, 7 skipped**; the skips are the embedder-conformance rules that need
process isolation. The count printed here until 2026-09-27 was **1344**, which
predated Phase 7's differential harness: `PROTOSCALA_TRANSPILED_TESTS` is `ON` by
default and registers one `transpiled/<fixture>` case per conformance fixture, so
the suite is 922 cases larger than that number said. **Every one of those tests was
written in this repository**, which is the limit that matters: they measure
faithfulness to the implementers' model of Scala, not to Scala.

**The Scala 3 compiler's own tests.** So protoScala is also measured against tests
nobody here wrote — the single-file programs under `tests/run` in the Scala 3
(dotty) compiler, at corpus commit `a68b419c`, **1654** of them, each scored by
dotty's own rule (the program must run and its output must match its `.check` file).
Measured 2026-09-25 against the tree above:

| Denominator | Passing | What the denominator is |
|---|---|---|
| **601 in scope** | **191 — 31.8 %** | our own triage, described below |
| **1654 whole corpus** | **216 — 13.1 %** | no triage at all |

**The 601 is our triage and it is not a standard.** Scala has no standards-body
conformance suite — there is no equivalent of JavaScript's Test262 — so this is the
reference implementation's own test corpus, and the in-scope bucket is a judgement
made in this repository, with one named rule recorded per test rather than any
wildcard exclusion. Of the 1654: **410 are out by design**, because they test a JVM
or Java surface protoScala has no equivalent for (95 reflection / `ClassTag` /
`Mirror`, 83 `java.*` interop, 62 `System` / `Console`, 56 `classOf`, 49
`getClass`, 22 `scala.compiletime`, 15 Java threads, 8 serialization, 6
`synchronized`, and 14 whose expected output is a JVM-specific class name or stack
trace); and **643 are out for now**, being roadmap rather than boundary (197
implicits and givens, 153 `inline` and macros, 77 lazy collections and `Iterator`,
75 `Seq` and mutable collections, 61 `Array`, 39 nested or anonymous classes, 23
`PartialFunction`, and 18 others). A reader who does not accept the triage should
read the 13.1 %; a reader who wants to know how much roadmap is left should read the
643.

**What the corpus found that our own documents had not.** On the run of 2026-09-25,
**257** of the corpus's disagreements with `scalac` were anticipated by no deviation
recorded here; re-running the same instrument against the tree at protoCore 2.5.0
the same day gives **236**, the difference being the fixes that landed in between.
The largest single cause — six missing `Predef` names — cost 103 in-scope tests and
was invisible to a suite that had never needed them. Both figures, how they are
derived and the per-test attribution are in [docs/STATUS.md](docs/STATUS.md)
("Track S deviations") and [docs/DECISIONS-LOG.md](docs/DECISIONS-LOG.md).

These are low rates and they are published to be read as such. protoScala's aim is
an agile, interoperable, easily integrable and very simple Scala — not a complete
one and not a fast one. The corpus is here to find disagreements we had not
anticipated, and that count, not the percentage, is the number worth watching.

## Performance

protoScala is positioned as an agile, interoperable, easily integrable and
very simple Scala — **not a fast one.** Beating the JVM on integer loops, or
matching protoClojure's raw actor throughput, are explicit non-goals (see
[docs/DESIGN.md](docs/DESIGN.md) §1). Where the numbers below show protoScala
losing, they are reported as measured, not adjusted or explained away.

**Both tables in this section were re-measured on 2026-09-26 on a genuinely
quiet host** — 0.55–0.59 busy CPUs of 12, verified with `mpstat -P ALL` before
each run — superseding figures taken at 3.1–9.1. The quiet window did **not**
flatter protoScala: the general suite's geomean against CPython moved from
0.91× to **1.06×**, and every one of its twelve ratios moved against
protoScala, because CPython gained more from the quiet host than protoScala did
(§ *The general suite*). That is reported as measured.

This machine is the maintainer's daily-driver desktop (AMD Ryzen 5 5500U, 6
physical / 12 logical CPUs); the quiet window was arranged, not typical. Every
runtime is sampled round-robin (one sample of column A, then B, then C, … then
back to A) so ambient load hits every column alike, and every cell below reports
its median **and** `[min-max]` spread — ratios between columns are the primary
result, absolute milliseconds/msg-per-second are indicative only. Every cell
verifies the work it did before a rate is computed; a cell that cannot be
verified is published as `FAILED`, never as a fast time. Full report:
[`benchmarks/reports/2026-09-26-quiet-window.md`](benchmarks/reports/2026-09-26-quiet-window.md).

### Actors (0.3.0)

Interleaved re-run: 2026-09-23, same machine, **CAS-list mailboxes**
(protoCore 2.0.0 has no `ProtoMPSCQueue` yet), load average 2.96 at start,
9.51 at midpoint, 9.68 at end. All 42 protoScala cells (7 modes × 6 worker
counts) and 24 protoClojure comparison cells verified their own message
counts before any rate was computed, 5 samples per cell, protoScala and
protoClojure sampled back to back at every (mode, workers) pair so the
comparison is same-machine, same-moment. Full report, with every cell's
spread:
[benchmarks/reports/2026-09-23-actors-v2.md](benchmarks/reports/2026-09-23-actors-v2.md)
(the superseded single-sample run is kept at
[benchmarks/reports/2026-09-23-actors.md](benchmarks/reports/2026-09-23-actors.md)).

**Re-measured 2026-09-26 on a quiet host** (0.59 busy CPUs of 12; idle 94.30 /
95.49 / 95.35 %), protoScala `f835aee`, protoCore `da5f19e2` (2.5.0),
protoClojure `39d8353`, **ProtoMPSCQueue mailboxes** — the superseded table below
it was measured on CAS-list mailboxes at load 3.59→9.09. 5 samples per cell,
protoScala and protoClojure sampled back to back at every (mode, workers) pair,
every cell verifying its own message count. The mode set is the current seven:
`MPSC` and `MPMC` were replaced by the two CPU-bound `saturation-*` modes.

| Mode | peak msg/s (median) | at | protoClojure at the same worker count | ratio |
|---|---:|---|---:|---:|
| single | 212,406 [210,807-230,738] | 2 workers | 257,742 [241,538-268,202] | **0.82×** |
| fan-out | 243,108 [233,633-252,268] | 2 workers | 483,679 [450,546-493,075] | **0.50×** |
| ping-pong | 26,939 [26,298-27,588] | 4 workers | no twin | — |
| await | 67,271 [66,136-71,312] | 6 workers | no twin | — |
| priority | 157,089 [153,404-160,404] | 2 workers | no twin | — |
| saturation-8 | 2,947 [2,567-3,034] | 12 workers | 2,679 [1,939-3,020] | **1.10×** |
| saturation-32 | 3,373 [3,178-3,402] | 6 workers | 2,874 [2,655-2,979] | **1.17×** |

**Where protoScala loses, it loses increasingly, and that is the honest reading
of this table.** On `fan-out` protoClojure scales 292,983 → 670,082 msg/s from 1
to 4 workers while protoScala peaks at 2 and then falls, so the ratio decays
**0.67× → 0.50× → 0.34× → 0.32× → 0.29× → 0.23×** at w = 1, 2, 4, 6, 8, 16. On
`single` it decays **0.84× → 0.59×**, because protoScala's rate *falls* with
added workers (210,572 → 149,160) where protoClojure's stays flat near 253,000.
Both are the single-method invariant serialising one actor while the extra
workers still cost synchronisation: **protoScala pays for workers it cannot
use.** It leads only on the two CPU-bound saturation shapes, where per-message
work dominates mailbox cost. Ask latency, 5,000 pooled samples per worker count:
p50 rises 24.4 → 33.9 µs from w=1 to w=16, p99 2,131 → 2,486 µs.

*The superseded 2026-09-23 table, CAS-list mailboxes at load 2.96→9.68:*

| Mode | peak msg/s (median) | at | protoClojure at the same worker count |
|---|---:|---|---:|
| single | 133,942 | 2 workers | 302,051 [294,048-330,161] |
| fan-out | 104,384 | 1 worker | 314,711 [302,402-329,415] |
| MPSC | 161,528 | 2 workers | 285,136 [264,822-301,420] |
| MPMC | 254,412 | 4 workers | 307,878 [299,924-323,011] |
| ping-pong | 20,397 | 2 workers | no twin |
| await | 46,815 | 4 workers | no twin |
| priority | 118,575 | 1 worker | no twin |

*Reading of that superseded table, kept as written:* across the four comparable
shapes protoScala ran at **0.11×-0.83×** protoClojure's median rate on *those
scripts* — but see the correction below: for `fan-out` that ratio compared two
different workloads that shared a name. `await` completes at every worker count
**including one**, which is the point of the cooperative suspension and still
holds. Mailboxes were the CAS-list fallback in that run; the current table above
is on `ProtoMPSCQueue`.

#### Correction (v4): the `fan-out` comparison was not like-for-like

An earlier version of this section stated that protoScala's actors are
roughly 2×-5× slower than protoClojure's and that `fan-out` regresses as
workers are added *where protoClojure improves*. The worker-count curve
measured afterwards shows **that comparison was invalid**, and the tables
above inherit the flaw. What the full curve establishes:

- **The two `fan-out` scripts measure different work.** protoScala's rotates
  its target on every send, so nearly every message is a cold wake;
  protoClojure's sends 1000 consecutive messages to one actor, so 999 of
  every 1000 coalesce into an already-claimed actor. **Run with protoScala's
  rotating shape, protoClojure collapses too, and harder**: 143,900 → 97,100
  → 78,400 → 64,100 msg/s at 1, 4, 6 and 16 workers — **−55%**, against
  **−32%** for protoScala's `ProtoMPSCQueue` variant over the same window.
  protoClojure's own batched script, run minutes apart on the same binary,
  held its 231k → 448k → 418k rise. Equalising the shape **reverses the
  ranking from 2 workers up**.
- **The scheduler does scale; `fan-out` specifically does not.** On the same
  scheduler, the same binaries and the same interleaved run, **`MPMC` rises
  +113%** from 1 to 4 workers (154,322 → 329,335 msg/s) and is still **+81%**
  on its 1-worker figure at 16 workers.
- **`fan-out` is producer-bound in its send path.** An isolated sender
  reaches 113,600 sends/s against the 87,238 msg/s the benchmark observes at
  1 worker, and the send loop itself slows from 8.80 s to 18.44 s as workers
  go 1 → 16 with the work held fixed. 78-99% of the measurable window is
  inside the sender's loop, not the pool's.
- **The `MPSC` "−11% with the queue" reported in v3 does not reproduce** — it
  was noise from two worker counts with overlapping bands. Across the full
  curve the queue is a wash at 1 worker and better at every higher count.
- **`single` stayed flat**, as its single-method invariant requires, which is
  the harness sanity check.

None of this makes protoScala fast, and it is not claimed to. protoScala is
an agile, interoperable, easily integrable and very simple Scala — **not a
fast one**; at 1 worker on the identical rotating shape it is still 14%
behind protoClojure. The honest correction is narrower and it is this: the
earlier cross-runtime `fan-out` comparison was not measuring the same work,
the scheduler scales on `MPMC`, and `fan-out`'s ceiling is in its producer.

**None of the modes above can exhibit a rise up to the 6 physical cores of
this machine**: `single` has one producer and one actor, `fan-out` one
producer, `MPSC` four producers against one actor, and `MPMC` four producers
against four actors — so each is capped by its own structural concurrency,
1 or 4, and not by the core count. No conclusion about protoScala's 6-core
ceiling can be drawn from any number in the table above.

#### The saturation modes (v5): the worker-count rise the suite was missing

Two CPU-bound modes were added to close that gap, mirroring protoST's
`saturation_8a.st` / `saturation_32a.st`: **`saturation-8`** and
**`saturation-32`** run 8 or 32 actors over the same total work, with a
20,000-iteration summation (~1.5 ms) inside every handler. That puts the cost
in the pool instead of the sender — the send loop is **0.14% of the run at 1
worker** — so the workers, not the producer, are the constraint. Each has a
protoClojure twin of the same shape and the **same message count**, and both
assert the **sum their actors actually computed**, so a handler that silently
did no work fails instead of reading as a fast run.

**Re-measured 2026-09-26 on a quiet host** (0.55 busy CPUs of 12; idle 95.15 /
95.46 / 95.67 %), 5 samples per cell, protoClojure's twin interleaved at every
`(mode, w)` **as the load control**, speedup against each series' own `w=1`
(full tables:
[benchmarks/reports/2026-09-26-qw-saturation.md](benchmarks/reports/2026-09-26-qw-saturation.md);
superseded contended run:
[benchmarks/reports/2026-09-23-actors-v5-saturation.md](benchmarks/reports/2026-09-23-actors-v5-saturation.md)):

| speedup vs w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 |
|---|---|---|---|---|---|---|---|---|
| `saturation-32` | 1.91× | 2.79× | 3.56× | 3.80× | **4.23×** | 3.71× | 2.40× | 2.30× |
| `saturation-32`, protoClojure (control) | 1.89× | 2.77× | 3.46× | 3.83× | **3.99×** | 3.76× | 2.39× | 2.34× |
| `saturation-8` | 2.04× | 2.57× | 3.56× | 3.46× | 3.36× | 3.14× | **3.59×** | 3.32× |
| `saturation-8`, protoClojure (control) | 1.89× | 2.38× | 3.42× | 3.32× | 3.00× | 3.02× | **3.67×** | 3.23× |

*(contended, superseded: `saturation-32` read 2.02 / 2.82 / 3.43 / 3.60 / 3.68 /
3.90 / 3.97 / 3.93, peaking at w=12.)*

**Two questions the v5 report left open because of load are now answered, and
both answers changed.**

**The near-linear region reaches the physical core count.** `saturation-32` is
95.7 % efficient per worker at w=2, 93.1 % at w=3 and **88.9 % at w=4**, and the
curve keeps rising to a peak at **w=6 — exactly the six physical cores**, at
**4.23×**. Under contention it bent at w=3 and peaked at w=12. So the earlier
bend *was* the machine's spare capacity, not the scheduler, as that report
suspected but could not show.

**The SMT regression is real.** Above six workers `saturation-32` **falls** —
4.23× → 3.71× → 2.40× → 2.30× at w = 6, 8, 12, 16, a **46 % loss from peak** —
where the contended run read a flat 3.90 / 3.97 / 3.93. **And the load control
falls with it** (3.99× → 3.76× → 2.39× → 2.34×): two independent runtimes on the
same protoCore, same hardware, same moment, both regressing above the physical
cores. That makes it a property of the **platform and the machine**, not of
protoScala's scheduler, and it reproduces the shape protoST documented (3.11× at
w=6, regressions at 8 and 12).

`saturation-8` does **not** settle it: it peaks at w=12, dips at w=6, and its
spreads are wide ([2,549-3,220] at w=6), because 8 actors cap concurrency at 8
under the single-method invariant. Its twin has the same irregular shape, which
again points away from protoScala. The SMT answer rests on `saturation-32`.
| protoClojure twin, 32 actors | 1.79× | 2.69× | 3.09× | 3.51× | 3.65× | 3.91× | 3.76× |

**protoScala's actors do scale with workers** — this is the first mode in the
suite that shows it, reaching **3.68× at 6 workers** and peaking at **3.97× at
12**. Three honest qualifications: the near-linear region ends at about 3
workers, not 6; **protoST's SMT regression above 6 workers did not reproduce**
here; and the machine was carrying 6-7 foreign threads on its 12 logical CPUs
throughout, which is enough to explain both. **The exact peak and the absence
of an SMT cliff are therefore not settled** and need a quiet machine.
protoClojure's twin traces the same curve, within a few percent, with the same
peak — evidence that the shape is the machine's and not protoScala's
scheduler.

**Nothing involving `ProtoMPSCQueue` is shipped**: it is not in protoCore
master, the released protoScala still uses the CAS-list mailbox, and every
queue figure quoted here describes a branch build that no released
protoScala uses. Measurement and method:
[benchmarks/reports/2026-09-23-actors-v4-curve.md](benchmarks/reports/2026-09-23-actors-v4-curve.md)
(superseding [v3](benchmarks/reports/2026-09-23-actors-v3-pmq.md)'s reading).
Full reading, with every mode and worker count:
[benchmarks/RESULTS.md](benchmarks/RESULTS.md). protoScala's actors carry
protoCore objects that the collector traces, which is a deliberate cost of
the design (DESIGN §8.4), not an accident.

#### ThreadSanitizer: protoScala's own code is clean

Re-run 2026-09-26 on a quiet host with `-DPROTOSCALA_SANITIZER=thread` against a
matching ThreadSanitizer build of protoCore 2.5.0. **`setarch -R` is required at
two sites** — the test run *and* the build, because the precompiled prelude runs
an instrumented `protoscala-precompile` at build time and otherwise aborts at
45 %.

**Across nine workloads — a single-threaded script (0 reports), six actor
conformance cases, an 8 × 25,000-send stress and an 8-thread no-actor control —
no protoScala frame is the access site of any report.** Checked rather than
assumed: frame `#0` of every reported access, and every `SUMMARY` site, is a
protoCore file. The one protoScala-sited race the first TSan run found — a plain
global `ActiveCallContext` handed to spawned threads with no happens-before
edge — was fixed in `63d3505` and no longer appears. The correctness invariants
hold: the stress printed **`200000`, exactly right**.

**This is a non-reproduction, not a clean bill of health.** `ActorScheduler.cpp`
has a separately diagnosed defect — `finishTurn` releases its claim by CAS and
*then* reads `pendingIdx` and the `__pend<b>__` list that the next owner is
already writing — which did not surface in these nine runs. A data race is
nondeterministic and nine runs on one host do not retire one; that defect is
pending a fix and its check is a re-run of the stress case.

What remains are protoCore sites, concentrated in `SparseListAlgorithms.h`.
**These are judged TSan false positives**, for four reasons: every "Previous
write" is a *constructor* (memory no other thread can yet reference); the
algorithm is a persistent AVL tree that never writes a published node, only
`new(context) Node(...)`; all reports fall inside **one 16 MiB `posix_memalign`
arena** whose cells are recycled through freelists with no `free()` and **no
`__tsan_*` annotations anywhere in protoCore**, so TSan's shadow history is never
invalidated on reuse; and the control's eight threads share no user object at all,
so they cannot genuinely race. The same output would also be produced by a real
live-cell recycle, which is strongly disfavoured but **not excluded** — annotating
protoCore's freelist handoff and re-running is the experiment that would close it.
Details: [`benchmarks/reports/2026-09-26-quiet-window.md`](benchmarks/reports/2026-09-26-quiet-window.md) §2.

### The general suite (0.3.0)

**Re-measured 2026-09-26 on a quiet host** (0.59 busy CPUs of 12; idle 94.57 /
95.23 / 95.49 %), AMD Ryzen 5 5500U (6 cores, 12 logical CPUs), Linux 7.0,
protoScala `f835aee`, protoCore `da5f19e2` (2.5.0), CPython 3.14.0,
protoPython `f40137cd`, protoST `55fe70f`, protoClojure `39d8353`. 5 runs per
cell, median wall-clock in ms of a **cold process** (start-up included, for every
runtime) with its `[min-max]` spread, every run's printed result verified;
`—` means the runtime has no twin of that workload. Full report:
[benchmarks/reports/2026-09-26-qw-suite.md](benchmarks/reports/2026-09-26-qw-suite.md)
(superseded contended runs are kept at
[benchmarks/reports/2026-09-23-suite-v2.md](benchmarks/reports/2026-09-23-suite-v2.md)
and [benchmarks/reports/2026-09-23-phase3-v1.md](benchmarks/reports/2026-09-23-phase3-v1.md)).

**The quiet host moved this table against protoScala.** The geomean versus
CPython went from **0.91× to 1.06×** — from 9 % faster to 6 % slower — and
**every one of the twelve ratios moved the same way**, because CPython gained
more from the quiet host than protoScala did: CPython's medians fell 16–22 %
(`fib30` 179.6 → 140.6, `object_tree` 209.1 → 166.6) while protoScala's fell
0.5–15 % (`fib30` 372.3 → 370.3, `object_tree` 406.9 → 382.0). **The contended
figures flattered protoScala.** Why CPython is the more load-sensitive of the two
is **not settled here**; a plausible but unverified candidate is that
protoScala's ~20 ms kernel-bound start-up is a large, load-insensitive constant
in every cell.

Two columns from the superseded table are absent rather than re-measured:
**Scala 3.9 (JVM 21)**, because no `SCALA_HOME` is installed on this host, and
**protoScala Release**, because `build_bench/protoscala` links the retired
`libprotoCore.so.2` and dies in the loader — the harness marked all 12 of its
cells `FAILED` and its 4.45 ms cold-start median is a crash, not a win. That
slot needs a rebuild before it means anything.

| Workload | protoScala | CPython 3.14 | protopy | protost | protoclj | protoScala ÷ CPython | was |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (0 until 100000) | 24.1 [23.4-24.9] | 32.1 [31.4-32.8] | 101.0 [100.8-101.9] | 32.9 [31.1-33.8] | — | 0.75× | 0.65× |
| `fib` (fib(25)) | 49.3 [49.1-49.3] | 38.1 [37.6-39.8] | 158.4 [152.8-362.5] | 346.8 [340.5-534.6] | — | 1.29× | 1.07× |
| `str_concat` (2000 concats) | 20.8 [20.5-22.0] | 28.6 [27.5-29.6] | 101.1 [99.9-114.4] | 15.9 [15.4-16.7] | — | 0.73× | 0.57× |
| `range_iterate` (100000) | 23.6 [23.3-24.2] | 31.5 [31.1-32.6] | 101.8 [92.6-105.3] | 58.5 [51.2-69.0] | — | 0.75× | 0.63× |
| `tak` (18, 12, 6) | 27.2 [26.1-28.4] | 29.9 [29.8-35.6] | 33.5 [33.3-34.2] | — | 39.0 [38.5-40.8] | 0.91× | 0.72× |
| `fib30` (fib(30)) | 370.3 [356.8-393.9] | 140.6 [136.4-147.5] | 730.9 [713.5-746.4] | — | 482.1 [471.3-495.4] | 2.63× | 2.07× |
| `sum_loop` (0..1000000) | 63.5 [62.3-64.4] | 76.4 [74.2-77.7] | 71.8 [71.5-72.5] | — | 57.7 [55.8-58.5] | 0.83× | 0.75× |
| `factorial_100` (BigInt) | 18.9 [17.6-20.6] | 28.2 [27.4-28.4] | 17.9 [17.2-18.5] | — | 14.0 [13.3-16.9] | 0.67× | 0.54× |
| `attr_lookup` (3 field reads × 100000) | 37.3 [36.4-39.3] | 37.9 [37.3-41.5] | 134.7 [133.5-163.2] | 103.8 [102.2-105.7] | — | 0.98× | 0.79× |
| `object_tree` (131071-object tree) | 382.0 [376.5-414.0] | 166.6 [165.4-170.4] | 3119.7 [3076.7-3144.0] | — | — | 2.29× | 1.95× |
| `list_ops` (map / filter / foldLeft, 100000) | 398.2 [393.3-418.9] | 1562.8 [1539.3-1867.7] | 525.2 [521.2-537.9] | — | — | 0.25× | 0.24× |
| `map_build` (build and read back 50000) | 299.9 [294.6-306.4] | 63.3 [62.6-66.6] | 796.6 [783.9-801.1] | — | — | 4.74× | 4.43× |
| **Geomean vs CPython** (12 workloads) | **1.06×** | 1.00× | 2.70× (12) | 1.93× (5) | 1.14× (4) | **1.06×** | 0.91× |

`map_build`'s 4.74× is the honest cost of an immutable map: each of the 50000
inserts returns a new `ProtoMap` version where CPython's `dict` mutates one
object in place. protopy runs the same twin in 796.6 ms, so it is the shared
object kernel's cost, not protoScala's frontend. `list_ops`'s 0.25× is partly the
Python twin's O(n) `insert(0, i)` against an O(log n) `::` — the row says the
prepend-and-fold surface is cheap, not that protoScala is four times CPython.
`fib30` and `object_tree` are the two clear losses to CPython, and they are the
two the quiet host worsened most.

*The superseded 2026-09-23 table follows, kept as measured at load 3.37→4.39
against protoCore `bf972d3f` (2.0.0), with the Scala/JVM column this run could
not reproduce.*

| Workload | protoScala | protoScala Release | Scala 3.9 (JVM 21) | CPython 3.14 | protopy | protost | protoclj | protoScala ÷ CPython |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` (0 until 100000) | 24.6 [22.2-25.2] | 23.5 [23.1-25.8] | 216.4 [201.4-263.2] | 38.0 [36.6-44.5] | 123.7 [120.5-129.5] | 36.7 [35.2-38.6] | — | 0.65× |
| `fib` (fib(25)) | 49.2 [47.5-56.0] | 49.2 [46.8-57.0] | 210.5 [204.5-225.0] | 46.0 [44.0-50.8] | 190.1 [179.8-192.6] | 415.3 [400.3-425.0] | — | 1.07× |
| `str_concat` (2000 concats) | 19.4 [17.8-20.0] | 20.6 [19.1-21.5] | 205.1 [197.8-214.8] | 34.2 [33.2-45.3] | 122.2 [119.2-140.1] | 17.4 [16.7-18.0] | — | 0.57× |
| `range_iterate` (100000) | 23.0 [21.5-25.7] | 23.9 [22.9-25.6] | 218.1 [204.3-240.3] | 36.5 [35.3-42.6] | 122.6 [119.5-142.6] | 63.3 [50.6-72.0] | — | 0.63× |
| `tak` (18, 12, 6) | 26.6 [25.6-30.8] | 26.2 [25.4-26.6] | 216.3 [192.4-270.0] | 36.9 [35.2-45.3] | 39.2 [37.5-41.0] | — | 46.4 [44.0-50.3] | 0.72× |
| `fib30` (fib(30)) | 372.3 [350.2-482.4] | 382.6 [350.9-438.7] | 206.7 [190.8-249.4] | 179.6 [169.8-228.2] | 887.3 [852.3-1051.0] | — | 582.1 [558.8-784.5] | 2.07× |
| `sum_loop` (0..1000000) | 67.1 [66.5-76.1] | 68.1 [66.4-79.9] | 205.8 [188.6-228.7] | 90.1 [87.7-97.0] | 87.6 [77.3-97.1] | — | 68.7 [65.9-71.2] | 0.75× |
| `factorial_100` (BigInt) | 17.9 [16.4-18.7] | 17.9 [17.3-19.7] | 206.1 [196.5-221.1] | 32.9 [31.5-35.5] | 20.4 [19.7-21.5] | — | 16.3 [14.5-17.0] | 0.54× |
| `attr_lookup` (3 field reads × 100000) | 35.8 [33.9-38.7] | 36.8 [34.0-37.9] | 210.0 [197.6-227.6] | 45.6 [42.0-47.1] | 161.6 [157.2-167.9] | 123.6 [117.6-129.4] | — | 0.79× |
| `object_tree` (131071-object tree: build, path-copy, fold) | 406.9 [383.7-629.9] | 404.7 [379.6-613.6] | 255.0 [216.0-384.1] | 209.1 [195.6-261.0] | 3657.1 [3619.2-4142.0] | — | — | 1.95× |
| **Geomean vs CPython** (rows) | 0.86× (10) | 0.87× (10) | 3.72× (10) | 1.00× | 2.82× (10) | 1.84× (5) | 1.11× (4) | **0.86×** |

**Suite v1 completed in 0.4.0.** Phase 3 added the last two workloads the
ROADMAP's benchmark suite v1 names, measured on 2026-09-23 at load average 3.90
against protoCore `983bbf98` (2.1.0) — full table:
[benchmarks/reports/2026-09-23-phase3-v1.md](benchmarks/reports/2026-09-23-phase3-v1.md),
reading: [benchmarks/RESULTS.md](benchmarks/RESULTS.md). Both rows are carried
into the current table above, re-measured; these are the figures as recorded then.

| Workload | protoScala | CPython 3.14 | protoScala ÷ CPython |
|---|---:|---:|---:|
| `list_ops` (map / filter / foldLeft over 100000 elements) | 465.5 [454.4-492.4] | 1934.7 [1876.3-2041.2] | 0.24× |
| `map_build` (build and read back 50000 entries) | 354.9 [345.7-355.3] | 80.0 [74.6-84.3] | 4.43× |
| **Geomean vs CPython**, all 12 workloads | 0.91× | 1.00× | **0.91×** |

Cold start (`benchmarks/cold-start.sh`, 21 runs, target < 25 ms).
**Verdict: MET.** This supersedes a `MISSED` verdict recorded on 2026-09-26,
which was a **load artefact**: it gated on a load *average* of 1.84 while its own
`mpstat` never put the foreign load below 2.3 of 12 busy CPUs. Re-measured on
2026-09-26 at **0.55 busy CPUs** (idle 95.59 / 94.97 / 95.61 %), three
interleaved rounds, 21 runs per cell, **all 378 runs verified**,
`cold-start.sh` exiting **0 in 24 of 24 cells**:

| build / prelude path | mode | median of 3 rounds | per-cell spread | verdict |
|---|---|---:|---:|---|
| Release, image | script | **21.63 ms** | 20.15–33.67 | **MET** |
| Release, image | repl | **22.32 ms** | 21.22–34.05 | **MET** |
| RelWithDebInfo, image | script | **21.73 ms** | 19.88–31.09 | **MET** |
| RelWithDebInfo, image | repl | **22.20 ms** | 20.35–24.54 | **MET** |
| Release, `PROTOSCALA_PRELUDE_NO_IMAGE=1` | script | 24.09 ms | 22.37–25.65 | MET |
| Release, `PROTOSCALA_PRELUDE_NO_IMAGE=1` | repl | 24.66 ms | 23.15–27.67 | MET |

**`Release` and `RelWithDebInfo` are indistinguishable**: 0.10–0.12 ms apart on
the median-of-medians against 1.5–12.7 ms within-cell spreads, which does not
support calling either faster. The image is worth **2.46 ms** (script) and
**2.34 ms** (repl), agreeing with the 1.92 / 2.12 ms measured at 0.6.0.

**The 23.73 ms reproduces, and is beaten.** The decisive control is the same
binary at two loads: `build_rwdi/protoscala`, unrebuilt since the loaded window,
measured **28.37 / 27.97 ms** there and **22.16 / 22.02 ms** here. Five
independently built binaries now land in **21.34–22.32 ms**. Load was the
variable all along, so the earlier "did not reproduce, cause unidentified" is
withdrawn.

**The memory half of the target is not met, and had never been measured.**
DESIGN §1 states the goal as "< 25 ms, ~20 MB RSS", but every cold-start run
recorded above measures only time. Measured on 2026-09-27 against the installed
0.6.0 binary (`/usr/bin/protoscala examples/hello.scala`, three runs,
`/usr/bin/time -f %M`): **24 088 / 24 184 / 24 092 KB max RSS**, so ≈ **24.1 MB**
against a ~20 MB target — over by about 20 %. The figure is reported here rather
than folded into the verdict above because it is a different target with a
different instrument, and a `MET` on time says nothing about it. No attempt has
been made yet to find where those 4 MB are.

Two operands belong beside that number, because they change what it means:

- **The harness's own floor is ~4.7 ms on this host.** `cold-start.sh` times a
  pipeline — two `date +%s%N` forks, the binary, a pipe, an `awk`, plus an
  `sh -c` and a `printf` for the REPL case — and timing that same construct
  around trivial commands gives medians of **4.67 ms for `/bin/true`**, 5.07 ms
  for `/bin/echo hi` and 4.46 ms for `sh -c true`, 21 runs each. It is **stated,
  not deducted**, and it is **not additive**: 42 bare runs with no harness around
  them take 0.819 s wall, i.e. **19.5 ms per run**, against the harness's 21.6 ms
  median — so ~2 ms, not ~5 ms, is what the harness actually adds here.
- **Start-up is kernel-bound here, not interpreter-bound.** Across the same 42
  runs the binary spent **0.172 s of user time against 0.669 s of system
  time** — `sys` is 3.9× `user` — which is process creation, `mmap` and dynamic
  linking rather than prelude work. That is a **diagnostic direction, not a
  defect**.

One trap worth naming, because it was raised and then refuted: this host is
`amd-pstate-epp` / `powersave` / `balance_power`, 1.11–4.06 GHz, so a 22 ms burst
on an idle machine might plausibly never boost — which would make a quiet host
the *wrong* host for this measurement. **It does not happen for this workload:**
the quiet host is 3–6 ms *faster* in every cell, so contention dominates clock
ramp. Mean CPU MHz is now logged beside every gate reading (1,746–3,410 MHz
across five gates, with no matching movement in the medians).

Full write-up, with every operand and the gate readings:
[`benchmarks/reports/2026-09-26-quiet-window.md`](benchmarks/reports/2026-09-26-quiet-window.md)
(the superseded loaded-host attempt is kept at
[`benchmarks/reports/2026-09-26-quiet-host-attempt.md`](benchmarks/reports/2026-09-26-quiet-host-attempt.md)).

The history the table used to carry stands as *history*, not as a current
verdict — these are the figures as recorded at each release, in one interleaved
window per row group, 21 runs per case, every case `verified=21`, load average
2.97 falling to 2.67:

| binary | script | REPL |
|---|---:|---:|
| 0.4.0 | 23.91 ms | 24.46 ms |
| 0.5.0 | 25.22 ms | 25.58 ms |
| 0.6.0, precompiled prelude image | 23.73 ms | 23.89 ms |
| 0.6.0, `PROTOSCALA_PRELUDE_NO_IMAGE=1` (the 0.5.0 path) | 25.65 ms | 26.01 ms |

Read that as what the harness printed then. The 0.6.0 image row is the one the
2026-09-26 quiet-window run **reproduced and beat** (21.63 / 22.32 ms), and its
`NO_IMAGE` row is reproduced too (24.09 / 24.66 ms), so the 2 ms the image is
worth is now measured three times on three different hosts' loads.
`benchmarks/run_benchmarks.py`, which applies a stricter per-sample verdict,
still reads **script STRADDLES** (median 21.94 ms, worst sample 25.54) and
**repl MET** (median 21.69, worst 23.36): the median is comfortably inside the
target and an occasional sample on a daily-driver desktop still crosses it.
Both readings are true, and neither overrides the other.

What the prelude image does is still real, and is unaffected by the verdict: it
removes parse, desugar and compile, measured before it was built at 59.6 %, 2.6 %
and 22.5 % of the prelude's 4.4 ms. What it cannot remove is `linkSymbols`
(342 µs) and *running* the compiled prelude (177 µs): the tables hold strings and
PODs, so the symbols must still be interned into a `ProtoSpace` and
`MAKE_CLASS`/`MAKE_FN`/`STORE_GLOBAL` must still execute. Removing those would
need a protoCore space image, which does not exist.

The REPL column comes from a two-binary interleave and the script column from a
three-binary one, so compare down a column and never across; the 0.4.0 script
median was 23.84 ms in the two-binary run and 23.91 ms in the three-binary one,
which is the size of the run-to-run error here.

How 0.5.0 came to miss it, since the table keeps the row: Phase 4 cost
**+1.31 ms** and grew the prelude from 156 to 200 lines — twenty exception
classes, `StringContext` and the `Priority` enum — which was parsed, desugared,
compiled and run at every start-up with nothing cached between runs. Adding twenty
*more* classes of the same shape to a probe build cost a further **+1.19 ms**, so
roughly 60 µs per prelude class accounted for the whole regression: it was the
prelude's size, not the exception machinery in the engine. That is what 0.6.0's
image removes, and the fourth row above is the same code path 0.5.0 shipped,
still missing by 0.65 ms.

Earlier phases measured under the target: the 0.3.0 run gave script 21.02
[19.44-22.68] ms and REPL 21.77 [20.37-23.28] ms, with every individual sample,
worst included, under 25 ms. Whether 0.3.0 would still measure there on the host
as it is now was **not** re-checked, so that row is history too.

**Reading.** Short rows measure start-up more than work: protoScala starts in
about 17-18 ms (`factorial_100` is almost pure start-up) and CPython in about
32-38 ms, so on short workloads protoScala comes out ahead, and the 0.86×
geomean is as much a start-up figure as a throughput one. Where the work
dominates, the picture reverses, and protoScala loses. `fib30` (2.69M calls)
is 2.07× slower than CPython 3.14 — protoScala is call-dispatch bound, which
the `fib(25)` row hides behind its start-up advantage. `object_tree`, the
workload that exercises what protoScala is actually *for* (build 131071
immutable case-class instances, path-copy a spine so the copy shares every
right subtree, then fold both versions with pattern matching), is 1.95×
slower than CPython's `__slots__` twin: structural sharing makes the copy
cheap, and allocating the tree is what dominates. protopy runs the same twin
in 3.66 s, so that cost belongs to the shared object kernel and its GC, not
to protoScala's frontend. `attr_lookup`, the field-read twin, runs at 0.79×
CPython and about 3.5× faster than protoST on the same machine: attribute
reads go through protoCore's per-thread attribute cache and no parallel
inline caches were added. Loop arithmetic (`sum_loop`, one million
iterations) runs at 0.75× CPython and close to protoClojure. The Release
build is indistinguishable from the canonical RelWithDebInfo build within
spread. The JVM column runs the same `.scala` source compiled with `scalac`
(compile time excluded, reported separately); each sample is a fresh `java`
process, so ~200-260 ms of JVM start-up and class loading dominate every row,
and the JIT's steady state — where the JVM would be far ahead on `fib30` and
`object_tree`, as its ~200 ms floor already suggests — is not measured.
Cold-process timing favours short-lived runtimes.

Compared against the superseded, single-sample run of the same day, every
protoScala workload's median moved less than 10%, and the geomean moved from
0.83× to 0.86× — inside the spread shown above, i.e. the earlier run's
numbers were not meaningfully noise-distorted for the protoScala column, even
though it reported no spread of its own. The columns that moved most were
`protoScala Release`'s `str_concat` (+17%, 17.6 → 20.6 ms) and `range_iterate`
(+14%, 21.0 → 23.9 ms) — both still inside a few milliseconds and both
plausibly load noise given the higher midpoint/end load of this run (up to
6.83) versus the superseded run (up to 4.21).

**What these numbers are for.** This suite tracks start-up and guards against
regressions; it is not a claim that protoScala is fast. Phase 2 added the
first two workloads that matter for the positioning, `attr_lookup` and
`object_tree`; the rest — the full persistent collections and interop — join
the suite as the phases that enable them land. Phase 5 added the actor
benchmarks above.

**Pending workloads** (not approximated): `list_append` and protoClojure's
`sum-squares`, and `exception_latency` — Phase 4 shipped exceptions, and the
measurement it owed was a targeted A/B rather than a new workload, because a
throw-per-iteration benchmark measures the handler search and not the thing
`exception_latency` was meant to compare. The per-frame retry loop was measured by
bypassing it: `fib30` 1.2505 vs 1.3016 Gcycles, `attr_lookup` 122.1 vs 128.8
Mcycles, `tak` 91.4 vs 88.7 Mcycles (`perf stat -r 3`) — free on the non-throwing
path, as a zero-cost-exceptions ABI should be. See
[benchmarks/README.md](benchmarks/README.md) to run the suite.

## Building

Prerequisites: a C++20 compiler, CMake ≥ 3.20, libreadline (from Phase 1), and
protoCore built as a sibling directory (or installed and passed via
`-DPROTO_CORE_PREFIX=<prefix>`).

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

## Producing a UMD module — in C++, in Python or in Scala

A protoCore UMD module is a shared library that defines `proto_module_init`. Three
toolchains now produce one: a hand-written C++ file, `protopyc` from Python, and
`protoscalac` from Scala. **The producing language is an implementation detail of the
module**: one `.so`, one `dlopen`, one init symbol, one entry in the provider registry.

```bash
build_release/protoscalac hello.scala --build-so   # -> module.so
build_release/protoscala --run-module ./module.so
ldd ./module.so | grep proto        # libprotoScala.so.1, libprotoCore.so.3
```

What that buys, and it is not speed: a transpiled module's functions are
`proto::ProtoMethod`s, and a `proto::ProtoMethod` is callable by any runtime in the
family. protoScala **bytecode** is not — a foreign runtime can hold a bytecode-backed
function object (values cross a runtime boundary with no copy) and still cannot call
it. The generated C++ calls the runtime dynamically, exactly as `protopyc`'s does, so
arithmetic is not faster and a send costs what it cost.

**That call is demonstrated, not argued.** `proto_module_init` returns a module object
carrying one `proto::ProtoMethod` cell per exported top-level `def`, and a caller needs
three protoCore calls and no protoScala anything:

```cpp
const proto::ProtoObject* fn = module->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "add"));
const proto::ProtoObject*  r = fn->asMethod(ctx)(ctx, fn->asMethodSelf(ctx), nullptr, args, nullptr);
```

`ctest -R interop/foreign-call` runs it. The caller includes `protoCore.h` and one shim
header, and the harness **greps** it for the name `protoScala` and fails if it appears;
its four answers are checked against what the interpreter prints from the same source,
and four named mutations each turn it red. The honest boundary is in
[docs/INTEROP.md](docs/INTEROP.md) §8: the call needs protoCore alone, exported cells
mean one `Session` per process, and no other runtime has taken it up yet — that needs a
change in that runtime's repository.

**And a compiled module is importable.** `import util.Strings` finds `util/Strings.so`
under `PROTOSCALA_MODULE_PATH` (and then the installed module directory — `--version`
prints the list). It is searched *after* source, so a `.scala` beside a `.so` still wins
and installing a compiled module cannot change an import that already resolved. What it
binds is a **foreign** module, with late binding: the tables that would carry a class
across and keep early type binding are not built, so a pattern match against a compiled
module's class does not compile.

`protoscalac` is still **incomplete**: it refuses `import`, `await`, and named arguments
and default values, each at transpile time with a named message and a source position,
and never mistranslates. Classes, traits, objects, case classes, enums, `super` and
`try`/`catch`/`finally` are supported. Of 922 conformance fixtures, **704** run
transpiled; of the 191 Scala 3 `tests/run` corpus tests the interpreter passes, **176**
pass transpiled — 95 of them checked against the corpus's own expected output — with
**zero** divergences in either direction. What it refuses, why, and the
two differentials that measure it are in
[docs/PROTOSCALAC_SPECIFICATION.md](docs/PROTOSCALAC_SPECIFICATION.md).

**And it is measurably not a speed-up.** The same source down both paths, interleaved
in one window: a geomean of **1.244× slower** transpiled over ten workloads, **2.57×**
at worst on `fib30`, and **indistinguishable** (geomean 0.985) on the six that are not
call-bound. A null program is 22.29 ms interpreted against 22.40 ms transpiled, so
removing the front end buys nothing measurable either. The cost is the calling
convention — a `proto::ProtoMethod` takes a `ProtoList`, so a transpiled call
allocates one and opens a second context — and it is paid *for* the cross-runtime
callability. The useful conclusion is where the cost is **not**: optimisation effort
belongs in the object model, not in the interpreter's dispatch loop.
[Report](benchmarks/reports/2026-09-26-interpreted-vs-transpiled.md).

## Usage

```bash
# Run a script
protoscala examples/hello.scala
protoscala examples/fib.scala 10

# Start the REPL
protoscala
```

## Learn more

- [docs/TUTORIAL.md](docs/TUTORIAL.md) — dual-audience tutorial (Scala programmers; Python/JavaScript developers)
- [docs/LANGUAGE.md](docs/LANGUAGE.md) — supported language and departures from Scala
- [docs/STATUS.md](docs/STATUS.md) — living implementation tracker
- [docs/DESIGN.md](docs/DESIGN.md) — the approved design specification
- [docs/ROADMAP.md](docs/ROADMAP.md) — phases with verifiable done-when criteria
- [docs/tutorial/15-modules-and-polyglot-interop.md](docs/tutorial/15-modules-and-polyglot-interop.md) — modules and polyglot interop, chapter by chapter
- [docs/tutorial/worked-example.md](docs/tutorial/worked-example.md) — the worked example, where the pieces are shown working together
- [docs/INTEROP.md](docs/INTEROP.md) — UMD polyglot interop: what routes where, and what is reachable today
- [docs/PROTOSCALAC_SPECIFICATION.md](docs/PROTOSCALAC_SPECIFICATION.md) — `protoscalac`, the protoScala-to-C++ transpiler: command line, generated code, what it refuses and why
- [docs/platform/](docs/platform/) — protoCore extensions specified by this project
- [docs/plans/](docs/plans/) — task-level implementation plans

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Gustavo Marino.
