# 17. Compiled modules — the same artefact from three languages

> **Implementation status.** `protoscalac` exists and works: it turns a `.scala`
> file into C++, `make` builds that into `module.so`, and
> `protoscala --run-module ./module.so` runs it. It is a **first cut**, and it
> refuses more than it accepts: classes, traits and objects (D118), `import`
> (D123), `try`/`catch`/`finally` (D120), `await` (D113), named arguments and
> default values (D121), and `super` (D122) are each refused **at transpile time**,
> with a message and a line number, and never mistranslated. `import`ing a compiled
> module is not available yet either — the provider that would do it is specified
> and not built — so `--run-module` is the whole loading surface today. Everything
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

Ask it to compile a class today and it says so:

Fixture: [`tests/conformance/tutorial/17-compiled-a-class-is-refused.scala`](../../tests/conformance/tutorial/17-compiled-a-class-is-refused.scala)

```text
$ protoscalac shapes.scala
shapes.scala:1: error: a class, trait or object is not supported by protoscalac yet
(D118): MAKE_CLASS needs the ClassSpec rebuilt from the static tables
```

It writes **no `.cpp` at all** when it refuses. That is deliberate: a half-written
file that a later `make` compiles into *something* is worse than no file. And a
refusal is always a refusal — the one thing a transpiler must never do is produce
a program that runs and gives a different answer, so every gap is a message and
never a guess.

If you are wondering how the gaps were found: by running protoScala's own 919
conformance fixtures down both paths and comparing, and then by running the Scala
3 compiler's own test corpus down both paths as well. Four of the six refusals
exist because that comparison caught a wrong answer.

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
`ProtoSpace`.** A cross-*space* call is not demonstrated, and neither, yet, is the
cross-runtime call itself — the argument is sound, the artefact exists, and the
test that would prove it is not written.
