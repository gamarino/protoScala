# protoScala

> **A dynamic Scala 3 dialect on the protoCore object kernel — fast start-up, persistent immutable data by default, GIL-free native actors, and a polyglot module system built on protoCore's Unified Module Discovery.**

protoScala is a language runtime of the [protoCore](https://github.com/numaes/protoCore) family, alongside [protoST](https://github.com/gamarino/protoST) (Smalltalk), [protoClojure](https://github.com/gamarino/protoClojure) (Clojure), [protoJS](https://github.com/gamarino/protoJS) (JavaScript) and [protoPython](https://github.com/gamarino/protoPython) (Python).

It is **not** a JVM replacement: there is no JVM, no sbt/Maven, no Java interop and no static typechecker. It runs Scala 3 source — braces or significant indentation — with types parsed and erased, on a runtime that aims to:

- **start in under 25 ms with ~20 MB RSS**, making Scala viable for scripts and REPL-driven work;
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

**4. There is no Java, and the exception hierarchy is twenty unqualified names.**

```scala
println(new java.io.File("x"))                // error: Not found: type java.io.File

try throw new IllegalStateException("bad")
catch case e: Exception => println(e.getMessage)    // bad
```

`catch { case e: java.io.IOException => }` does not compile: there is no `java`
namespace (D8, D73).

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

Five things are new if you are coming from Python or JavaScript.

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

**Phase 6 complete (version 0.6.0) — not production ready, open for community
review.** Phase 5 was implemented before Phases 3 and 4, so the minor version went
0.2.0 → 0.3.0 (actors) → 0.4.0 (collections) → 0.5.0 (exceptions, enums,
arguments and extensions) → 0.6.0 (modules, UMD and packaging). Every phase the
roadmap named is now closed; what remains is listed in
[docs/ROADMAP.md](docs/ROADMAP.md) as tracks rather than phases. **Track Y**
delivered the first working cross-runtime import (`import st.<module>`); what it
did not deliver is `import py.numpy`, which needs work in protoPython rather than
here. The binary runs Scala 3 scripts and offers a REPL,
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
  parameter lists.

```scala
val counter = Actor.spawn(0) { (state, msg) => (state + msg, state + msg) }
counter ! 1                      // tell: queue a message, return at once
println((counter ? 41).await)    // ask:  a Future of the reply  ->  42

val sink = Actor.spawn(0) { (state, msg) => state + msg }   // no reply to give
println(Future(6 * 7).await)     // Future takes its body by name  ->  42
```

Exceptions, `enum`, extension methods, the Phase 3 collections, string
interpolation and UMD are not implemented yet — see
[docs/STATUS.md](docs/STATUS.md) for the exact boundary and
[docs/ROADMAP.md](docs/ROADMAP.md) for what each later phase brings.

## Performance

protoScala is positioned as an agile, interoperable, easily integrable and
very simple Scala — **not a fast one.** Beating the JVM on integer loops, or
matching protoClojure's raw actor throughput, are explicit non-goals (see
[docs/DESIGN.md](docs/DESIGN.md) §1). Where the numbers below show protoScala
losing, they are reported as measured, not adjusted or explained away.

Both tables in this section come from an **interleaved re-run** (2026-09-23)
that supersedes an earlier same-day run measured in blocks. This machine is
the maintainer's daily-driver desktop: VS Code, Chrome and PyCharm run
throughout, the load-average floor is roughly 2-3 on 12 logical CPUs, and
there is no quiet window to wait for. Every runtime is sampled round-robin
(one sample of column A, then B, then C, … then back to A) so ambient load
hits every column alike, and every cell below reports its median **and**
`[min-max]` spread — ratios between columns are the primary result, absolute
milliseconds/msg-per-second are indicative only.

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

| Mode | peak msg/s (median) | at | protoClojure at the same worker count |
|---|---:|---|---:|
| single | 133,942 | 2 workers | 302,051 [294,048-330,161] |
| fan-out | 104,384 | 1 worker | 314,711 [302,402-329,415] |
| MPSC | 161,528 | 2 workers | 285,136 [264,822-301,420] |
| MPMC | 254,412 | 4 workers | 307,878 [299,924-323,011] |
| ping-pong | 20,397 | 2 workers | no twin |
| await | 46,815 | 4 workers | no twin |
| priority | 118,575 | 1 worker | no twin |

Across the four comparable shapes, protoScala runs at **0.11×-0.83×**
protoClojure's median rate on *these scripts* — but see the correction
below: for `fan-out` that ratio compares two different workloads that share
a name. `await` completes at every worker count **including one**, which is
the point of the cooperative suspension. protoScala's mailboxes are still
the CAS-list fallback (`protoscala --version` reports it explicitly).

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

Shipped CAS-list binary, median of 4 interleaved samples, speedup against 1
worker (full tables:
[benchmarks/reports/2026-09-23-actors-v5-saturation.md](benchmarks/reports/2026-09-23-actors-v5-saturation.md)):

| speedup vs w=1 | w=2 | w=3 | w=4 | w=6 | w=8 | w=12 | w=16 |
|---|---|---|---|---|---|---|---|
| `saturation-8` | 1.98× | 2.35× | 3.18× | **3.16×** | 3.47× | 3.14× | 3.26× |
| `saturation-32` | 2.02× | 2.82× | 3.43× | **3.68×** | 3.90× | **3.97×** | 3.93× |
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

### The general suite (0.3.0)

Interleaved re-run: 2026-09-23, AMD Ryzen 5 5500U (6 cores, 12 logical CPUs),
Linux 7.0, protoScala commit `3703759` (main, actors landed), protoCore
`bf972d3f` (2.0.0), load average 3.37 at start, 4.08 at midpoint, 4.39 at
end. Full report, with versions and build types of every runtime:
[benchmarks/reports/2026-09-23-suite-v2.md](benchmarks/reports/2026-09-23-suite-v2.md)
(the superseded run, whose protoScala figures agree within 10%, is kept at
[benchmarks/reports/2026-09-23-suite.md](benchmarks/reports/2026-09-23-suite.md)).
Median wall-clock in ms of a **cold process** (start-up included, for every
runtime) with its `[min-max]` spread, 2 warmup + 7 timed runs, every run's
printed result verified; `—` means the runtime has no twin of that workload.

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
reading: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).

| Workload | protoScala | CPython 3.14 | protoScala ÷ CPython |
|---|---:|---:|---:|
| `list_ops` (map / filter / foldLeft over 100000 elements) | 465.5 [454.4-492.4] | 1934.7 [1876.3-2041.2] | 0.24× |
| `map_build` (build and read back 50000 entries) | 354.9 [345.7-355.3] | 80.0 [74.6-84.3] | 4.43× |
| **Geomean vs CPython**, all 12 workloads | 0.91× | 1.00× | **0.91×** |

`map_build` is the honest cost of an immutable map: each of the 50000 inserts
returns a new `ProtoMap` version where CPython's `dict` mutates one object in
place. protopy runs the same twin in 935.0 ms, so it is the shared object
kernel's cost, not protoScala's frontend. `list_ops`'s 0.24× is partly the
Python twin's O(n) `insert(0, i)` against an O(log n) `::` — the row says the
prepend-and-fold surface is cheap, not that protoScala is four times CPython.

Cold start (`benchmarks/cold-start.sh`, 21 runs, target < 25 ms). **Verdict for
0.6.0: MET.** The prelude is now compiled at build time rather than at every
start-up, and the third row is what proves it was the *image* and not anything
else in the release: both paths live in one binary, and
`PROTOSCALA_PRELUDE_NO_IMAGE=1` takes the source path. Three rounds interleaved in
one window, 21 runs per case, **every case reported `verified=21`**, load average
2.97 at the start and 2.67 at the end:

| binary | script | REPL |
|---|---:|---:|
| 0.4.0 | 23.91 ms | 24.46 ms |
| 0.5.0 | 25.22 ms | 25.58 ms |
| **0.6.0, precompiled prelude image** | **23.73 ms** | **23.89 ms** |
| 0.6.0, `PROTOSCALA_PRELUDE_NO_IMAGE=1` (the 0.5.0 path) | 25.65 ms | 26.01 ms |

`cold-start.sh` exited 0 in all three image rounds and 1 in all three source
rounds, on both cases; that exit code *is* the check, because it fails when any
run printed the wrong line or when a median is not below the target. The
artefact the `.deb` and the `.tar.gz` ship is a different binary (`Release`
rather than `RelWithDebInfo`) and was measured separately: 23.28 ms script and
23.73 ms REPL, also passing.

One thing that reading does **not** claim: the *worst* sample is still above
25 ms. `benchmarks/run_benchmarks.py` applies a stricter per-sample verdict and
records all four 0.6.0 cases as **STRADDLES** — median below the target, spread
crossing it — on a machine that runs an editor and a browser throughout. The
budget is met on the measure DESIGN §1 uses; the tail is not yet quiet.

The image removes parse, desugar and compile — measured before it was built at
59.6 %, 2.6 % and 22.5 % of the prelude's 4.4 ms. What it cannot remove is
`linkSymbols` (342 µs) and *running* the compiled prelude (177 µs): the tables
hold strings and PODs, so the symbols must still be interned into a `ProtoSpace`
and `MAKE_CLASS`/`MAKE_FN`/`STORE_GLOBAL` must still execute. Removing those would
need a protoCore space image, which does not exist. The budget is met with about
1.3 ms of headroom on the script case, and that headroom is what the next twenty
prelude classes would spend.

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

Earlier phases met the target: the 0.3.0 run measured script 21.02
[19.44-22.68] ms and REPL 21.77 [20.37-23.28] ms with every individual sample,
worst included, under 25 ms.

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
- [docs/platform/](docs/platform/) — protoCore extensions specified by this project
- [docs/plans/](docs/plans/) — task-level implementation plans

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Gustavo Marino.
