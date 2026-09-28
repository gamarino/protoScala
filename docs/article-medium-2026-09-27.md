# protoScala: a dynamic Scala 3 dialect without the JVM — and a question for you

*2026-09-27*

Two things before anything else, because they change how you should read the rest.

**I am not a Scala programmer.** I have built language runtimes, and I read the Scala 3 reference carefully, but I do not have the instincts of someone who writes Scala every day. That is exactly why I am publishing this instead of polishing it further in private: the decisions I am least equipped to make are the ones a Scala programmer makes without thinking.

**The inspiration is Scala Native.** The idea that Scala does not have to mean the JVM is not mine. Scala Native demonstrated it, and it is the reason this project exists at all. What I did with that idea, though, goes somewhere Scala Native deliberately does not — and that difference is the whole substance of this post.

## The objection you are already forming

Let me put it before you do, because if I do not, everything after it reads as evasion.

Scala Native keeps the type system and drops the JVM. It is an ahead-of-time compiler: real Scala, fully typechecked, compiled to native code. Your program means the same thing; only the machine underneath changes.

protoScala does the opposite. It keeps the syntax and drops the type system. Scala 3 source goes in — braces or significant indentation, your choice — and **types are parsed and erased**. There is no static typechecker. There is no JVM, no sbt, no Maven, no Java interop.

For a Scala programmer that is close to heresy, and the objection is not a quibble: *the type system is Scala*. Implicits, given/using, opaque types, variance, the whole compile-time apparatus — that is not decoration on top of a syntax, it is the language. Take it away and what is left, a skeptic would say, is Python with worse ergonomics.

I think that objection is serious and I do not have a knock-down answer. What I have is a reason, and the reason is the runtime underneath.

## protoCore, and why the dynamism is not a shortcut

protoScala is not a standalone project. It is one of five language runtimes on a C++ object kernel called **protoCore**, and protoCore is prototype-based and late-binding *by construction* — Lieberman-style prototypes, dynamic inheritance, attributes resolved at run time through a chain.

That is not a limitation I worked around; it is the thing being tested. protoCore's claim is that it can be a solid base for implementing *any* language, and each new language on top exposes what it is missing. protoScala is explicitly a **platform validation project**. Two capabilities went into the kernel because Scala needed them and they were absent: `ProtoMap`, a persistent map with GC-traced object keys, and `ProtoMPSCQueue`, a lock-free GC-traced actor mailbox. Both now serve the Clojure and Smalltalk runtimes too.

So the honest framing is not "Scala, but dynamic". It is: **a Scala-shaped surface on a late-binding runtime**, and the open question is whether that is useful or a contradiction in terms.

What the runtime gives you in exchange for the types is not nothing:

- **Persistent immutable data by default.** protoCore's collections are immutable with structural sharing — AVL trees and ropes. `case class` and the functional collections map onto that directly rather than being emulated on top of mutable structures. `val stock = Map(...)`; `stock + ("pears" -> 12)` returns a new map and `stock` is untouched, because that is what the underlying structure does, not because a wrapper copies.
- **Native actors with no GIL.** Real OS threads, no global lock. Messages are pointers to immutable data, so there is nothing to serialize and nothing to copy. Mailboxes are lock-free with three priority bands, and `await` inside an actor suspends cooperatively instead of parking a thread.
- **Integers that do not overflow.** `factorial(100).toString.length` is 158.
- **Start-up measured in tens of milliseconds**, which makes Scala plausible for scripts and REPL work in a way the JVM never quite managed.

## Where this actually is, stated plainly

This is the part I want you to see early, because everything above is worth nothing if the numbers are hidden at the bottom.

**The Scala 3 corpus: 191 of 601 reachable tests pass — 31.8%.** I took the Scala 3 compiler's own `tests/run` corpus, 1654 single-file programs from dotty at `a68b419c`. Not a corpus I chose or curated; theirs. Since the denominator is the whole story, here is how those 1654 files divide, by a triage that records **which construct it found in each file** rather than excluding by wildcard:

| | tests | share | pass rate |
|---|---:|---:|---:|
| **Unreachable — JVM-bound** (Java interop, `classOf`, `getClass`, reflection, `Serializable`, `synchronized`, `System.out`, or a checkfile expecting a JVM class name) | 410 | 24.8% | 1.0% |
| **Unimplemented features** (`given`/`using` 197, `inline`/macros 153, lazy collections 77, `Seq`/mutable 75, `Array` 61, anonymous classes 39, …) | 643 | 38.9% | 3.3% |
| **In scope — no out-of-scope construct found** | 601 | 36.3% | **31.8%** |

So: **31.8% of what is reachable (191 of 601), and 13.1% of the corpus as a whole (216 of 1654).** Both are true and neither is the interesting one alone. Note that 216 is more than 191: **25 tests the triage ruled out of scope pass anyway**, which is the first thing to know about the triage — it is conservative, and it under-reports what works.

The part worth arguing about is the middle row. About **21% of the whole corpus is compile-time machinery** — the 197 `given`/`using` tests and the 153 `inline`/macro tests. A dialect that erases types cannot implement those in any honest sense, so they belong with the first row far more than with a to-do list: **roughly 46% of this corpus is closed to protoScala by what it is**, not by what it has not got round to. The rest of that row — collections, `Array`, anonymous classes — is genuinely missing work.

**A check on the triage itself, because a classification that cannot be wrong is worth nothing.** Its error rate is measurable: 1.0% of the "impossible" row passes anyway (4 tests of 410) and 3.3% of the "unimplemented" row does (21 of 643). Those 25 are cases where a regex matched a mention that did not matter — a `Serializable` never used, a `Seq` in a comment's neighbourhood. The errors all run one way, towards calling something unreachable that is not, so the 601 is a floor and the true reachable set is slightly larger.

I will also say how I got these numbers wrong once, since it is the kind of mistake that survives into published posts. My first draft of this section reported 11.5% for the whole corpus, and a flawless 0.0% for the impossible row. Both came from computing against an **older run of the corpus** that was still sitting in the working directory next to the current one — and the 11.5% was worse than stale, because I derived it arithmetically (31.8% of 601, over 1654) instead of counting the passes. Counting them gives 216, not 191. Four "impossible" tests passing is a less tidy story than none, and it is the true one.

**The start-up target is half met, and I measured it for this post.** The goal is under 25 ms to a prompt and about 20 MB RSS. On the installed 0.6.0 binary, median of 31 verified runs: **23.32 ms** for a script and **23.72 ms** for the REPL — both inside the target, and measured on a machine carrying a load average near 1.9 rather than an idle one. The **memory half is not met**: about 24.1 MB RSS against a ~20 MB target, and nobody has yet looked for where those 4 MB are. The variance is also wide — the slowest of those 31 runs was 37.9 ms, and the stricter of the two harnesses, which judges every sample rather than the median, still calls the script case a straddle. So: met by median, straddling by worst case, and over budget on memory. That is three different answers to "is it fast to start?", and a single word would have hidden two of them.

**Polyglot imports work — across one boundary of four.** This is the capability I am least able to be brief about, so: `import st.<module>` genuinely crosses from protoScala into the Smalltalk runtime. Not a bridge, not a serializer, not an FFI — the **same object at the same address**, printed from both runtimes to prove it, with a named argument arriving in the callee's keyword parameters unchanged. That is demonstrated and mutation-tested, and it is the part of this project I would defend hardest.

The honest other half: **no runtime in the family registers a `py`, `js` or `clj` provider yet.** So `import py.numpy as np` parses, routes to the right place, and tells you `ImportError: no provider registered for 'py'`. One pipe of four is connected. The mechanism is real and proven across a live runtime boundary; the reach is not there.

**118 recorded deviations from Scala 3**, numbered up to D123. Every place the implementation knowingly differs from the reference is written down rather than left for you to discover.

The in-house suite registers **2313 cases**. CI builds protoCore, protoST and protoScala from clean on every push and runs 2310 of them — three clock-dependent cases run in a separate job — reporting 0 failed and 7 skipped, the skips being embedder-conformance rules that need process isolation. But the limit that matters is not the count: **every one of those tests was written in this repository.** They measure faithfulness to my model of Scala, not to Scala. That is precisely why the dotty corpus number above is the one I led with, however much worse it looks — it is the only figure here that someone else wrote the answers for.

One caveat on the corpus figure that I owe you, because the project discloses it for the transpiler and not here: **84 of those 191 in-scope passes — 44% — have no `.check` file**, so they pass on "exited 0 and printed nothing unexpected" rather than on matching expected output. That is dotty's own rule and the stricter of the two variants, so it is defensible, but 44% is a large enough share that you should know it before reading 31.8% as 31.8% of *verified* behaviour.

Until I wrote this post that number was also **not reproducible from a clone**: the harness, the triage rules and every result file lived in a scratch directory outside the repository, with absolute paths baked in. In a project whose own standard is "prefer the command to the number", that was the fairest thing anyone could have held against it. It is committed now, parameterised, with the per-test verdicts of the published run, so `python3 tools/corpus/score.py --csv --both` prints both figures under both scoring rules — including the lenient one, which reads 32.1% rather than 31.8%. I publish the stricter.

And the honest summary: **this is not production software.** Phase 6 is complete at 0.6.0; Phase 7 — the transpiler — is a **first cut**, and the version has deliberately not been bumped for it. There is no release tag. The project is open for review, not for deployment.

## The rest of the family

If you came for Scala, the more interesting thing may be what sits beside it. The same kernel carries five languages, and the point is that they share objects rather than protocols:

- [**protoPython**](https://github.com/gamarino/protoPython) — a GIL-free Python 3.14-compatible runtime. The most mature of the five.
- [**protoJS**](https://github.com/gamarino/protoJS) — JavaScript on the same object model, measured against test262.
- [**protoST**](https://github.com/gamarino/protoST) — Smalltalk; the runtime that protoScala's cross-language import actually talks to today.
- [**protoClojure**](https://github.com/gamarino/protoClojure) — Clojure, with atoms built on the kernel's compare-and-swap and real-thread futures.
- [**protoScala**](https://github.com/gamarino/protoScala) — this one.

The kernel they all sit on is [**protoCore**](https://github.com/numaes/protoCore).

And then there is the one I left out of my own list until someone pointed it out, which is the most natural consumer of all: **C++**. protoCore *is* a C++20 kernel with a public header, so plain C++ can drive it directly — no interpreter, no bytecode loop, no symbol-table dispatch. That is [**protoCpp**](https://github.com/gamarino/protoCpp), and it exists for two reasons that matter more than the language count.

The first is that it is the **embedder's reference**: six short examples, 30 to 98 lines each, covering the primitives an application actually needs — building and reading a list, SmallInt arithmetic through the inline fast-path helpers, atomic compare-and-swap on a mutable attribute, persistent collections with structural sharing, OS threads managed by the kernel, and a hand-built one-actor system. If you want to know what protoCore is without a language in the way, that is where to look.

The second is the one I would point a sceptic at. protoCpp is **the ceiling**, and it is measured as a decomposition: every benchmark has a pure C++ version that does not link protoCore at all, and a protoCpp version doing the same work through the kernel. So the gap between them is the cost of **the kernel**, and the gap between protoCpp and a language runtime is the cost of **the language layer**. When you ask "how fast is protoScala really?", that is the honest way to answer it — two separate questions, measured separately, instead of one number that hides which half you are paying for.

And it publishes the answer rather than implying one. I re-measured it for this post, against protoCore 2.5.0: going through the kernel costs **3.7× to 24.8×** the time of plain C++ doing the same work, in whole-process wall time.

| | C++ floor | through the kernel | ratio |
|---|---:|---:|---:|
| `list_append_loop` | 4.57 ms | 16.85 ms | 3.7× |
| `str_concat_loop` | 4.20 ms | 16.25 ms | 3.9× |
| `multithread_cpu` | 4.58 ms | 26.88 ms | 5.9× |
| `int_sum_loop` | 6.96 ms | 73.03 ms | 10.5× |
| `call_recursion` | 3.07 ms | 35.41 ms | 11.5× |
| `attr_lookup` | 7.19 ms | 178.46 ms | 24.8× |

That is a wide and unflattering range, and it is the most informative number anyone has about protoCore: whatever a language on top costs, this is the floor it is standing on. Attribute lookup is the worst cell by a distance, which is exactly where you would expect a prototype chain to cost you.

The inline SmallInt helpers are the one lever an embedder has, and they are worth having: **57% off `call_recursion`** (35.41 → 15.14 ms), **37% off `int_sum_loop`** (73.03 → 46.25 ms), 24% off `multithread_cpu`. Each fast variant prints the same computed result as the plain one — 49999995000000, 75025 — so the saving is not a skipped workload, which is a failure mode this project has been bitten by before.

Two honest qualifications on that table. It was measured on a machine at a load average near 1.4, not an idle one, so read the ratios rather than the absolutes; and the runner times the binaries with their output discarded, so it does not itself check that the work happened. I checked by hand — all twelve pairs exit 0 and every C++/kernel pair prints an identical result — but a harness that cannot catch a crashed cell is a gap, and by this project's own rule a benchmark runner should verify what it timed.

For reference, the previous published figures were **3.87× to 32.60×** from 2026-06-15. The worst case improved by about a quarter over three months of kernel work; the best case did not move.

It is also where the project is most explicit about what it does not get for free. Removing the language layer still leaves you paying for the concurrent collector, the per-thread attribute cache and the sharded mutable-root table, allocation and structural sharing in the AVL-backed collections, SmallInt encoding and decoding on every numeric operation unless you use the inline helpers, and compare-and-swap on every mutable update. "No language overhead" is not "no overhead", and protoCpp's own README says so before any number appears.

The rest of protoCpp's published table — the comparisons against CPython and the Python runtimes — is still the **2026-06-15 snapshot**, and I have not re-run it. Read those as dated; only the kernel-cost table above is from today.

When a value crosses between two of these, nothing is marshalled. It is the same protoCore object at the same address, and a named argument arrives in the callee's keyword parameters unchanged, because keyword arguments are part of the kernel's calling convention rather than each language's invention. That is the capability I find genuinely worth arguing about, and it is the one a Scala programmer is most likely to have a use for: calling into a Python or JavaScript library without a bridge, a serializer or an FFI.

It is also the capability that is least finished. See above.

## About how this was built

This project was built with heavy AI assistance — Claude, working in the repositories over months. Every commit carries the co-authorship trailer, so you would find out in two minutes of reading the history; I would rather say it than have it discovered.

I mention it for a better reason than disclosure, though. It is how a non-Scala programmer got a Scala dialect to a 2313-case suite that passes, and it is also where the most transferable engineering content of the project lives. There is a curated field-notes document in protoCore recording fourteen real defects and what they cost, and a running theme in it that I did not expect: **eleven tests that could not fail.** Tests that were green because they asserted nothing, or measured the wrong thing, or ran after the condition they were checking had been cleaned up. Each one was found by deliberately breaking the code and watching the test stay green.

Before writing this post I had the documentation audited against the code, on the theory that anything a sharp reader could falsify in ten minutes should be found by me first. It came back with about eighteen places where the docs contradicted themselves or lagged the measurements — most of them live on the public `main` branch. The README claimed the start-up target was missed while its own performance section, 830 lines below, said met and explained that the missed reading had been retracted as a load artefact. It advertised 1344 tests when the tree has 2313. It said a component was not shipped that the binary itself reports as shipped. The installation guide told you to install a protoCore version that makes the build fail with a fatal error.

Those are fixed now. But the auditor's one-line summary is the part I want to repeat, because it says where the risk in this project actually sits: **an unusually honest benchmark corpus with a stale README wrapped around it.**

That distinction matters more than the eighteen fixes. Every contradiction was maintenance lag — documentation that failed to keep up with measurements that were themselves done carefully. Not one of them inflated a result. The benchmark reports state machine, kernel, date, the commit SHAs of all five runtimes, sample counts and min–max spreads; cells that cannot verify their own work print `FAILED` rather than a fast time, and the README volunteers that one protoScala column failed and that "its 4.45 ms cold-start median is a crash, not a win". The transpiler section says outright that it is "measurably not a speed-up — geomean 1.244× slower". The one invalid number that did survive into the current tables makes protoScala look **worse**, not better.

I mention all of this because I am about to ask you to spend attention on an early project, and you are entitled to know which kind of early it is.

If you take one thing from this post and it is not protoScala, let it be that: mutate your fixture and confirm it goes red, or you do not know what your green means.

## What I am asking you

Two questions, in this order, because the second one is presumptuous without the first.

**First: is this direction worth pursuing at all?** A Scala-shaped language without Scala's type system, on a late-binding prototype runtime. Tell me if that is a contradiction. I would genuinely rather hear "this is the wrong shape, and here is why" now than keep building for another six months. The argument I find most persuasive against it is the one I opened with, and I have not answered it.

**Second: if it is worth pursuing — what has to work?** This is where I am least qualified and where a five-line comment from you is worth a week of my guessing. Concretely:

- **Which parts of the type system are load-bearing at run time** — where erasing types changes behaviour, not just checking? To show the shape of answer that helps: method overloading looked fatal, and the way out was a principle rather than a workaround — **arity is the part of a Scala signature that survives erasure**, so a top-level overload dispatches on argument count, and the five cases where count cannot identify an alternative (two alternatives of the same arity, a default value, a repeated parameter, several parameter lists, a by-name parameter) are **refused with a diagnostic instead of guessed at**. Overloading inside a template is still an error. Tell me what else has that shape, and whether refusing is acceptable to you or worse than not having the feature.
- **The smallest set of Scala 3 features whose absence makes this a non-starter.** My guess is that `for`-comprehensions matter most, because desugaring to `map`/`flatMap`/`withFilter` needs no types at all. I suspect `given`/`using` is the hard one — someone has already suggested resolving it as dynamically scoped context, Clojure-style, rather than by type-directed search, and I do not yet know whether that is clever or a trap.
- **Is a dynamic dialect useful to you as a scripting companion to real Scala**, rather than a replacement? That is the use case I think is strongest and the one I am most likely to be fooling myself about.
- **Where should the collections library stop pretending?** Partial fidelity to `scala.collection` may be worse than an honestly smaller API under a different name.

The repositories are public and the issues are open. Direction criticism is worth more to me right now than bug reports, and "I would not touch this until X" is the most useful comment of all.

So, the one question I would most like answered in the comments: **is Scala's syntax without Scala's type system a useful heresy, or a contradiction in terms?**

---

*The kernel is at [github.com/numaes/protoCore](https://github.com/numaes/protoCore), the five runtimes at [protoScala](https://github.com/gamarino/protoScala), [protoPython](https://github.com/gamarino/protoPython), [protoJS](https://github.com/gamarino/protoJS), [protoST](https://github.com/gamarino/protoST) and [protoClojure](https://github.com/gamarino/protoClojure), and the direct C++ path at [protoCpp](https://github.com/gamarino/protoCpp). protoCore's failure write-ups are being prepared as teaching material for a short course at ITBA — the first cohort has not run yet — which is part of why the failures are documented as carefully as the successes.*
