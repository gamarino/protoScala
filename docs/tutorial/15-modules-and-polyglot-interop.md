# 15. Modules and polyglot interop

> **Implementation status.** Everything in §§15.1–15.4 and §15.7 runs today: a
> `.scala` file is a module, all five import forms work, a selector may name a
> type, and a failed import names what it tried. The four family prefixes
> `py.`, `js.`, `st.` and `clj.` route to protoCore's provider registry — but
> **no runtime in the family registers `py`, `js` or `clj` yet**, so those
> imports compile, route, and stop with `no provider registered for '<alias>'`
> (§15.5). What is not implemented: a wildcard import of a foreign module
> (D92), types from a foreign module (D94), and lexical scoping of an import
> (D96).

Up to here every program has been one file. This chapter is about the second
file: how protoScala finds it, what an `import` binds, and what happens at the
edge where a module stops being protoScala and starts being another language.

## 15.1 What a module is here

A module is a **`.scala` file**, and you reach it by its path with the dots
that a directory separator would be. `import util.Strings` loads
`util/Strings.scala` and binds the name `Strings`.

Fixture: [`tests/conformance/tutorial/15-modules-import-a-module.scala`](../../tests/conformance/tutorial/15-modules-import-a-module.scala)

The module, in `util/Strings.scala`:

```scala
def shout(s: String): String = s.toUpperCase + "!"
def initials(s: String): String = s.split(" ").map(w => w.substring(0, 1)).mkString(".")
val greeting: String = "hello"
```

and the program beside it:

```scala
import util.Strings
@main def run(): Unit =
  println(Strings.shout("hello") + " " + Strings.greeting)
```

Prints:

```text
HELLO! hello
```

The file's top-level definitions are the module's members. There is nothing to
export and nothing to declare: what you write at the top level is what an
importer can reach.

**Coming from Python or JavaScript.** This is the model you already have.
`import util.Strings` is Python's `import util.strings` and JavaScript's
`import * as Strings from './util/Strings.js'`, and it behaves the way both do
in the one place that matters: **the file's top level runs when it is
imported**, exactly once, however many files import it.

Fixture: [`tests/conformance/tutorial/15-modules-runs-at-import.scala`](../../tests/conformance/tutorial/15-modules-runs-at-import.scala)

With `effects/Setup.scala` containing

```scala
println("setup ran")
val ready: Boolean = true
```

this program

```scala
import effects.Setup
@main def run(): Unit =
  println("ready: " + Setup.ready)
```

prints

```text
setup ran
ready: true
```

`setup ran` comes first because the import is resolved *while this file is
being compiled* — the module is found, compiled and run, and only then is the
importing file finished and its `@main` called. A module that exists purely
for its effects therefore works, which is the reason the choice was made this
way (D90). Import the same module from ten files and its top level still runs
once: the key is the file's canonical absolute path, so two spellings that
reach the same file reach the same object.

**Coming from Scala on the JVM.** This is **not** a package. There is no
`package` clause, no classpath, no JAR and no `package object`; a `package`
declaration is not how anything is found here. The file *becomes an `object`*
named after itself: `util/Strings.scala` is `object Strings`, its `def`s and
`val`s are that object's members, and a `case class` written in it is
`Strings.Tag` (D91). Two consequences follow from the name coming from the file
rather than from the source. A module may not carry an `@main` — it is a
library, not a program, and a silently ignored entry point would be a trap, so
it is refused:

```text
a module may not define an @main method: Shapes is imported, not run
```

And an `object` in Scala initialises lazily, on first access, while a module
here is forced at the import site. That is the D90 difference above, and it is
deliberate: Scala has no file-level modules to diverge from, and the languages
that do have them all run a module at import.

## 15.2 The five forms

Every form Scala 3 writes is accepted, with Scala's meaning.

| Form | What it binds |
|---|---|
| `import util.Strings` | the module object, under its own name |
| `import util.Strings as S` | the module object, under `S` |
| `import util.Strings.{a, b as c}` | the named members, `b` under `c` |
| `import util.Strings.*` | every member of the module |
| `import util.Strings._` | the same, in the Scala 2 spelling |

**The module under an alias.**

Fixture: [`tests/conformance/tutorial/15-modules-import-with-an-alias.scala`](../../tests/conformance/tutorial/15-modules-import-with-an-alias.scala)

```scala
import util.Strings as S
@main def run(): Unit =
  println(S.shout("hello"))
```

```text
HELLO!
```

**Named selectors, with a rename.** A selector list picks members out of the
module and binds each one directly, so the module's own name never appears at
the call site. `as` renames one of them; the Scala 2 arrow, `initials => short`,
is accepted for the same thing.

Fixture: [`tests/conformance/tutorial/15-modules-import-selectors.scala`](../../tests/conformance/tutorial/15-modules-import-selectors.scala)

```scala
import util.Strings.{shout, initials as short, greeting}
@main def run(): Unit =
  println(shout("hello") + " " + short("Ada Lovelace") + " " + greeting)
```

```text
HELLO! A.L hello
```

**The wildcard.**

Fixture: [`tests/conformance/tutorial/15-modules-import-a-wildcard.scala`](../../tests/conformance/tutorial/15-modules-import-a-wildcard.scala)

```scala
import util.Strings.*
@main def run(): Unit =
  println(shout("hello") + " " + greeting)
```

```text
HELLO! hello
```

Fixture: [`tests/conformance/tutorial/15-modules-import-underscore-wildcard.scala`](../../tests/conformance/tutorial/15-modules-import-underscore-wildcard.scala)

```scala
import util.Strings._
@main def run(): Unit =
  println(greeting)
```

```text
hello
```

`*` and `_` are the same wildcard; `as` and `=>` are the same rename. Both
spellings exist because both Scala 2 and Scala 3 code is read here, and
rejecting one of them would buy nothing.

A `given` selector — `import M.given`, or `given` inside a selector list — is
parsed and **ignored** rather than rejected, because there are no givens to
import (D3, D93). `import util.Strings.{shout, given}` compiles and binds
`shout`.

One thing the list does not include, because it is worth its own section: a
selector may also name a **type**.

## 15.3 Where modules are found

A logical path becomes a relative file path — `util.Shapes` becomes
`util/Shapes.scala` — and that relative path is looked for in three places, in
this order:

1. **the importing file's own directory**, so a program and the modules beside
   it need no configuration at all;
2. **each colon-separated entry of `PROTOSCALA_PATH`**, in the order written;
3. **the working directory**, as a plain relative path.

The first regular file that exists wins, and the search stops there. Nothing
else is consulted: there is no classpath, no package index, no manifest and no
install-time registry.

```bash
$ PROTOSCALA_PATH=/opt/mylib:./vendor protoscala report.scala
```

A miss is reported with **every** path it tried, in the order it tried them,
which is almost always enough to see what went wrong — a misspelt segment, a
`PROTOSCALA_PATH` entry that is not where you thought, or a file one directory
higher than the import says.

Fixture: [`tests/conformance/tutorial/15-modules-a-missing-module.scala`](../../tests/conformance/tutorial/15-modules-a-missing-module.scala)

```scala
import util.Nope
@main def run(): Unit = println(1)
```

```text
15-modules-a-missing-module.scala:3:1: error: ImportError: no module found for
'util.Nope' (tried <the importing file's directory>/util/Nope.scala,
<each PROTOSCALA_PATH entry>/util/Nope.scala, util/Nope.scala)
```

The two bracketed parts are the only thing edited here: the first candidate is
the importing file's directory and the middle ones come from your environment,
so both are absolute paths that differ on every machine. Everything else —
the `ImportError:` prefix, the quoted logical path, the `(tried …)` list and
the trailing relative candidate — is what the binary prints, and the fixture
pins it.

A file that **exists** and is broken is a different outcome from a miss, and
the message says so: a parse error, a compile error or an exception from the
module's own top level is reported as an `ImportError` naming the module file
and the position inside it, never as "no module found". A miss means the
resolver looked and found nothing; a failure means it found the file and the
file was wrong.

## 15.4 Importing a type

A selector may name a class, and the name it binds is usable as a **type**: in
a constructor application, in a pattern, in a type test.

Fixture: [`tests/conformance/tutorial/15-modules-import-a-type.scala`](../../tests/conformance/tutorial/15-modules-import-a-type.scala)

With `util/Shapes.scala`:

```scala
case class Point(x: Int, y: Int)
def area(p: Point): Int = p.x * p.y
```

and beside it:

```scala
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))
```

```text
12
```

Fixture: [`tests/conformance/tutorial/15-modules-an-imported-type-is-a-type.scala`](../../tests/conformance/tutorial/15-modules-an-imported-type-is-a-type.scala)

```scala
import util.Shapes.{Point}
@main def run(): Unit =
  val p: Any = new Point(1, 2)
  println(p.toString + " " + p.isInstanceOf[Point])
```

```text
Point(1,2) true
```

Without a selector the class is still reachable through the module object, as
`Shapes.Point(1, 2)`, exactly as a class nested in an `object` is (chapter 6).

**Coming from Scala on the JVM.** This is the paragraph that explains why the
mechanism is shaped the way it is. protoScala has no static type checker (D4),
so almost everything here is resolved when it runs — but an `import` is not.
It is resolved **while the importing file is compiled**: the module file is
found, compiled and run, and its exported classes enter the importing unit's
type table before a single instruction of the importing file is emitted. That
is what makes `case Point(x, y) =>` work, because a constructor pattern needs
the class's field list at compile time, and a name bound at run time would
carry none. It is also why the cost of an import is paid once, at load, rather
than at every use.

**Coming from Python or JavaScript.** There is a small idea here with no
counterpart in either language: **a class is a value and a type at the same
time.** In Python, `from shapes import Point` binds one thing, the class
object, and you use it by calling it or by passing it to `isinstance`. Here the
same import binds *two* things under one name — the companion value, which is
what `Point(3, 4)` calls, and the type, which is what `case Point(x, y) =>` and
`p.isInstanceOf[Point]` consult. You never have to think about which one a
given occurrence means; the position decides. The practical consequence is the
one worth remembering: a case class from another file can be destructured in a
`match` in this file, which is the ordinary way data moves between modules
here.

## 15.5 Polyglot imports

protoScala also imports from **other runtimes** built on protoCore, through the
same `import`. Four prefixes are reserved for this, and each names a family:

| Prefix | Goes to | Example |
|---|---|---|
| `py.` | the provider registered under the alias `py` | `import py.numpy as np` |
| `js.` | the provider registered under `js` | `import js.d3 as d3` |
| `st.` | the provider registered under `st` | `import st.Transcript as T` |
| `clj.` | the provider registered under `clj` | `import clj.string as s` |

The prefix is **only** the first segment, and only when there are at least two.
`import py` is a module called `py`, not a prefix; `import py.a.b` asks the `py`
provider for `a.b`, not for `py.a.b`; and `import local.py.Helper` is an
ordinary file import of `local/py/Helper.scala`, because `py` is not in segment
zero. A prefix is a route, not a namespace.

**What has to be present.** protoCore's Unified Module Discovery keeps a
process-global registry of providers. A runtime built on protoCore registers
itself there under an alias, and from then on any other runtime **in the same
process** can import its modules. There is no socket, no serialization and no
subprocess: an imported value is a protoCore object and both sides see the same
object. protoScala registers itself, under the alias `scala`, so another
runtime can import a `.scala` module from it; and it loads provider plug-ins by
`dlopen` from `PROTOSCALA_PROVIDERS` and from
`<prefix>/lib/protoscala/providers`.

**What works, and what is missing, plainly.** One of the four prefixes has a
provider behind it: **`st`**. In a process that also holds a protoST runtime,
`import st.<module>` loads a Smalltalk module, its members bind by name, and the
values are literally the same objects protoST holds — the test that proves it
prints the same cell address and the same identity hash read from each runtime.
That is the claim in the paragraph above, verified rather than promised. It also
has limits worth knowing before you rely on it: a foreign **value** crosses, but
*calling* a protoST method from protoScala does not work (protoST interprets its
own methods), and the import must happen on the thread that built the protoST
runtime. `docs/INTEROP.md` §6 lists them all.

The other three — `py`, `js` and `clj` — have no provider. protoPython registers
the aliases `native`, `python_stdlib`, `compiled` and `hpy` — none of them `py` —
and, more deeply, resolves its Python environment from a thread-local and treats
its object space as one-per-process, which a co-resident protoScala session
contradicts; it also ships no numpy. protoJS and protoClojure register no provider
at all. So this is the honest state of the headline example:

Fixture: [`tests/conformance/tutorial/15-modules-no-py-provider.scala`](../../tests/conformance/tutorial/15-modules-no-py-provider.scala)

```scala
import py.numpy as np
@main def run(): Unit = println(np)
```

```text
15-modules-no-py-provider.scala:3:1: error: ImportError: no provider registered
for 'py'. Install the runtime that provides it, or point PROTOSCALA_PROVIDERS
at its plug-in
```

That is what you get at home, and it is the whole of what is missing for `py`:
protoScala routes the prefix, reaches the registry, and finds nothing under
that alias. The remaining work is in the *other* repositories — registering the
family aliases and making each provider serve a caller from another object space,
which protoST's now does — and it is tracked as **Track Y** in
[ROADMAP.md](../ROADMAP.md). Nothing in this dialect has to change for
`import py.numpy as np` to start working.

**Coming from Python.** `import py.numpy as np` is spelled to look like
`import numpy as np` on purpose. When the provider exists, the only difference
from the line you write every day is the three characters of the prefix that
say which language the module is written in. That is the whole design: one
`import` for every runtime in the family, with the prefix as the only marker,
rather than a foreign-function interface with its own vocabulary.

## 15.6 Named arguments across the boundary

The boundary itself is built and exercised, with a **test double** rather than
a real library. `tests/unit/probe_provider.cpp` is a small protoCore provider
that registers under the family alias `js` and answers exactly two logical
paths with hand-built objects. It is built by the test target, it is never
installed, and it answers `probe` — never a real library's data. A stand-in
that pretended to be numpy would be a green test asserting numpy's behaviour
against a stub, which is the one thing a test double must not do.

What it proves is the shape of the boundary. This is
[`tests/conformance/25-interop/stand-in-provider-keyword.scala`](../../tests/conformance/25-interop/stand-in-provider-keyword.scala):

```scala
import js.probe as p
@main def run(): Unit = println(p.echo(1, b = 2))
```

```text
echo a=1 b=2
```

It is not a fixture under `tutorial/`, because it runs only with the plug-in
loaded; `tests/CMakeLists.txt` points `PROTOSCALA_PROVIDERS` at the built
double for the `25-interop` fixtures that need it.

Read the call again. `b = 2` is an ordinary Scala named argument, and `echo` is
a C++ native inside a `dlopen`ed plug-in that has never heard of protoScala.
There is no adapter between them, and the reason is worth the paragraph.

protoScala compiles `f(b = 2)` into protoCore's `keywordParameters`, the fifth
parameter of **every** `ProtoMethod` in the kernel's calling convention — for a
Scala method, a native, and a foreign callable alike. The key is not the text
of the name: it is **the address of the interned symbol** for it, obtained with
`ProtoString::createSymbol`. Interning guarantees exactly one address per name
per object space, so the address *is* the name's identity, and because UMD is
in-process that address is valid in every runtime sharing the space. The callee
looks up the address it interns for its own parameter name and finds the
caller's argument. Nothing is translated because there is nothing to translate.

The sharp edge, which the double also exercises, is that the non-interning
constructors (`fromUTF8String` and its siblings) return a *different* pointer
for the same text — and protoCore embeds a short string in the pointer word
itself, so a key built the wrong way works by accident for a short name and
fails **silently** for a long one.
[`stand-in-provider-long-keyword.scala`](../../tests/conformance/25-interop/stand-in-provider-long-keyword.scala)
passes `aVeryLongKeywordName = 2` for exactly that reason. The convention is
written out in [INTEROP.md §7](../INTEROP.md).

The same fixtures pin the rest of the boundary's shape: a member selected out
of a foreign module (`import js.probe.{name}`), a foreign member that genuinely
holds `null` importing as `null` rather than being reported absent, and a C++
exception thrown inside the provider arriving at the Scala call site as a
catchable `RuntimeException` with its message intact — including one that is
not a `std::exception` at all, which without the translation would escape the
VM and take the process with it.

Two things a foreign module does **not** give you. It binds no **types**, so
`new`, a type pattern and `isInstanceOf` are unavailable on anything reached
through a prefix: a foreign value carries no class description, and its members
resolve by name at run time (D94). And a **wildcard** import of one is refused,

```text
a wildcard import of a foreign module is not supported: name the members you
need, as in `import probe.{a, b}` (D92)
```

because a foreign object's attribute names cannot be enumerated, and guessing a
set of names would fail silently at the first one that was wrong (D92). Name
the members you want.

## 15.7 When an import goes wrong

Four failures, with the message each one prints.

**No module.** The resolver looked in every candidate location and found no
file (§15.3):

```text
ImportError: no module found for 'util.Nope' (tried …)
```

**No member.** The module was found and loaded; the selector is not one of its
members.

Fixture: [`tests/conformance/tutorial/15-modules-a-missing-member.scala`](../../tests/conformance/tutorial/15-modules-a-missing-member.scala)

```scala
import util.Strings.{nope}
@main def run(): Unit = println(1)
```

```text
15-modules-a-missing-member.scala:3:22: error: ImportError: Strings has no member named 'nope'
```

A selector that names nothing is an error rather than a silent no-op, which is
the Scala rule and the useful one: a typo in a selector would otherwise become
a `Not found` at the first use, a page away from its cause.

**A cycle.** Two modules that import each other cannot both be loaded, because
an import is resolved by loading, and the second load would have to complete
before the first one did.

Fixture: [`tests/conformance/tutorial/15-modules-a-cycle.scala`](../../tests/conformance/tutorial/15-modules-a-cycle.scala)

With `util/Cycle1.scala` importing `util.Cycle2` and `util/Cycle2.scala`
importing `util.Cycle1` back:

```scala
import util.Cycle1
@main def run(): Unit = println(1)
```

```text
15-modules-a-cycle.scala:3:1: error: ImportError: …/util/Cycle1.scala:2:1: ImportError:
…/util/Cycle2.scala:2:1: ImportError: cyclic module import: util.Cycle1
```

The message nests one layer per module, so it reads as the chain that closed
the loop: this file imported `Cycle1`, whose line 2 imported `Cycle2`, whose
line 2 came back. Python tolerates a cycle and hands out a half-initialised
module; here it is refused, because the importing unit needs the module's
*complete* export list at compile time to bind a type from it. Break the cycle
by moving what both files need into a third module.

**No provider.** A family prefix routed correctly and nothing was registered
under that alias (§15.5):

```text
ImportError: no provider registered for 'py'. Install the runtime that provides
it, or point PROTOSCALA_PROVIDERS at its plug-in
```

All four are **compile-time** failures: they happen while the importing file is
being compiled, before any of its code runs, and the exit code is 1. At the
REPL the same failure leaves the session intact — a line that fails to compile
defines nothing — so a mistyped import at the prompt costs you the line and
nothing else (chapter 14).

## 15.8 What differs from Scala 3

Modules are the one area where there is no Scala 3 behaviour to match, because
Scala has no file-level modules: it has packages, resolved against a classpath
by a build tool. What follows is therefore less a list of divergences than a
list of choices, each with the reason it was made that way.

**D90 — a module's top level runs when it is imported.** Not lazily on first
member access, which is what an `object` does in Scala, but at the import site,
during the importing unit's compilation. Scala has nothing file-level to
diverge from; Python and JavaScript both run a module at import, and a module
that exists for its effects — `import mylib.Setup` — would otherwise never run
at all. The load is keyed by the file's canonical absolute path, so it happens
exactly once per file per session however many spellings reach it.

**D91 — a module *is* an `object`.** `util/Shapes.scala` becomes
`object Shapes`; its `def`s and `val`s are that object's members and its
classes are `Shapes.Point`. The name comes from the **file**, not from anything
written inside it, so renaming the file renames the module. Two visible edges
follow. A module may not define an `@main`: the file is a library rather than a
program, and an `@main` that was silently ignored would be a trap, so it is
refused with `a module may not define an @main method: Shapes is imported, not
run`. And the module's members are its top-level definitions, with no way to
mark one private to the file — the unit of privacy here is the class, as it is
everywhere else in this dialect.

**D92 — a wildcard import of a foreign module is refused.** `import py.numpy.*`
stops with a message that names the working spelling:

```text
a wildcard import of a foreign module is not supported: name the members you
need, as in `import numpy.{a, b}` (D92)
```

A foreign object's attribute names cannot be enumerated
through the API this crosses, and binding a guessed set of names would fail
silently at the first name that was wrong, long after the import. Named
selectors work, because the loader can read one named attribute. A wildcard
over a *protoScala* module is unaffected: its export list is known exactly.

**D93 — `given` selectors are parsed and ignored.** `import M.given` and
`import M.{given T}` bind nothing and are not an error, which is the same
treatment every other `given` gets (D3): there are no type classes to resolve
here, so there is nothing for the selector to bring into scope. It is ignored
rather than rejected so that a file written for Scala 3, where the selector is
routine, still compiles.

**D94 — a foreign module binds no types.** `new`, a type pattern and
`isInstanceOf` on a class reached through `py.`, `js.`, `st.` or `clj.` are
unavailable, and the error is late — `Not found: type probe` at the use rather
than at the import. A foreign value is a protoCore object with no class
description of the kind protoScala's own type table holds, and its members
resolve by name at run time, which is exactly what the type-mapping table of
[DESIGN.md §5](../DESIGN.md) already says. The detection is late, as a
late-binding platform's is; it is not silent.

**D95 — `--disassemble` resolves imports, and therefore runs them.** Printing
the bytecode of a file requires compiling it, compiling it requires resolving
its imports, and resolving an import means loading the module (D90). So
`protoscala --disassemble report.scala` runs the top level of every module
`report.scala` imports. That is surprising the first time and unavoidable
without a second, non-loading resolution path that would have to guess at
exports.

**D96 — an `import` is hoisted to its compilation unit.** Scala scopes an
import lexically: one written inside a block, a method or a template is visible
only there. Here it is visible for the **whole file**, wherever it is written,
and a binding made inside a block outlives the block. Lexical scoping needs a
scope-aware name resolver the compiler does not have, and D82's
extensions — which are session-wide for the same underlying reason — have the
same shape, so the two are scoped together or not at all. Write imports at the
top of the file and the difference never arises.

One more thing that is not a deviation but surprises Scala readers, so it
belongs here: **an extension method defined in a module is session-wide**
(D82). Importing the module that defines one makes it visible, as you would
expect — but it stays visible to code that never imported it, and code that
uses one without importing anything works as soon as *something else* in the
session has loaded the defining module. The import mechanism arriving did not
change that; scoping extensions was considered and declined, because with an
import hoisted to its unit (D96) a "scope" would mean per-unit, which is a
third behaviour that is neither Scala's nor today's.

**What did not diverge**, and is worth stating because a reader will look for
it: the five import forms have Scala's meaning; `as` and `=>` are both accepted
as renames and `*` and `_` are both accepted as wildcards; a selector that
names nothing is an error rather than a silent no-op; a selector may name a
type, as in Scala; and the longest-prefix rule that picks the module out of a
dotted path is what Scala's package-or-object resolution means.

---

Previous: [14. The REPL and tooling](14-repl-and-tooling.md) ·
Back to [the tutorial index](../TUTORIAL.md)
