# 17. Compiled modules — the same artefact from three languages

> **Implementation status.** `protoscalac` exists and works: it turns a `.scala`
> file into C++, `make` builds that into `module.so`, and
> `protoscala --run-module ./module.so` runs it. Classes, traits, objects, case
> classes, enums, `super` and `try`/`catch`/`finally` transpile; three things are still
> refused **at transpile time**, with a message and a line number, and never
> mistranslated: `import` (D123), `await` (D113), and named arguments and default
> values (D121). A compiled module **is** importable — `import util.Strings` finds
> `util/Strings.so` on `PROTOSCALA_MODULE_PATH` — but it binds *late*, like any foreign
> module, so a pattern match against one of its classes does not compile. Everything
> below either runs or says plainly that it does not.

Chapters 1–16 are about writing Scala. This one is about what happens to it
afterwards, and it is short because there is only one idea in it:

> A protoCore module is a shared library with one init symbol. Three toolchains
> produce one — a hand-written C++ file, `protopyc` from Python, `protoscalac`
> from Scala — and **the language it was written in is an implementation detail of
> the module**.

## 17.1 From a file to a `.so`

Take the smallest possible program:

Fixture: [`tests/conformance/tutorial/17-compiled-a-module-runs.scala`](../../tests/conformance/tutorial/17-compiled-a-module-runs.scala)

```scala
@main def greet(): Unit =
  println("Hello from a compiled module!")
```

Interpreted, that is `protoscala greet.scala`. Compiled, it is two commands:

```text
$ protoscalac greet.scala --build-so
$ protoscala --run-module ./module.so
Hello from a compiled module!
```

`--build-so` writes three files: `greet.cpp`, a `Makefile`, and `module.so`. Use
`--emit-cpp` if you want to read the C++ without building it, and `--emit-make` if
you want the `Makefile` but would rather run `make` yourself.

The target is always `module.so`. Rename it to the name you want the module found
under before you put it on a search path.

**On Windows** the same two commands work from a Developer Command Prompt for Visual
Studio (or from any prompt, when Visual Studio with its C++ workload is installed:
`protoscalac` finds it). The `Makefile` is then an NMake file, `nmake`, `cl` and
`link` build it, and the module is `module.dll`:

```text
> protoscalac greet.scala --build-so
> protoscala --run-module module.dll
Hello from a compiled module!
```

## 17.2 What the C++ looks like, and what it does not do

Two things are worth seeing once, because they are the whole design.

**It calls the runtime; it does not replace it.** A line of Scala becomes a call
into protoScala, not an expansion into C++ arithmetic:

```cpp
S[B + 7] = gen::add(C, S[B + 7], S[B + 8]);
```

`gen::add` is protoScala's `+` — the same one the interpreter runs, from the same
file. So `2147483647 + 1` is still `2147483648` in a compiled module, because
there is **one** implementation of `+` and both paths call it. That is also why
transpiling is not a performance feature: the send still costs what a send costs.

**Nothing is held in a C++ variable.** Every value lives in a slot of the frame's
traced locals, indexed by a number the compiler worked out. There is no temporary,
which is why a generated module cannot lose a value to the collector by accident.

```text
$ ldd ./module.so | grep proto
        libprotoScala.so.1 => ...
        libprotoCore.so.3  => ...
```

That line is the honest price: **the module brings a protoScala runtime with it.**
`protoscalac --report-purity greet.scala` will tell you why, naming the reasons:

```text
greet.scala needs protoScala:
  PUSH_GLOBAL println at line 2    a prelude or module global
verdict: not protoCore-pure. Loading this module adds a protoScala runtime and
therefore one ProtoSpace term to the process sizing rule (protoCore MemoryModel.md).
```

## 17.3 What it refuses, and why that is the good news

A class transpiles:

Fixture: [`tests/conformance/tutorial/17-compiled-a-class-runs.scala`](../../tests/conformance/tutorial/17-compiled-a-class-runs.scala)

```text
$ protoscalac shapes.scala --build-so && protoscala --run-module ./module.so
25.0
```

Ask it for something it does not do yet, and it says so instead of guessing:

```text
$ protoscalac uses-a-default.scala
uses-a-default.scala:1: error: a parameter with a default value is not supported by
protoscalac yet (D121): defaults are bound by the callee's prologue, which a
transpiled frame does not run
```

It writes **no `.cpp` at all** when it refuses. That is deliberate: a half-written
file that a later `make` compiles into *something* is worse than no file. And a
refusal is always a refusal — the one thing a transpiler must never do is produce
a program that runs and gives a different answer, so every gap is a message and
never a guess.

`try`/`catch`/`finally` works, including the case that is easy to get wrong: an
exception raised *inside* a handler, caught by an enclosing `try` in the same function.
The frame keeps one retry loop for its whole body, so its handler table is still there
when the handler itself fails.

If you are wondering how the gaps were found: by running protoScala's own 922
conformance fixtures down both paths and comparing, and then by running the Scala
3 compiler's own test corpus down both paths as well. Most of the refusals exist
because that comparison caught a wrong answer, not because anyone predicted them.

That second harness is also what says how far this has got, and it is worth the
two numbers. Of the 191 corpus tests the **interpreter** passes, **176** pass
transpiled, with **no** case where the two paths disagree. Of our own 922
fixtures, 704 run transpiled. The gap between those two ratios is the honest
reason the corpus run exists: our fixtures are full of top-level `def`s, and real
Scala is full of `object X { def main … }`.

## 17.4 Importing one

A `.so` is not only something you run; it is something you `import`. Put it where the
runtime looks and the ordinary import finds it:

```bash
mkdir -p modules/util
protoscalac Strings.scala --build-so --module-name util.Strings -o build
cp build/module.so modules/util/Strings.so
PROTOSCALA_MODULE_PATH=./modules protoscala importer.scala
```

```scala
import util.Strings
@main def run(): Unit = println(Strings.shout("hello"))
```

`protoscala --version` prints the directories it searches, which is the answer to "why
did my import miss?".

Three rules worth knowing before you rely on it.

**A `.scala` beside the `.so` wins.** Compiled modules are searched *after* source ones,
so dropping one in cannot change the meaning of an import that already worked. If you
want the compiled one, remove the source from the path.

**A module cannot be a program.** A `.so` built from a file with an `@main` is refused as
an import, with the same message a source module gets: `a module may not define an @main
method`. Run it with `--run-module` instead.

**The import binds late.** Members work — `Strings.shout("hello")` is an ordinary send —
but the *types* do not come across. `import util.Shapes` of a compiled module will not let
you write `case Point(x, y) =>`, because a compiled module hands over an object, not a
description of its classes. A source module does both. This is the one place where
compiling a module costs you something.

## 17.5 The reason any of this exists: calling in from another language

Everything so far has been about producing the same artefact. This is the part you
cannot get any other way.

A transpiled module's top-level `def`s are `proto::ProtoMethod`s — plain function
pointers in protoCore's own calling convention — and `proto_module_init` returns a
module object carrying one per exported function, under its Scala name. So a
program that knows **protoCore and nothing about protoScala** can call your Scala
code:

```cpp
// The whole recipe. No protoScala header, no ExecutionEngine, no bytecode.
const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, "add");
const proto::ProtoObject*  fn = module->getAttribute(ctx, key);
const proto::ProtoObject*   r = fn->asMethod(ctx)(ctx, fn->asMethodSelf(ctx), nullptr, args, nullptr);
```

Compare that with what it replaces. A value could already cross a runtime boundary
with no copy — Track Y measured one cell, two runtimes, the same hash printed from
both. A **function** could not be *called* across, because a bytecode-backed
function is a module address plus the engine that walks it, and the foreign side
has neither. Compiling the module is what turns the second into the first.

Two things to know before you rely on it. Only a top-level `def` is exported, and
only one that captures nothing: a bare cell is called with no receiver, so a
closure would read an empty environment, and refusing to export it is better than
publishing one that misbehaves. And because a cell has nowhere to record which
runtime owns it, a process may run **one** protoScala session while exported cells
are in use; a second one is refused when the module links, with a message saying
why.

`ctest -R interop/foreign-call` is the test, and it is worth reading rather than
trusting: the caller file is *grepped* for the string `protoScala` and the test
fails if it appears, its four answers are compared with what the interpreter
prints from the same source file, and four deliberate sabotages of the machinery
each turn it red. [`docs/INTEROP.md`](../INTEROP.md) §8 has the limits.

---

## For the Scala programmer

You have shipped a jar. This is not that.

- There is **no linking step across modules** and no classpath. A module is one
  `.so`; what it needs from protoScala it takes from `libprotoScala.so`, which
  `ldd` shows you.
- The `@main` you already write is what makes the module runnable: with one, the
  module exports a program entry point; without one, it is a library. A module
  that declares an `@main` is deliberately **not importable** — the same rule D91
  already applies to source modules, with the same message.
- There is **no ahead-of-time type specialisation**, because there are no static
  types to specialise on (D4). The transpiler performs no type inference at all;
  it consumes bytecode, which has none.
- Think of it as the JVM's `-Xshare` rather than as a native-image compiler: the
  front end is gone, the dispatch is not.

## For the Python or JavaScript developer

You have seen this shape before. It is a **native extension**: a `.so`, a
`dlopen`, one init symbol, one registry entry.

There is one difference worth naming, and it is the point of the chapter. Writing
a CPython extension means learning a C API — `PyObject*`, reference counts,
`PyArg_ParseTuple` — and converting at the boundary. Here there is **no C API to
learn and no conversion**: a value is a protoCore cell on both sides, so a list
built in one module *is* the list the other module reads, at the same address.
Track Y measured exactly that — one cell, two runtimes, the same hash printed
from both.

What the compiled form adds on top of that is the other direction. A value could
already cross; a **function** could not be *called* across, because a
bytecode-backed function needs the runtime that compiled it. A transpiled function
is an ordinary native method pointer, so any runtime in the family can call it. If
you have written a Python C extension so that a Python library could be used from
C, this is the same move, made once for every language in the family.

Honest caveat, because it is easy to over-read: **within one address space and one
`ProtoSpace`.** The call itself is demonstrated (§17.5, `ctest -R
interop/foreign-call`). A cross-*space* call is not, and it is a real open question
rather than a missing test: a `proto::ProtoMethod` is a raw code pointer, while the
arguments and the result are cells that belong to the space that allocated them. Nor
has any *other* runtime taken the call up — protoPython, protoJS and protoST would
each need a change of their own — so what is proven is that the call needs protoCore
and nothing else, not that anyone is making it yet.
