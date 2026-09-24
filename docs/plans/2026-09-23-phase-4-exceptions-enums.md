# protoScala Phase 4 — Exceptions, `super[T]`, Enums, Named Arguments and Extension Methods Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** complete core semantics (ROADMAP "Phase 4"): `try`/`catch`/`finally`/`throw` with pattern-matched handlers; native error translation, so a protoCore-level failure surfaces as a catchable Scala exception; `super[T].m`, which names the ancestor explicitly instead of taking the next one in the linearization; `enum` and sealed hierarchies with `values`/`ordinal`/`valueOf`/`fromOrdinal`; named and default arguments for Scala-defined methods **and for every other callable protoCore can reach, foreign ones included**; extension methods; and templates nested in an `object`. Release **0.5.0** (Phase 5 took 0.3.0 out of order and Phase 3 takes 0.4.0).

**Architecture:** The VM stays recursive with frame snapshots (DESIGN §3.6; the mechanism Phase 5's cooperative `await` uses) and **nothing about that changes**. Exceptions are added as a per-frame retry loop *outside* the C++ catch block: `ExecutionEngine::execute` and `resumeFrames` stop calling `runLoop` directly and call a new `runFrame`, which loops `try { return runLoop(...); } catch (ScalaThrow&) { … continue; }`. Because the handler is entered by `continue` — after the catch block has been left — the Scala `catch` and `finally` bodies run with **no live C++ exception handler**, an ordinary `ip`, and therefore exactly the same cooperative-`await` behaviour as any other bytecode. Each `BytecodeModule` carries a handler table of `{startPc, endPc, handlerPc, stackDepth, slot, kind}` entries in innermost-first order; `THROW` raises, `RETHROW` re-raises the value a `finally` saved, and a `catch` body is an ordinary pattern-match cascade ending in `RETHROW` when no case matches. `FutureYield` is not a `std::exception` and `runLoop`'s `catch (FutureYield&)` stays **first**, so a suspension passes straight through `runFrame` untouched and **no `finally` runs on a suspension** — a suspended frame is going to be resumed, not abandoned. A failed future resuming a suspended actor raises *into* the frame at the pc of the in-flight `await` call, so `try { f.await } catch { … }` works across a suspension, which retires D50. `enum` is lowered entirely in the frontend to a sealed class, one child per case and a companion; extension methods install their member on the receiver's prototype at definition time (D6); named arguments travel in protoCore's own `keywordParameters` `ProtoSparseList`, keyed by the address of the interned parameter-name symbol, which is the kernel convention every runtime in the family already speaks.

**Tech Stack:** C++20, CMake ≥ 3.20, protoCore ≥ 2.1.0 (`find_package(protoCore 2.1 CONFIG)`, sibling-tree fallback `../protoCore/build_release`), GoogleTest 1.14, libreadline; Python 3 for `benchmarks/run_benchmarks.py`; Scala 3.9.0 at `/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0` (`bin/scalac` plus `java -cp "$SCALA_HOME/lib/*:out"`, **never** `bin/scala`) as the reference for expected outputs; OpenJDK 21 for that reference only.

**Spec:** `docs/DESIGN.md` §1.1 (P1–P6), §3.5 (opcodes; the `96..127` band is reserved for exceptions), §3.6 (recursive VM, frame snapshots, native stack guard), §4.3 (linearization), §4.4 (`super`), §4.5 (case classes, `sealed trait` / `enum`), §4.6 (`ProtoTuple` prohibition), §5.2 (named and default arguments), §5.3 (pattern matching), §7 (**exceptions**), §8.3 (cooperative `await`), §9 (UMD), §10 (testing), §11 (R3, R5); `docs/ROADMAP.md` "Phase 4" and the Documentation track; `docs/LANGUAGE.md` §2, §3; `protoST/docs/archive/design-specs/2026-05-20-exceptions.md` as the precedent this plan follows in part and departs from in part (Task 0 A0-1).

---

## Global Constraints

Every Phase 1, Phase 2, Phase 3 and Phase 5 constraint still holds (`plans/2026-09-22-phase-1-core-language.md`, `plans/2026-09-22-phase-2-object-model.md`, `plans/2026-09-23-phase-3-collections.md` and `plans/2026-09-23-phase-5-actors.md`, "Global Constraints"); repeated here with the Phase 4 additions.

**Workspace safety.**

- **Nothing is written outside `/home/gamarino/Documentos/proyectos`.** No `/tmp`, no `$HOME`, no `cmake --install` outside the workspace. Tests create temporaries in the CTest working directory; ad-hoc scratch goes to `../.agent_scratch/phase4/`.
- The canonical build directory is `build_release/`. After any protoCore change, **delete it first** (`rm -rf build_release`, path checked): a stale binary against a new protoCore ABI crashes on the first `ProtoSpace`.
- Commits use the repository's configured git identity (never `-c user.*`, never `--author`) and end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`. Never force-push, never rewrite published history, never create the `v0.5.0` tag — the maintainer tags.
- All code, comments, messages and documentation in professional English. User-facing messages never mention internal phase names.

**Binding principles (DESIGN §1.1 P1–P6).**

- **P1 — values live in protoCore structures.** The in-flight exception value is the hard case in this phase: a `ProtoObject*` held only inside a C++ exception object is invisible to the collector. A0-2 settles where it lives; every frame the exception passes through re-roots it in that frame's `ProtoContext` before anything allocates.
- **P2 — one `ProtoContext` per invocation.** A `catch` body and a `finally` body run in the **same** frame and the same context as the `try` body — they are ordinary bytecode at a different `ip`, not a new invocation.
- **P3 — a missing capability extends protoCore as a new type.** Phase 4 needs **nothing new from protoCore**: exceptions are C++ unwinding, the handler table is protoScala's, and named arguments already have a kernel convention (A0-12). **If an implementer concludes a protoCore change is required, that is a maintainer decision** — write it into Task 0 as a new `A0-n` with options and a recommendation, and stop; never patch protoCore from this plan.
- **P4 — every deviation from Scala 3 is documented** with a stable id in `docs/STATUS.md`. **Phase 4 uses D71–D89.** That assumes Phase 3 has taken D54–D70; the highest id in use before either phase is **D53**, claimed by the by-name-parameters work that landed on 2026-09-23 (`ce172f9`, `ae6ab5b`, `bca0352`). **Re-check before writing a single id:** run `grep -oE 'D[0-9]+' docs/STATUS.md | tr -d D | sort -n | tail -1`; if the highest id in use is not D70, shift this whole block so it starts one past it, keeping the relative order, and record the shift in `docs/DECISIONS-LOG.md`. If Phase 3 has **not** landed, Phase 4 starts at D54 instead and Phase 3 shifts later.
- **P5 — purity over performance.** The retry loop adds one C++ `try` region per frame. Task 13 **measures** the cost and the phase is not done until the benchmark suite shows no regression beyond noise; any claimed figure is backed by `perf stat -r 3` cycles on a quiet host.
- **P6 — no mutable-heap GC premises.** No new stop-the-world work, no write barrier, no card marking. Exceptions add no blocking wait; where one would be added it would run inside `ProtoContext::UnmanagedScope`.

**`ProtoTuple` prohibition (DESIGN §4.6, R2).** **Never map transient data to `ProtoTuple`.** protoCore interns every `ProtoTuple` node and interned tuples are perennial (`core/ProtoTuple.cpp:214`, `core/ProtoSpace.cpp:449`). Scala tuples are **case classes** `Tuple2`..`Tuple22` (Phase 2). In this phase that rule applies to: an exception instance and its `getMessage`/`getCause` fields (an ordinary prototype-based object), the `finally` save slot (a local slot, not a structure), a handler table (plain C++ `std::vector` of PODs in the `BytecodeModule`, which holds no `ProtoObject*`), an `enum` case's `__ordinal__` (a `SmallInteger` attribute), `E.values` (a `ProtoList`), and every keyword-argument bundle (a `ProtoSparseList`, which is protoCore's own convention — A0-12 explains why that is correct and not a compromise).

**`PROTO_NONE`.** `PROTO_NONE` is Scala `null` *and* "attribute missing". Presence is probed with `hasOwnAttribute`/`hasAttribute`, or by the `nullptr` a raw accessor returns, never by comparing a lookup result with `PROTO_NONE` alone. `throw null` is therefore a real case and A0-5 decides it.

**Interned symbols.** Symbols are per `ProtoSpace`. **Never cache them in function-local `static`s.** Every new attribute key and method name goes into `RuntimeLayout` (filled in `Runtime::Runtime`) and is read from there on the hot paths. **Every keyword-argument key is derived from `proto::ProtoString::createSymbol`, never from `fromUTF8String`, `fromUTF8` or `fromStdString`** — A0-12 explains the silent failure that rule prevents.

**Tutorial (the Documentation track, every phase).** Phase 4 writes `docs/tutorial/11-exceptions.md` and `docs/tutorial/12-enums-and-sealed-hierarchies.md`, and extends `docs/tutorial/02-for-the-python-or-javascript-developer.md` (the Python/JavaScript bridge: `try`/`except`/`finally` and `try`/`catch`/`finally`, `raise`/`throw`, Python's `Enum` and TypeScript's union types, Python's keyword arguments and JavaScript's options objects) and `docs/tutorial/03-for-the-scala-developer.md` (the departures catalogue, keyed to the new D-ids). Chapter 6 (classes, objects and traits) gains a `super[T].m` section and an extension-methods section; chapter 5 (functions and closures) gains a named-and-default-arguments section. The tutorial is **dual-audience**: traditional Scala programmers get an honest catalogue of departures; developers coming from Python or JavaScript get Scala from first principles with a bridge back to what they know. **Every runnable snippet in a chapter is, verbatim, the body of a conformance fixture** `tests/conformance/tutorial/NN-<topic>-<name>.scala` whose `// EXPECT:` line is the output printed under the snippet, so the text cannot drift from the implementation.

**Conformance fixtures.** `tests/conformance/NN-topic/*.scala`; the **first line** is the directive and nothing else is parsed: `// EXPECT: <last non-empty stdout line>`, `// EXPECT-ERROR[: <substring of stderr or stdout>]`, `// XFAIL: …`, `// XFAIL-ERROR[: …]`. One CTest case per file; files whose name starts with `_` are helpers and are not run on their own. **Every fixture whose syntax differs between braces and indentation exists in both variants** (`-braces` / `-indent` suffixes) — for this phase that is nearly every fixture, because `try`/`catch`/`finally`, `enum` bodies and `extension` blocks all have both forms. **Every fixture that starts an actor carries `set_tests_properties(... PROPERTIES TIMEOUT 120)`**, as the Phase 5 block in `tests/CMakeLists.txt` already does for `13-actors/` and `14-futures/`; extend that block to the new directory. Where a behaviour should match scalac:

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase4/out ../.agent_scratch/phase4/probe.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase4/out" probe
```

Never `bin/scala`: it is a script runner whose top-level wrapping differs from a compiled `@main`, which has produced false divergences before.

**Benchmarks self-report and the runner verifies.** Every benchmark prints the work it computed on its last line and `benchmarks/run_benchmarks.py` compares it with the `// EXPECT:` directive of the same file **before** computing any rate; a wrong result, a non-zero exit or a timeout marks the cell FAILED and a FAILED cell never enters a median or a geometric mean. Exit code alone never counts as success (DESIGN §10; the protoPython sprint-9 lesson). Phase 4 adds no workload, but Task 13 re-runs the whole suite because the retry loop touches every frame.

**Build and test only with:**

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
cmake -B build_release -S . && cmake --build build_release
ctest --test-dir build_release --output-on-failure
```

Every target builds with `-Wall -Wextra -Wpedantic` and **no warnings**. The full suite must also pass under `PROTOCORE_HEAP_LIMIT_CELLS=20000` (unfiltered) and at `PROTOSCALA_ACTOR_WORKERS=1` and `=16`.

---

## Preflight (before Task 0)

- [ ] **Step 0: Reconcile with the work in flight on `main`**

A **by-name-parameters implementation landed on `main` on 2026-09-23** (commits `ce172f9`, `ae6ab5b`, `bca0352`), outside every phase's scope — ROADMAP records it under Phase 4 as "Landed early (2026-09-23), with the D47 ruling". It takes opcode **38** (`FORCE_THUNK`, `src/compiler/Opcodes.h:59`), adds `builtinByNameSignatures()` (`src/runtime/Primitives.h:41-45`) and `GlobalTable::byNameMaskOf` / `byNameSelectorMask` (`src/compiler/GlobalTable.h:41-100`), and claims **D53**: a by-name parameter is lazy only where the compiler resolves the call site to the declaration, and is evaluated once at the call otherwise. The same run **overturned D45** (an actor handler may now return a bare `newState`, `ce172f9`), which every actor fixture in Task 5 must be read against — a handler written `(s, out)` still works, and a handler written `out` now also works. Phase 3, if it has landed, holds D54–D70 and opcode 39 (`CONCAT`).

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
git log --oneline | head -5
git status --short
grep -nE 'FORCE_THUNK|CONCAT|byName' src/compiler/Opcodes.h src/runtime/Primitives.h | head
grep -oE 'D[0-9]+' docs/STATUS.md | tr -d D | sort -n | tail -1
build_release/protoscala --version
```

Record four things and carry them through the phase:

1. **The highest deviation id in use** — D53 before Phase 3, D70 after it. Shift Phase 4's D71–D89 block so it starts one past whatever `grep` reports.
2. **Whether the `96..127` band is still entirely free.** It should be; if anything has taken 96 or 97, take the lowest free numbers instead and say so in every task that names them.
3. **The lowest free opcode in the `80..95` object-model band** for `CALL_KW` (Task 11). 80 at the time of writing.
4. **That by-name parameters are still in the tree** (`grep -c 'FORCE_THUNK' src/compiler/Opcodes.h` returns 1). They are, as of `bca0352`, so Task 3 Step 6's fixtures are **required, not optional**: a by-name argument whose thunk throws throws at the *use* site, inside whatever `try` encloses it, which is Scala's behaviour — and D53's boundary, where an unresolvable call site evaluates the argument eagerly so the exception escapes the callee's `try`, needs its own fixture.

If `git status --short` is not clean, **stop and ask the maintainer** whether to branch from the committed state or wait for the merge.

- [ ] **Step 1: Rebuild from clean and confirm the baseline is green**

```bash
rm -rf build_release
cmake -B build_release -S . && cmake --build build_release 2>&1 | grep -Ei 'warning|error' | head
ctest --test-dir build_release 2>&1 | tail -5
ctest --test-dir build_release -N | tail -1
```

Expected: no warning, no error, `100% tests passed`, and a test count to quote in Task 14. If the tree is not clean, **stop**.

- [ ] **Step 2: Confirm the Phase 4 surface is still absent (the failing starting point)**

```bash
mkdir -p ../.agent_scratch/phase4
printf '@main def run(): Unit =\n  try println(1 / 0) catch case e: ArithmeticException => println("caught")\n' \
  > ../.agent_scratch/phase4/probe-try.scala
build_release/protoscala ../.agent_scratch/phase4/probe-try.scala; echo "rc=$?"
printf '@main def run(): Unit = throw new RuntimeException("x")\n' > ../.agent_scratch/phase4/probe-throw.scala
build_release/protoscala ../.agent_scratch/phase4/probe-throw.scala; echo "rc=$?"
printf 'enum Color:\n  case Red, Green\n@main def run(): Unit = println(Color.Red)\n' \
  > ../.agent_scratch/phase4/probe-enum.scala
build_release/protoscala ../.agent_scratch/phase4/probe-enum.scala; echo "rc=$?"
printf 'trait A:\n  def m: Int = 1\ntrait B extends A:\n  override def m: Int = 2\nclass C extends B:\n  override def m: Int = super[A].m\n@main def run(): Unit = println(new C().m)\n' \
  > ../.agent_scratch/phase4/probe-super.scala
build_release/protoscala ../.agent_scratch/phase4/probe-super.scala; echo "rc=$?"
printf 'def f(a: Int, b: Int = 2): Int = a + b\n@main def run(): Unit = println(f(b = 10, a = 1))\n' \
  > ../.agent_scratch/phase4/probe-named.scala
build_release/protoscala ../.agent_scratch/phase4/probe-named.scala; echo "rc=$?"
printf 'extension (n: Int) def double: Int = n * 2\n@main def run(): Unit = println(3.double)\n' \
  > ../.agent_scratch/phase4/probe-ext.scala
build_release/protoscala ../.agent_scratch/phase4/probe-ext.scala; echo "rc=$?"
printf 'object O:\n  class C(val n: Int)\n@main def run(): Unit = println(new O.C(1).n)\n' \
  > ../.agent_scratch/phase4/probe-nested.scala
build_release/protoscala ../.agent_scratch/phase4/probe-nested.scala; echo "rc=$?"
```

Expected messages, each rc=1: `try is not implemented yet` (`src/frontend/Parser.cpp:253`), `throw is not implemented yet` (`:252`), `'enum' definitions are not implemented yet` (`:894`), `super[T] (a qualified super call) is not implemented yet` (`:567`), `named arguments are not implemented yet` (`src/compiler/Compiler.cpp:422`) or the `SEND_KW` runtime message `named arguments are not supported yet for methods written in Scala` (`src/runtime/ExecutionEngine.cpp:386`), `'extension' definitions are not implemented yet` (`:898`), `classes, traits and objects must be defined at the top level of a file` (`src/compiler/Compiler.cpp:457` and `src/compiler/CompileTemplates.cpp:330`). **Paste the exact messages into the task notes** — Task 14 removes each one and the diff is the proof.

- [ ] **Step 3: Confirm `Layout` already handles the exception keywords**

```bash
grep -n 'KwTry\|KwCatch\|KwFinally\|KwThrow' src/frontend/Layout.cpp
```

Expected: `opensRegion` lists `KwCatch`, `KwFinally`, `KwThrow` and `KwTry` (`Layout.cpp:65-77`); the pending-partner rules pair `KwCatch` with opener `KwTry` and `KwFinally` with `KwTry` or `KwCatch` (`:104-105`); `endMarkerName` answers `"catch"`/`"finally"` (`:222-223`). **The offside rule for `try`/`catch`/`finally` is already implemented and unit-tested**; Phase 4 adds no layout work and Task 2 Step 1's indentation fixtures should pass the layout stage on the first build. If any of those lines is missing, the plan's Task 2 gains a layout step.

- [ ] **Step 4: Record the benchmark baseline before the retry loop exists**

```bash
uptime
python3 benchmarks/run_benchmarks.py --name phase4-baseline --runs 5 --warmup 2
python3 benchmarks/run_actor_benchmarks.py --name phase4-actors-baseline 2>/dev/null || \
  bash benchmarks/actor-bench.sh
```

Expected: reports in `benchmarks/reports/` with no FAILED cell. Task 13 compares against these. Record the load average with them; above 2.0, say the figures are a reference only and re-measure on a quiet host before Task 13 claims anything.

- [ ] **Step 5: Confirm the scalac reference works**

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
mkdir -p ../.agent_scratch/phase4/out
printf '@main def probe(): Unit =\n  try\n    throw new IllegalStateException("boom")\n  catch\n    case e: IllegalStateException => println("caught " + e.getMessage)\n  finally\n    println("cleanup")\n' \
  > ../.agent_scratch/phase4/probe.scala
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase4/out ../.agent_scratch/phase4/probe.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase4/out" probe
```

Expected: `caught boom` then `cleanup`. If `scalac` fails, every "verify against scalac" step becomes "record that the reference was unavailable and why" — it never fabricates the expected output.

---

## Task 0: Maintainer decisions

Each decision below is **decided by the agent, pending review** under the maintainer's standing authorisation, **except A0-12, which the maintainer has already settled** and which is recorded here as settled, not as a question. Task 14 records every one in `docs/DECISIONS-LOG.md` with the right marker. Where a decision creates a user-visible departure from Scala 3, Task 14 records it as a `D<n>` in `docs/STATUS.md` and `docs/LANGUAGE.md` §5. **Phase 4 uses D71–D89**, subject to the Preflight Step 0 re-check.

### A0-1 — How much of protoST's exception design to follow

protoST's spec (`protoST/docs/archive/design-specs/2026-05-20-exceptions.md`) splits finding a handler from unwinding: `signal` walks a **runtime-maintained handler stack** with the signalling stack intact, runs the handler in place, and only *then* throws a C++ `UnwindToHandler` if the handler decided to unwind. It does that because **Smalltalk exceptions are resumable** (`resume:` must return to the `signal` point) and a C++ throw destroys the stack between throw and catch.

DESIGN §7 already draws the conclusion: "Scala has no resumable exceptions, so protoST's separate handler stack is not needed."

| Option | Consequence |
|---|---|
| **(a) C++ unwinding only, with a per-module handler table** (recommended, and what DESIGN §7 prescribes) | No handler stack to maintain, push, pop or snapshot. A `try` costs nothing until something throws. **Nothing has to survive a cooperative suspension**, because there is no auxiliary stack to snapshot — the only state is the `ip`, which the frame snapshot already carries |
| (b) protoST's handler stack | Buys `resume:`, which Scala does not have, and costs a push/pop per `try`, a snapshot/restore per suspension (protoST's spec explicitly lists "a handler stack entry must survive an actor's yield/resume — snapshot/restore it alongside `frames_`"), and a new thread-local |

**Decision: (a).** Where protoScala **does** follow protoST is its mandatory §6: **every boundary that calls out to native or foreign code must let the control signals through and translate everything else**, in exactly this order:

```cpp
try { /* native or foreign call */ }
catch (FutureYield&)          { throw; }   // a cooperative suspension, not an error
catch (ScalaThrow&)           { throw; }   // already a Scala exception value
catch (ScalaError&)           { throw; }   // already a Scala error, translated lazily
catch (const std::exception& e) { throw ScalaError("RuntimeException", e.what()); }
catch (...)                   { throw ScalaError("RuntimeException", "native exception"); }
```

and where it **departs** is that there is no handler stack, no `resume:`, no `retry`, no `pass` and no `ifCurtailed:`; `finally` is Scala's `ensure:` and is the only cleanup form.

### A0-2 — Where the in-flight exception value lives while the stack unwinds

DESIGN §7 says the payload "is rooted in a slot of the throwing frame's context and re-rooted in the catching frame". The throwing frame's `ProtoContext` is a C++ stack object that unwinding destroys, so the wording needs an operational reading.

| Option | Consequence |
|---|---|
| **(c) Every frame re-roots the payload in its own context as the exception passes through** (recommended; DESIGN §7's own wording) | Free: `runLoop` already has a per-frame catch block, and `ProtoContext::returnValue` is a traced slot that is unused on the exception path (`execute` only assigns it after `runLoop` returns). The only window in which the payload is unrooted is between one frame's context being destroyed and the next frame's catch running — and **nothing in that window allocates**: C++ unwinding runs destructors, and `ProtoContext::~ProtoContext` does not allocate |
| (a) A per-thread pin stack anchored in a root slot | One attribute write per throw and one per catch, plus a registry keyed by thread. Correct, but it pays on every throw for a window option (c) closes for free |
| (b) Keep the value only inside the C++ exception object | P1-violating: a `finally` body allocates, and a collection there can reclaim the exception |

**Decision: (c).** The carrier is:

```cpp
// ScalaThrow — an in-flight Scala exception value.
//
// It deliberately derives from std::exception and NOT from std::runtime_error:
// ExecutionEngine::runLoop catches `const std::runtime_error&` and turns it into
// a RuntimeException (the protoCore-error bridge), which would swallow every
// Scala throw if ScalaThrow were a runtime_error. Task 1 pins that with a test.
//
// `value` is re-rooted by every frame the exception passes through (plan A0-2):
// the frame writes it into its own ProtoContext::returnValue, which is a traced
// slot the exception path never otherwise uses.
class ScalaThrow : public std::exception {
public:
    explicit ScalaThrow(const proto::ProtoObject* v) : value(v) {}
    const char* what() const noexcept override { return "protoScala exception"; }
    const proto::ProtoObject* value;
};
```

### A0-3 — Where the handler search happens, and why it must not be inside the C++ catch block

The obvious implementation puts the handler search inside `runLoop`'s existing `catch (ScalaError&)` and jumps to the handler pc there. That is wrong in a way that is invisible until Phase 5's `await` meets it: the `catch` and `finally` bodies would then run **inside a live C++ catch handler**, so a `FutureYield` thrown from them would propagate out of a handler with an exception still in flight, and the snapshot's `ip` would point into a handler body that a resume would re-enter with no exception to re-raise.

| Option | Consequence |
|---|---|
| **(a) A retry loop outside the catch: `for (;;) { try { return runLoop(...); } catch (ScalaThrow&) { … ; continue; } }`** (recommended) | The `continue` leaves the catch block, so the Scala handler body runs with no live C++ handler, an ordinary `ip`, an ordinary operand stack and therefore **exactly the same `await` behaviour as any other bytecode**. `runLoop` is unchanged except for one out-parameter |
| (b) Search and jump inside the catch | Breaks cooperative `await` inside a `catch` or `finally` body, silently — the failure mode is a resumed frame running a handler body with nothing to handle |
| (c) Recurse: call `runLoop` again from inside the catch for the handler body | Grows the native stack by one frame per caught exception, so a loop that catches per iteration overflows the stack guard; and the handler body's frame would be a *different* `ProtoContext` from the `try` body's, breaking P2 and breaking access to the `try`'s locals |

**Decision: (a).** `runLoop` gains one parameter so the retry loop knows where the fault was:

```cpp
    // The dispatch loop of one frame. On an exception it writes the word index of
    // the faulting instruction to *faultPc and rethrows; runFrame uses it to
    // search the module's handler table (plan A0-3).
    const proto::ProtoObject* runLoop(proto::ProtoContext& frame, const BytecodeModule& mod,
                                      const proto::ProtoObject** slots,
                                      const proto::ProtoObject** sp, const Instr* ip,
                                      std::size_t* faultPc);
```

The faulting index is `(ip - code) - 1`, which is exactly the index `runLoop`'s existing `catch (ScalaError&)` already uses for `mod.lineAt(...)` (`ExecutionEngine.cpp:929`), so the two agree by construction and an `EXTEND` prefix at `ip - 2` lies inside the same handler range anyway.

### A0-4 — How `finally` is implemented

| Option | Consequence |
|---|---|
| **(a) A `Finally` handler-table entry plus an inline copy on every normal exit** (recommended) | One mechanism for the exceptional path (the entry fires, the body runs, `RETHROW` re-raises) and straight-line code for the normal path (no table lookup when nothing throws). `return` inside a `try … finally` emits the enclosing finally bodies innermost-first before the `RETURN`, which the compiler can do because it already tracks the enclosing construct stack |
| (b) A C++ RAII guard per `try` | The cleanup would run *inside* unwinding, i.e. inside a destructor, where a Scala exception thrown by the `finally` body would cross a `noexcept` boundary and terminate the process |
| (c) Duplicate the finally body at every exit, exceptional path included | Needs a `catch`-all handler entry anyway, so it is (a) with more code |

**Decision: (a).** For `try B catch C finally F` the compiler emits **two** entries, in this order: `{range = B, kind = Catch, handlerPc = C}` then `{range = B ∪ C, kind = Finally, handlerPc = F}`. `handlerFor(pc)` returns the **first** entry in table order whose `[startPc, endPc)` contains `pc`, and the compiler emits nested `try`s before enclosing ones, so the search order is innermost-Catch, innermost-Finally, outer-Catch, outer-Finally — Scala's order. Because a handler body's pc is *outside* its own `try` range, a handler can never catch its own exception, and a `RETHROW` emitted at the end of a non-matching `catch` cascade lands inside the `Finally` entry's range, so the `finally` still runs. Recorded as **D71**: a `finally` body that itself throws replaces the in-flight exception (Scala does the same and warns about it; protoScala cannot warn — D4).

### A0-5 — What `throw` accepts

| Option | Consequence |
|---|---|
| **(a) Only an instance of the prelude `Throwable` chain** (recommended) | Matches Scala; `case e: Exception` and `e.getMessage` always mean something; a `throw 5` is a loud `IllegalArgumentException` at the throw site rather than a value nothing can usefully catch |
| (b) Any value | A dynamic dialect could allow it, but then `catch { case e: Throwable => e.getMessage }` breaks on a thrown `Int`, and `MatchError`'s own message has to cope |

**Decision: (a)**: `throw v` requires `v` to answer the `@Throwable` marker key; anything else raises `IllegalArgumentException: throw expects a Throwable, got <T>` at the `THROW` instruction. `throw null` raises `NullPointerException`, as Scala does. This is Scala's rule, so no deviation id is needed.

### A0-6 — Native error translation: what maps to what

"Native error translation" means a protoCore-level (or C++-level) failure surfacing as a **catchable** Scala exception. The full table, decided here:

| Source | Becomes | Catchable by | Where |
|---|---|---|---|
| `ScalaError(cls, msg)` from any of the 53 native throw sites | an instance of the prelude class named `cls` | `case e: <cls>`, and every ancestor of it | `runFrame`, lazily (A0-7) |
| `ScalaError` whose `cls` names no prelude class | `RuntimeException(cls + ": " + msg)` | `case e: RuntimeException` | `materialiseError` |
| protoCore `std::runtime_error` (e.g. "Objects are not integer types for division.") | `RuntimeException(what())` | `case e: RuntimeException` | `runLoop`'s existing bridge, `ExecutionEngine.cpp:931-936` |
| protoCore `std::invalid_argument` (e.g. `asIntegerString` with a bad base) | `IllegalArgumentException(what())` | `case e: IllegalArgumentException` | new branch in `runLoop`, **before** the `runtime_error` branch |
| protoCore `std::out_of_range` | `IndexOutOfBoundsException(what())` | `case e: IndexOutOfBoundsException` | same |
| protoCore `std::overflow_error` (`asLong` beyond 64 bits) | `ArithmeticException(what())` | `case e: ArithmeticException` | same |
| `std::bad_alloc`, and protoCore's `PROTOCORE_HEAP_LIMIT_CELLS` failure | `OutOfMemoryError(what())` — an `Error`, not an `Exception` | `case e: OutOfMemoryError`, `case e: Error`, `case e: Throwable`; **not** `case e: Exception` | same |
| `StackGuard` depth exceeded (`checkNativeStack`) | `StackOverflowError` — an `Error` | as above | unchanged site, now a catchable `Error` |
| `std::length_error` from bytecode limits | unchanged: a **compile-time** failure reported by `Session` | not catchable | `Session.cpp:83`, `:214` |
| `std::logic_error` (a compiler or VM bug) | **not translated.** It reaches `main.cpp:87` and prints `protoscala: internal error: …` | **not catchable** | deliberate |
| `FutureYield` | **never translated, never catchable.** It is not a `std::exception`, and `runLoop`'s `catch (FutureYield&)` is first | — | A0-7 |
| a UMD provider's or foreign method's C++ exception | the A0-1 boundary shape, i.e. `RuntimeException` | `case e: RuntimeException` | **Phase 6**, where UMD lands; Task 12 Step 5 states it and adds no code |

Recorded as **D72**: protoScala's exception class names are unqualified (`ArithmeticException`, never `java.lang.ArithmeticException`) and the hierarchy is the small one of Task 4, not the JVM's — `catch { case e: java.io.IOException => }` does not compile, because there is no `java` namespace (D8). Recorded as **D73**: `std::logic_error` from a VM or compiler defect is deliberately **not** catchable, so a bug in protoScala can never be masked by a user's `catch { case e: Throwable => }`.

### A0-7 — How unwinding interacts with an actor turn and with a suspended `await`

This is the decision that silently breaks Phase 5 if it is wrong, so it is stated as six invariants, each with a named test.

1. **`FutureYield` is never a Scala exception.** It does not derive from `std::exception` (DESIGN §7), `runLoop`'s `catch (FutureYield&)` stays the **first** catch clause, and `runFrame` catches only `ScalaThrow` and `ScalaError`. A `FutureYield` therefore passes straight through `runFrame` untouched. *Test:* `Engine.AFutureYieldIsNotCaughtByAScalaCatch` (Task 5) and the fixture `await-inside-try.scala`.
2. **No `finally` runs on a suspension.** A suspended frame is going to be *resumed*, not abandoned, so its `finally` has not been reached yet and must not run. This falls out of invariant 1 — `runFrame` never sees the `FutureYield` — and it is the whole reason A0-3 puts the handler search outside the catch rather than in it. *Test:* the fixture `finally-does-not-run-on-suspension.scala`, which awaits inside a `try … finally` and prints the order of events.
3. **`resumeFrames` must call `runFrame`, not `runLoop`.** Today it calls `runLoop` directly (`ExecutionEngine.cpp:548`). Left unchanged, a resumed frame would have **no handler table active**, so `try { f.await } catch { … }` would silently fail to catch anything raised after the resume. Task 5 Step 2 changes that one line and Task 5's fixtures are what prove it.
4. **The bound exception variable survives a suspension for free.** `appendSuspendedFrame` saves `slots[0, pendingBase)` (`ExecutionEngine.cpp:508-512`), and `pendingBase` is an operand-stack index, which is always `≥ arity + localCount`. Every local slot — including a `catch` clause's bound `e` and the hidden `finally` save slot — is therefore inside the saved prefix. *Test:* the fixture `await-inside-catch.scala`, which catches, awaits inside the handler, and then reads `e.getMessage`.
5. **A failed future resumes by raising, not by returning.** `await` on a failed future must raise the carried exception *at the `await` call site*, so an enclosing `try` in the suspended handler catches it. Mechanism: `Futures::complete` with a failure re-enqueues the suspended actor with a raise flag; the resume path calls `runFrameRaising` on the innermost frame, which performs the handler search at the pc of the in-flight call (`ipOffset - 1`) and then falls into the ordinary retry loop. This **retires D50**. *Test:* the fixture `await-a-failed-future-inside-try.scala`.
6. **A handler exception still leaves the actor alive.** `ActorScheduler::deliver`'s catch chain becomes, in this order: `catch (FutureYield&) { throw; }` (Phase 5's suspension path, unchanged), `catch (ScalaThrow& t)` → complete the ask's future with `Failure(t.value)`, `catch (ScalaError& e)` → materialise and complete with `Failure(that)`. In both failure cases the actor keeps its previous state and the error is reported on stderr, exactly as Phase 5 specifies (DESIGN §8.4). `Thread.start`'s thread body gets the same chain, so a throwing thread reports and joins instead of terminating the process. *Test:* Phase 5's `failed-ask.scala` (now carrying a real exception) plus the new `thread-body-throws.scala`.

Recorded as **D74**: an actor suspended on a future that never completes never runs the `finally` of the `try` it was suspended inside; the shutdown diagnostic reports how many actors are parked, and that is the only notice. Scala has no equivalent situation, and making it otherwise would mean running arbitrary user code during shutdown.

### A0-8 — `super[T].m` semantics

Plain `super.m` shipped in Phase 2: `SEND_SUPER` finds the defining class in the receiver's linearization by pointer identity and probes each **subsequent** entry (`ExecutionEngine::superSend`, `:347-371`). `super[T].m` names the ancestor explicitly instead.

| Option | Consequence |
|---|---|
| **(a) Probe `T` itself first, then continue after `T`** (recommended) | `super[A].m` finds `A`'s own `m` when `A` defines it, and an `m` that `A` inherits when it does not — which is what "the member `m` as seen from `A`" means in Scala. One extra probe, no new mechanism |
| (b) Start strictly after `T` | Wrong: `super[A].m` would skip `A`'s own definition, which is precisely the one the programmer named |

**Decision: (a)**, confirmed by the maintainer (2026-09-23). Scala additionally requires `T` to be a **direct** parent of the class the `super[T]` appears in. protoScala keeps no direct-parent list (`ClassInfo` stores the flattened `linearization`, `ClassInfo.h:55`), so it checks only that `T` is in the receiver's linearization. Recorded as **D75**: `super[T].m` accepts any ancestor in the linearization, where scalac 3.9 requires a direct parent — `class C extends B` with `trait A` above `B` may write `super[A].m` here and may not in Scala. `T` not in the linearization raises `NoSuchMethodError: A is not in the linearization of C, so super[A].m has no meaning here`, reusing the Phase 2 message shape.

### A0-9 — How `enum` is lowered

Scala 3's `enum` is defined by the reference as sugar over a sealed class with a companion; protoScala can lower it entirely in the frontend.

| Option | Consequence |
|---|---|
| **(a) Desugar in `Desugar.cpp` to a `sealed abstract class`, one `case object` or `case class` per case, and a companion `object` carrying `values`, `valueOf` and `fromOrdinal`** (recommended) | No new compiler concept, no new opcode, no runtime prototype. Pattern matching, `toString`, `equals`, `hashCode`, `unapply` and exhaustive-looking `match` all come from Phase 2's case-class machinery for free |
| (b) A runtime `Enum` prototype with native `values`/`ordinal` | A second object model for something the first one already expresses |

**Decision: (a).** `enum Color { case Red, Green, Blue }` becomes:

```scala
sealed abstract class Color extends Enum
case object Red extends Color
case object Green extends Color
case object Blue extends Color
object Color:
  def values: List[Color] = Red :: Green :: Blue :: Nil
  def valueOf(name: String): Color = /* generated cascade, else IllegalArgumentException */
  def fromOrdinal(n: Int): Color = /* generated cascade, else NoSuchElementException */
```

with each case additionally carrying `__ordinal__` and `__enumname__` attributes so `ordinal` and `productPrefix` work, and with `Enum` a builtin trait (`@Enum` in `builtinTypes()`) carrying native `ordinal` and `toString`. A parameterised case (`case Red extends Color(0xFF0000)`) becomes a `case object Red extends Color(0xFF0000)`; a case with its own parameters (`case Node(l: Tree, r: Tree)`) becomes a `case class`. Deviations, recorded together: **D76** — `values` returns a `List`, where Scala returns an `Array`; **D77** — an `enum` case is a top-level definition in the module's namespace *and* a member of the companion, so both `Color.Red` and `Red` resolve, where Scala requires `Color.Red` unless imported; **D78** — `derives`, `enum` type parameters with bounds and `enum` cases that override members of the enum class are parsed and ignored or rejected with a clear message rather than implemented.

### A0-10 — Templates nested in an `object`

ROADMAP's Phase 4 done-when includes "templates nested in an `object`"; Phase 2 rejects every nested template with "classes, traits and objects must be defined at the top level of a file" (`Compiler.cpp:457`, `CompileTemplates.cpp:330`).

**Decision:** a template nested in an `object` is **lifted to the top level** by `Desugar` with a qualified name, and the enclosing object's namespace records the mapping. `object O { class C }` gets type key `@O.C` and display name `O.C`; a nested `object` gets the term key `O.C` in the globals. `O.C(args)` and `new O.C(args)` resolve through the compiler's type namespace with no runtime link, exactly as Phase 2's companion linking does (DESIGN §4.2, Q3). Nesting is arbitrarily deep (`@O.P.C`). What stays out of scope, recorded as **D79**: a `class`, `trait` or `object` nested in a **`class`** or **`trait`** (not an `object`), a local class inside a block, and an anonymous class `new T { … }` are still rejected — a template nested in a `class` would capture the enclosing instance, which needs a per-instance class, and that is a different feature.

### A0-11 — Named and default arguments for Scala-defined methods

DESIGN §5.2 already fixes the mechanism: "Named arguments are passed in the keyword `ProtoSparseList` keyed by interned parameter names. Default values are compiled into the callee's prologue." Phase 2 built half of it: `SEND_KW` (opcode 77) builds that list and passes it to a **native** method, and refuses anything else (`ExecutionEngine.cpp:385-388`). The decisions left are the details.

**Decision, confirmed by the maintainer (2026-09-23), with the maintainer's rationale recorded because it is a platform principle and not a concession:**

> *"la plataforma es lazy binding, aunque Scala no lo sea"* — the platform is late-binding even when the source language is not.

A runtime built on protoCore may resolve at **run time** what its source language resolves at compile time; that follows directly from a prototype-based, dynamically dispatched, type-erased kernel, and it is why binding named arguments in the callee is the *right* design here rather than a fallback forced by D4. No future reader should mistake it for a limitation to be fixed by adding a static resolution path. **The behavioural bar is unchanged and is what late binding does not excuse:** the error must be loud and must name both what was expected and what arrived. Late binding excuses late *detection*; it never excuses silent failure — which is exactly the standard A0-12's `createSymbol` trap is held to as well.

- **Where the binding happens: in the callee, not the caller.** `BytecodeModule` gains `paramNames` (interned to `paramNameSymbols` by `linkSymbols`) and one optional default-block index per parameter. `execute`'s prologue fills positional parameters, then each keyword argument into its parameter's slot by name, then each still-unfilled parameter from its default block. A caller that does not know the callee statically therefore still works — the platform resolves it when the call happens.
- **Evaluation order.** Arguments are evaluated at the call site in **source order**, Scala's rule, which is what the compiler already emits. Defaults are evaluated in the callee prologue in **parameter order**, and a default may read parameters declared before it (`def f(a: Int, b: Int = a + 1)`), which Scala also allows.
- **Errors.** An unknown keyword name raises `IllegalArgumentException: <m> has no parameter named 'x'`. A keyword that duplicates a positional argument raises `IllegalArgumentException: <m> received parameter 'x' twice`. A parameter left unfilled with no default raises `IllegalArgumentException: <m> is missing argument 'x'`. All three are **loud**, and each names what was expected and what arrived — the bar the maintainer set alongside the late-binding ruling. Silently dropping a named argument is the failure mode A0-12's trap warns about, and it must not be reachable from this side either.
- **`copy`, case-class `apply` and `unapply`** already accept named arguments as natives (Phase 2, Q9) and keep working unchanged.
- **One new opcode.** `SEND_KW` covers `recv.m(x = 1)`. A bare `f(x = 1)`, where `f` is a global `def` or a local function value, compiles to `PUSH_GLOBAL f; args; CALL n` today and needs a keyword form: `CALL_KW`, in the free `80..95` object-model band (Preflight Step 0 records the exact number).

Recorded as **D80**: because binding happens in the callee, a named argument to a value whose callee cannot be resolved still works, but a **typo in a parameter name is a run-time error, not a compile error** — scalac rejects it at compile time.

### A0-12 — Named arguments across the UMD boundary — **settled by the maintainer**

**This is not an open question and there is nothing to negotiate between runtimes.** Keyword arguments are a first-class part of protoCore's kernel calling convention, so each runtime translates *its own* surface syntax into that convention and the boundary needs no protocol of its own.

**The authority.** Every method in protoCore has this signature (`protoCore/headers/protoCore.h`):

```cpp
typedef const ProtoObject*(*ProtoMethod)(
    ProtoContext* context,
    const ProtoObject* self,
    const ParentLink* parentLink,
    const ProtoList* positionalParameters,
    const ProtoSparseList* keywordParameters
);
```

and `ProtoContext`'s constructor binds `args` plus `kwargs` against a `parameterNames` list. A Python callee reached through UMD receives keyword arguments as `keywordParameters` because that is how **every** protoCore method receives them. JavaScript's lack of keyword arguments, Clojure's trailing map and Smalltalk's keyword selectors being part of the selector itself are each that runtime's own translation problem, on its own side of the boundary — not protoScala's, and not a cross-runtime protocol question.

**Decision:** protoScala compiles `f(x = 1)` into protoCore's `keywordParameters` on the call, for Scala-defined methods, native methods and foreign callables alike, **with no special case for the foreign path**. Phase 2's `SEND_KW` is the mechanism to **extend**, not to replace; Task 11 extends it to Scala callees and Task 12 proves it reaches a non-Scala callee.

**The key convention: the `keywordParameters` key is the address of the interned parameter-name symbol.** `ProtoSparseList` is keyed by `unsigned long`, and that value is `reinterpret_cast<unsigned long>` of the interned `ProtoString` symbol for the parameter's name. Interning guarantees exactly one address per name in a `ProtoSpace`, so the address **is** the name's identity; and because UMD is in-process, the same address is valid in every runtime, which is the unambiguity the convention buys. The call site already in the tree:

```cpp
// src/runtime/ExecutionEngine.cpp:389-392 (ExecutionEngine::sendKeywords)
    const proto::ProtoSparseList* keywords = scope.newSparseList();
    for (std::size_t k = 0; k < site.nameSymbols.size(); ++k)
        keywords = keywords->setAt(&scope, reinterpret_cast<unsigned long>(site.nameSymbols[k]),
                                   base[1 + site.argc + k]);
```

and the interning that makes it sound:

```cpp
// src/compiler/BytecodeModule.cpp:230,241 (BytecodeModule::linkSymbols)
    auto intern = [ctx](const std::string& s) { return proto::ProtoString::createSymbol(ctx, s.c_str()); };
    // (line 230; the loop below is line 241, in the per-constant switch)
    for (const auto& n : c.names) c.nameSymbols.push_back(intern(n));
```

**The trap, pinned here because it has already caused real bugs elsewhere in the family.** The symbol must be obtained with `proto::ProtoString::createSymbol`, which **interns**. `ProtoString::fromUTF8String`, `fromUTF8` and `fromStdString` return a **different pointer for the same text** and do not intern, so a key built from one of them matches nothing — and the failure is **silent**: the keyword argument simply never binds, exactly as if it had not been passed. protoJS hit this class of bug (interned-versus-uninterned symbol identity) more than once. Therefore:

- every keyword-argument key in this phase is derived from `createSymbol`, never from any non-interning constructor;
- Task 11 Step 6 gives it a fixture whose failure mode is **visible** — a named argument that must bind and whose value is printed, not merely a call that does not crash;
- Task 11 Step 6 also gives the negative case a fixture: a named argument whose name matches no parameter must produce a clear error, never be dropped;
- Task 12 Step 4 adds a unit test that builds a key with `fromUTF8String` and asserts it does **not** bind, so the trap is pinned by a test and not only by a comment.

**Why an integer-keyed `ProtoSparseList` is the correct structure, not a concession.** Record this rationale in the plan and in `docs/DESIGN.md` §5.2 (Task 14), so a future reader does not mistake the integer key for a GC hazard and propose migrating keyword dictionaries to `ProtoMap`:

> `ProtoMap` was added to protoCore because `Map`/`Set` need arbitrary **collectable** object keys that the collector must trace; that is a different problem. Keyword-argument keys are **interned symbols, which are perennial by construction** — never collected, never moved — so there is nothing for the collector to trace and an integer-keyed sparse list is exactly right. It is the same principle that makes `ProtoTuple` interned and perennial, and the same reason transient data must never be mapped to `ProtoTuple`.

**Scope, stated plainly.** **UMD is not implemented** (ROADMAP Phase 6; `README.md` lists it as pending). Phase 4 therefore delivers:

- the **Scala-side mechanism** — `SEND_KW` extended to Scala callees, `CALL_KW`, the callee-prologue binding, the three loud errors (Task 11);
- fixtures that exercise the **whole protoScala half against a non-Scala callee**, using a runtime-provided stand-in object whose native method reports the keyword arguments it received (Task 12). That stand-in is reached through exactly the same `SEND_KW` path a foreign object will be, so it proves the mechanism without UMD;
- **two deferred fixtures** for the real foreign call (`import py.numpy as np; np.array(xs, dtype = "float64")`), written now as `// XFAIL: requires UMD (Phase 6)` with the expected output recorded, so Phase 6 converts them rather than inventing them.

Nothing in Phase 4 claims the foreign half works. Task 14 records that split in `docs/STATUS.md` in those words.

### A0-13 — Extension methods

`extension (x: T) def m(...)` with D6 already deciding the dispatch ("extension methods dispatch on the runtime prototype, not the static type").

**Decision:** the definition installs `m` as an attribute on `T`'s prototype when the enclosing unit runs. `T` must name a class, trait or builtin type the compiler knows (`GlobalTable::findType`), or the definition is rejected at compile time with `extension: <T> is not a protoScala type`. `extension [A](xs: List[A]) def …` uses the erasure (`List`). A collective extension (`extension (x: T) { def a = …; def b = … }`) installs each member. An extension whose name collides with a member `T` already has is rejected at compile time with `extension: <T> already has a member named <m>`, because silently shadowing a builtin method would be unrecoverable. Deviations: **D81** — an extension is **global and session-wide**: it is visible to every piece of code that runs after its definition, with no import scoping, where Scala's extensions are scoped like any other member; **D82** — an extension on a builtin type (`Int`, `String`, `List`) mutates that prototype for the whole session, so two units cannot define conflicting extensions of the same name on the same type.

Defining `extension (sc: StringContext) def json(args: Any*): String` is what **closes Phase 3's custom-interpolator restriction** (Phase 3's A0-3 deviation). Task 14 Step 3 records the closure, and Task 12 Step 3 carries the fixture — **only if Phase 3 has landed**; if it has not, that step is skipped and the note is not written.

### A0-14 — Multiple constructor parameter lists

`docs/LANGUAGE.md` §3 lists "Multiple constructor parameter lists (Phase 4)"; **ROADMAP's Phase 4 done-when does not mention them**, and `docs/STATUS.md`'s "Not yet implemented" lists them with no phase at all. The three documents disagree.

**Decision, ruled 2026-09-23: deliver them** — the contradiction is resolved in favour of `docs/LANGUAGE.md`. `Parser::parseClassParamClause` loops instead of running once, and the lists **concatenate** into one positional parameter list. Recorded as **D83**: `class C(a: Int)(b: Int)` has one flat parameter list, so `new C(1)(2)` and `new C(1, 2)` are the same call and `C.curried`-style partial application of a constructor does not exist.

**The other two documents must be brought into agreement, so the three stop disagreeing** (Task 14 Steps 2 and 4):
- `docs/ROADMAP.md`'s Phase 4 **done-when** gains `multiple constructor parameter lists` to its list, between `named and default arguments for Scala-defined methods` and `extension methods`.
- `docs/STATUS.md`'s "Not yet implemented" line `Multiple constructor parameter lists.` moves into "Implemented" with D83 attached.
Do both in the same commit as the feature, not as a later tidy-up.

### A0-15 — Opcodes added in this phase

**Decision:** three.

| # | Name | Band | Why |
|---|---|---|---|
| 96 | `THROW` | `96..127`, which DESIGN §3.5 reserves for exceptions | raises the value on the top of the stack |
| 97 | `RETHROW` | same | re-raises the value a `Finally` handler saved in a local slot |
| 80 | `CALL_KW` | `80..95`, the object-model band `SEND_KW` (77) already lives in | `f(x = 1)` on a function value, which `SEND_KW` cannot express |

Nothing else needs one: `try`/`catch`/`finally` are a handler table plus ordinary jumps, `enum` is frontend sugar, `super[T].m` reuses `SEND_SUPER` with one extra field on its `SuperSite` constant, extension methods are ordinary `setAttribute` calls, and a nested template is a lifted top-level one. The `128..159` (actors) band stays reserved and untouched, and `perf stat -r 3` is the only argument that would ever justify a fourth (DESIGN §1).

### A0-16 — Which existing deviations this phase retires

**Decision**, recorded so Task 14 does not leave stale entries behind:

| Id | Today | After Phase 4 |
|---|---|---|
| D44 | "Until Phase 4 there are no exception values: a failed `Future` carries `RuntimeError(className, message)`" | **Retired.** `Failure` carries a `Throwable`; `RuntimeError` is removed from the prelude and `__mkRuntimeError` with it |
| D50 | "Awaiting a future that fails, inside an actor, abandons the rest of the handler" | **Retired** by A0-7 invariant 5: the failure is raised at the `await` site and an enclosing `try` catches it |
| D52 | "`Priority.High`/`Medium`/`Low` are the integers `0`/`1`/`2` on an object, not an `enum`" | **Retired.** `Priority` becomes a real `enum` whose ordinals are 0/1/2, so the scheduler's integer band index is unchanged |
| Phase 3's custom-interpolator restriction | "only `s`, `f` and `raw` are available" | **Retired** by A0-13, if Phase 3 has landed |
| D43 | "`await` suspends only a chain of protoScala frames each stopped at a call instruction" | **Unchanged.** A `catch` or `finally` body is ordinary bytecode, so it suspends like any other; a native higher-order method in the chain is still refused |
| D33 | "`Option.getOrElse` evaluates its default eagerly" | **Not this phase's business.** The by-name work of Preflight Step 0 retires it |

---

## Design notes that apply to several tasks

File:line references are to the trees of 2026-09-23.

1. **The frame entry point today.** `execute` (`ExecutionEngine.cpp:449-488`) builds one `ProtoContext`, sizes its automatic locals to `arity + localCount + maxStack`, fills parameters and captures, and calls `runLoop(frame, mod, slots, slots + stackBase, mod.code().data())` at `:487`. `resumeFrames` (`:517-551`) rebuilds a frame from a snapshot record and calls `runLoop` at `:548`. **Both must call `runFrame` after Task 1**, and missing the second is the silent Phase 5 breakage A0-7 invariant 3 names.

2. **`runLoop`'s existing catch chain** (`:917-937`) is, in order: `catch (FutureYield&)` — appends the frame record to the actor's snapshot and rethrows, or refuses when `pendingBase == kNoPendingCall`; `catch (ScalaError& e)` — fills `e.line` and rethrows; `catch (const std::runtime_error& e)` — the protoCore bridge to `RuntimeException`. Task 1 adds `*faultPc` assignments to all three and inserts the three new `std::` branches of A0-6 **before** the `runtime_error` one (a `std::invalid_argument` *is* a `std::logic_error`, not a `std::runtime_error`, so the ordering matters for a different reason: `std::logic_error` must not be caught generically — catch `std::invalid_argument` and `std::out_of_range` by their exact types).

3. **`std::out_of_range` derives from `std::logic_error`**, and A0-6 says a `std::logic_error` must not be catchable. Catch `std::out_of_range` and `std::invalid_argument` by their **exact** types, before any `std::logic_error` clause, and never add a `catch (const std::logic_error&)` clause at all. A unit test pins it: a `std::logic_error` thrown from a test native must escape every Scala `catch`.

4. **`ProtoContext::returnValue` is free on the exception path.** `execute` writes it only after `runLoop` returns (`:486`), and `resumeFrames` only after its own `runLoop` returns (`:549`). A0-2 uses it as the per-frame exception root; nothing else does.

5. **A pattern-match cascade is already a compiler primitive.** `compileMatch` (`src/compiler/CompilePatterns.cpp`) compiles `e match { case … }` into type tests (`TEST_TYPE`, `TEST_PROTO`), extraction (`UNAPPLY_FIELDS`, `UNCONS`), binding and `MATCH_ERROR` on no match. A `catch` body is **the same cascade with `RETHROW` instead of `MATCH_ERROR`** at the end. Task 2 reuses `compileMatch` with one flag rather than writing a second cascade.

6. **Class membership uses per-class marker attributes, not protoCore's `isInstanceOf`.** `TEST_PROTO` tests `v->getAttribute(ctx, typeKeySymbol) == PROTO_TRUE` (`ExecutionEngine.cpp:861-866`), a Phase 2 workaround for R3 (protoCore's `isInstanceOf` stops after 50 visited objects). `case e: ArithmeticException` therefore works exactly as `case p: Point` does, and the exception hierarchy of Task 4 must install its marker keys the same way every Scala class does — which `MAKE_CLASS` already handles, because the hierarchy is written in the prelude.

7. **`show` on an exception instance calls its own `toString` through the active engine** (`src/runtime/Values.h`), so `println(e)` and an uncaught exception's report both go through the prelude's `Throwable.toString`. Keep `ScalaError`'s `what()` format (`ClassName: message`, D14) identical to it so the two reporting paths agree.

8. **Materialisation must be lazy.** The uncaught path — a script that throws and dies — must not allocate an exception instance, because it may be dying of `OutOfMemoryError`. `runFrame` searches the handler table **before** materialising, and `Session`'s top-level report uses `ScalaError::className()`/`message()` as it does today.

9. **`Desugar` is where `enum` and nested templates are lowered.** `Desugarer` (`src/frontend/Desugar.cpp:9-613`) already has `synthesizeCompanions`, `syntheticApply` and `syntheticUnapply`, which is exactly the machinery an `enum` companion needs. `TemplateDef` carries `synthetic` (`AST.h:213-226`) for generated templates.

10. **`GlobalTable` already supports qualified type keys.** `declareType(name)` returns `"@Name"` (or `"@Name#N"`), and `findTypeByKey` keeps every type reachable by key forever (`GlobalTable.h:88-122`). A nested template's key `@O.C` needs no new mechanism: `.` cannot occur in a Scala identifier, so it can never collide, exactly as `@O.type` and `<init>2` already do not.

11. **Rooting across an allocation.** Anything built in a loop inside a native or inside `runFrame` must be kept in a slot, not a C++ local, across an allocation — the Phase 5 `Mailbox::push` bug. In this phase that applies to `materialiseError` (which allocates the message string, then the instance) and to the callee prologue's default evaluation (each default runs arbitrary bytecode, so the already-bound arguments must live in the frame's slots, which they do by construction).

12. **Every concurrency fixture is deterministic in its output** and carries a CTest timeout. A fixture never prints a value that depends on interleaving; it prints a count, a sum or a sorted result the single-method invariant makes exact.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/runtime/Errors.h` (modify) | `ScalaThrow` (A0-2), deriving from `std::exception` and **not** `std::runtime_error` |
| `src/compiler/Opcodes.h` (modify) | `THROW = 96`, `RETHROW = 97`, `CALL_KW = 80`, their comments |
| `src/compiler/BytecodeModule.h` / `BytecodeModule.cpp` (modify) | `struct Handler`, the handler table, `addHandler`, `handlerFor`, `paramNames`/`paramNameSymbols`, per-parameter default block indices, `opName` for the three new opcodes, `disassemble` printing the handler table |
| `src/runtime/ExecutionEngine.h` / `ExecutionEngine.cpp` (modify) | `runFrame`, `runFrameRaising`, `enterHandler`, `materialiseError`; `runLoop` gains `faultPc` and the three `std::` translation branches; `THROW`, `RETHROW`, `CALL_KW`; `superSend` handles an exact `SuperSite`; `sendKeywords` handles a Scala callee; `execute`'s prologue binds keywords and defaults; `resumeFrames` calls `runFrame` |
| `src/frontend/Parser.h` / `Parser.cpp` (modify) | `parseTry`, `parseThrow`, `parseCatchCases`, `parseEnumDef`, `parseExtension`, `super[T]`, nested templates, repeated class parameter clauses |
| `src/frontend/AST.h` / `AST.cpp` (modify) | `Try`, `Throw`, `Super`, `EnumDef`, `ExtensionDef` nodes and their `NodeKind`s; `TemplateDef::enclosing` |
| `src/frontend/Desugar.cpp` (modify) | `enum` → sealed class + cases + companion; nested templates lifted with qualified names; `extension` → a template-less member installation |
| `src/compiler/Compiler.h` / `Compiler.cpp` / `CompilePatterns.cpp` / `CompileTemplates.cpp` (modify) | `compileTry`, `compileThrow`, `compileCatch` (the `compileMatch` cascade with `RETHROW`), the finally-stack for `return`, `compileExtension`, qualified type keys, named/default argument call sites, `CALL_KW` |
| `src/compiler/ClassInfo.h` / `ClassInfo.cpp` (modify) | `kThrowableKey`, `kEnumKey`; `builtinTypes()` gains `Enum`; `ClassInfo::enclosingKey` |
| `src/runtime/Runtime.h` / `Runtime.cpp` (modify) | `enumProto`, the `__ordinal__`/`__enumname__`/`__message__`/`__cause__` keys, `PreludeHooks::throwableClasses` |
| `src/runtime/Primitives.h` / `Primitives.cpp` (modify) | `Enum.ordinal`/`toString`; the `__kwprobe` stand-in of Task 12; `bindPreludeHooks` resolves the exception classes |
| `src/runtime/ActorScheduler.cpp` (modify) | `deliver`'s catch chain (A0-7 invariant 6) |
| `src/runtime/Futures.h` / `Futures.cpp` (modify) | a failure re-enqueues a suspended actor with a raise flag (A0-7 invariant 5) |
| `src/runtime/ActorPrimitives.cpp` (modify) | `await`'s failure path raises the carried `Throwable`; `Thread.start`'s body catch chain |
| `lib/prelude.scala` (modify) | the `Throwable` hierarchy; `Failure` carries a `Throwable`; `RuntimeError` removed; `Priority` becomes an `enum`; `Try`'s `recover`/`recoverWith` take a `Throwable` |
| `src/repl/Session.cpp` (modify) | the top-level report handles `ScalaThrow` as well as `ScalaError` |
| `src/main.cpp` (modify) | the outermost report distinguishes a Scala exception from an internal error |
| `tests/unit/test_engine.cpp`, `test_parser.cpp`, `test_desugar.cpp`, `test_compiler.cpp`, `test_bytecode.cpp`, `test_objectmodel.cpp`, `test_primitives.cpp`, `test_scheduler.cpp` (modify); `tests/unit/test_exceptions.cpp` (new); `tests/unit/CMakeLists.txt` (modify) | Unit tests |
| `tests/conformance/20-exceptions/`, `21-enums/`, `22-super-and-extensions/`, `23-named-arguments/` (new); `tests/conformance/13-actors/`, `14-futures/` (modify); `tests/CMakeLists.txt` (modify) | Fixtures and their timeouts |
| `tests/cli/script-errors.sh`, `tests/cli/repl-errors.sh`, `tests/cli/actors-shutdown.sh` (modify) | Uncaught-exception reporting, the REPL, the actor paths |
| `docs/tutorial/11-exceptions.md`, `docs/tutorial/12-enums-and-sealed-hierarchies.md` (new); `docs/TUTORIAL.md`, `docs/tutorial/02-*.md`, `03-*.md`, `05-*.md`, `06-*.md` (modify); `tests/conformance/tutorial/11-*`, `12-*` (new) | The Documentation track |
| `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `docs/DESIGN.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt` | Status, D71–D89, the retirements of A0-16, version 0.5.0 |

The library order is unchanged: `frontend ← compiler ← runtime ← repl ← protoscala`. No new source file is added to `protoscala_runtime`.

### Opcodes added in this phase

| # | Name | Stack effect | Notes |
|---|---|---|---|
| 80 | `CALL_KW` | `[f a1..an v1..vm] -> [r]` | operand: a `KwSendSite` constant (positional count, keyword names); `f(x = 1)` on a function value (A0-11, A0-12) |
| 96 | `THROW` | `[v] -> throws` | `v` must answer the `@Throwable` marker (A0-5); `null` raises `NullPointerException` |
| 97 | `RETHROW` | `[] -> throws` | operand: the local slot a `Finally` handler saved the in-flight value in (A0-4) |

`Opcodes.h`'s `// 80..95   reserved (object model)` comment becomes `// 80..95   reserved; 80 = CALL_KW (Phase 4)`, and `// 96..127  exceptions, Phase 4: THROW (+ per-module handler table)` becomes `// 96..127  exceptions; 96 = THROW, 97 = RETHROW (Phase 4)`. `docs/STATUS.md`'s opcode table gains the three rows (Task 14).

---

### Task 1: The exception carrier, the handler table and the per-frame retry loop

**Files:**
- Modify: `src/runtime/Errors.h`, `src/compiler/Opcodes.h`, `src/compiler/BytecodeModule.h`, `src/compiler/BytecodeModule.cpp`, `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`
- Test: `tests/unit/test_exceptions.cpp` (new), `tests/unit/test_bytecode.cpp` (modify), `tests/unit/CMakeLists.txt` (modify)

**Interfaces:** other tasks consume exactly these.

- `src/runtime/Errors.h`:
```cpp
// ScalaThrow — an in-flight Scala exception value.
//
// It derives from std::exception and deliberately NOT from std::runtime_error:
// ExecutionEngine::runLoop catches `const std::runtime_error&` and turns it into
// a RuntimeException (the protoCore-error bridge), which would swallow every
// Scala throw if ScalaThrow were a runtime_error.
//
// `value` is re-rooted by every frame the exception passes through (plan A0-2):
// the frame writes it into its own ProtoContext::returnValue before anything
// allocates. The only window in which it is unrooted is between one frame's
// context being destroyed and the next frame's catch running, and nothing in
// that window allocates.
class ScalaThrow : public std::exception {
public:
    explicit ScalaThrow(const proto::ProtoObject* v) : value(v) {}
    const char* what() const noexcept override { return "protoScala exception"; }
    const proto::ProtoObject* value;
};
```
- `src/compiler/BytecodeModule.h`, inside `class BytecodeModule`:
```cpp
    enum class HandlerKind : std::uint8_t { Catch, Finally };
    // One protected region of this module. `[startPc, endPc)` are instruction
    // word indices; `handlerPc` is where the handler body starts; `stackDepth`
    // is the operand-stack depth at the `try`'s entry, which the handler resets
    // to; `slot` is the local slot the caught value is written into.
    struct Handler {
        std::size_t startPc = 0;
        std::size_t endPc = 0;
        std::size_t handlerPc = 0;
        int stackDepth = 0;
        int slot = 0;
        HandlerKind kind = HandlerKind::Catch;
    };
    // Appends an entry. The compiler appends nested `try`s before enclosing
    // ones, and a `try`'s Catch entry before its Finally entry, so table order
    // is search order (plan A0-4).
    std::size_t addHandler(const Handler& h);
    void patchHandlerBody(std::size_t index, std::size_t handlerPc);
    // The first entry whose range contains `pc`, or nullptr.
    const Handler* handlerFor(std::size_t pc) const;
    const std::vector<Handler>& handlers() const { return handlers_; }
```
- `src/runtime/ExecutionEngine.h`, private:
```cpp
    // One frame, with its handler table active. `execute` and `resumeFrames`
    // call this, never runLoop directly (plan A0-3): the handler body is entered
    // by `continue`, outside the C++ catch block, so it runs with no live C++
    // handler and suspends cooperatively like any other bytecode.
    const proto::ProtoObject* runFrame(proto::ProtoContext& frame, const BytecodeModule& mod,
                                       const proto::ProtoObject** slots,
                                       const proto::ProtoObject** sp, const Instr* ip);
    // Enters `h`: resets the operand stack, writes `value` into the handler's
    // slot and sets `*ip`. Returns the new stack pointer.
    const proto::ProtoObject** enterHandler(proto::ProtoContext& frame, const BytecodeModule& mod,
                                            const proto::ProtoObject** slots,
                                            const BytecodeModule::Handler& h,
                                            const proto::ProtoObject* value, const Instr** ip);
    // A native ScalaError as a prelude Throwable instance. Allocates, so it is
    // called only once a handler is known to exist (design note 8).
    const proto::ProtoObject* materialiseError(proto::ProtoContext* ctx, const ScalaError& e);
```

- [ ] **Step 1: Add `ScalaThrow` and pin the inheritance trap**

Add the class above to `src/runtime/Errors.h` (which gains `#include "protoCore.h"` for `proto::ProtoObject`). Then, in `tests/unit/test_exceptions.cpp` (new; add it to `tests/unit/CMakeLists.txt`'s `protoscala_unit_tests` source list):

```cpp
#include "runtime/Errors.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <type_traits>

using namespace protoScala;

// runLoop catches `const std::runtime_error&` and turns it into a
// RuntimeException. If ScalaThrow ever became a runtime_error, every Scala
// `throw` would be silently rewritten into a RuntimeException and every
// user-defined exception class would stop being catchable by its own name.
TEST(ScalaThrowShape, IsNotARuntimeError) {
    static_assert(std::is_base_of_v<std::exception, ScalaThrow>);
    static_assert(!std::is_base_of_v<std::runtime_error, ScalaThrow>);
    static_assert(!std::is_base_of_v<std::logic_error, ScalaThrow>);
    try {
        throw ScalaThrow(nullptr);
    } catch (const std::runtime_error&) {
        FAIL() << "ScalaThrow was caught as a std::runtime_error";
    } catch (const ScalaThrow&) {
        SUCCEED();
    }
}

// ScalaError stays a runtime_error (53 native throw sites rely on it), and
// runLoop's catch order — ScalaError before runtime_error — is what keeps it
// from being re-wrapped.
TEST(ScalaThrowShape, ScalaErrorIsStillARuntimeError) {
    static_assert(std::is_base_of_v<std::runtime_error, ScalaError>);
}
```

- [ ] **Step 2: Add the three opcodes**

In `src/compiler/Opcodes.h`, inside `enum class Op`, replace the `// 80..95   reserved (object model)` line with:

```cpp
    // Phase 4: a keyword-argument call of a function value (plan A0-11, A0-12).
    // The keyword ProtoSparseList is keyed by the address of the interned
    // parameter-name symbol, protoCore's own convention (A0-12).
    CALL_KW        = 80,  // [f a1..an v1..vm] -> [r]     operand: KwSendSite constant
    // 81..95   reserved (object model)
```

and replace the `// 96..127  exceptions, Phase 4: THROW (+ per-module handler table)` line with:

```cpp
    // Phase 4: exceptions (DESIGN §7). The protected regions live in the
    // module's handler table, not in the instruction stream.
    THROW          = 96,  // [v] -> throws                v must be a Throwable (A0-5)
    RETHROW        = 97,  // [] -> throws                 operand: the local slot a
                          // Finally handler saved the in-flight value in (A0-4)
    // 98..127  reserved (exceptions)
```

In `src/compiler/BytecodeModule.cpp`'s `opName`, add `case Op::CALL_KW: return "CALL_KW";`, `case Op::THROW: return "THROW";`, `case Op::RETHROW: return "RETHROW";`.

- [ ] **Step 3: Add the handler table**

In `src/compiler/BytecodeModule.h` add the declarations under **Interfaces** and the member `std::vector<Handler> handlers_;`. In `src/compiler/BytecodeModule.cpp`:

```cpp
std::size_t BytecodeModule::addHandler(const Handler& h) {
    handlers_.push_back(h);
    return handlers_.size() - 1;
}

void BytecodeModule::patchHandlerBody(std::size_t index, std::size_t handlerPc) {
    handlers_[index].handlerPc = handlerPc;
}

const BytecodeModule::Handler* BytecodeModule::handlerFor(std::size_t pc) const {
    // Table order is search order: the compiler appends nested try blocks before
    // enclosing ones, and each try's Catch entry before its Finally entry
    // (plan A0-4), so the first containing entry is the innermost applicable one.
    for (const Handler& h : handlers_)
        if (pc >= h.startPc && pc < h.endPc) return &h;
    return nullptr;
}
```

and extend `disassemble()` so a module with handlers prints them after its code, one line each: `handler [12, 31) -> 34  depth=2 slot=3 catch`. That listing is how every later task's `--disassemble` check reads the table.

- [ ] **Step 4: Thread `faultPc` through `runLoop`**

Change `runLoop`'s declaration and definition to take `std::size_t* faultPc` as the last parameter, and set it in all three existing catch clauses:

```cpp
    } catch (FutureYield&) {
        // A cooperative suspension, not an error: this frame is part of a
        // suspended chain. It never reaches runFrame's handler search, and no
        // `finally` runs, because the frame will be resumed rather than
        // abandoned (plan A0-7 invariants 1 and 2).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (pendingBase == kNoPendingCall)
            throw ScalaError("UnsupportedOperationException",
                             "await is not supported here: " + mod.name() +
                                 " cannot be suspended at this instruction");
        appendSuspendedFrame(&frame, layout_, mod, static_cast<unsigned>(ip - code), pendingBase,
                             slots);
        throw;
    } catch (ScalaThrow&) {
        // A Scala exception: runFrame searches this module's handler table.
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        throw;
    } catch (ScalaError& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        if (e.line == 0) e.line = mod.lineAt(*faultPc);
        throw;
    } catch (const std::invalid_argument& e) {
        // protoCore argument errors (e.g. asIntegerString with a bad base).
        // Caught by its exact type: std::invalid_argument derives from
        // std::logic_error, and a std::logic_error is a VM or compiler defect
        // that must stay uncatchable (plan A0-6, D73).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IllegalArgumentException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::out_of_range& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("IndexOutOfBoundsException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::overflow_error& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("ArithmeticException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::bad_alloc& e) {
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("OutOfMemoryError", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    } catch (const std::runtime_error& e) {
        // A protoCore error (e.g. "Objects are not integer types for division.")
        // becomes a Scala RuntimeException; it is never swallowed (DESIGN §7).
        *faultPc = static_cast<std::size_t>(ip - code) - 1;
        ScalaError se("RuntimeException", e.what());
        se.line = mod.lineAt(*faultPc);
        throw se;
    }
```

Note the ordering: `std::overflow_error` derives from `std::runtime_error`, so it must precede it; `std::invalid_argument` and `std::out_of_range` derive from `std::logic_error`, which has **no** clause of its own, so a `std::logic_error` that is neither of them escapes to `main.cpp` as an internal error (A0-6, D73).

- [ ] **Step 5: Write `runFrame`, `enterHandler` and `materialiseError`**

```cpp
const proto::ProtoObject** ExecutionEngine::enterHandler(proto::ProtoContext& frame,
                                                         const BytecodeModule& mod,
                                                         const proto::ProtoObject** slots,
                                                         const BytecodeModule::Handler& h,
                                                         const proto::ProtoObject* value,
                                                         const Instr** ip) {
    const unsigned stackBase =
        static_cast<unsigned>(mod.arity()) + static_cast<unsigned>(mod.localCount());
    slots[h.slot] = value;                       // the caught value, in a traced slot
    *ip = mod.code().data() + h.handlerPc;
    return slots + stackBase + h.stackDepth;     // the try's entry depth
}

const proto::ProtoObject* ExecutionEngine::materialiseError(proto::ProtoContext* ctx,
                                                            const ScalaError& e) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const proto::ProtoObject** slot = scope.getAutomaticLocals();
    // A named prelude class if there is one, else RuntimeException carrying the
    // original class name in its message (plan A0-6).
    auto it = L.hooks.throwableClasses.find(e.className());
    if (it == L.hooks.throwableClasses.end()) {
        slot[0] = makeString(&scope, e.className() + ": " + e.message());
        const proto::ProtoObject* r =
            construct(&scope, const_cast<proto::ProtoObject*>(L.hooks.runtimeException), slot, 1);
        scope.returnValue = r;
        return r;
    }
    slot[0] = makeString(&scope, e.message());
    const proto::ProtoObject* r =
        construct(&scope, const_cast<proto::ProtoObject*>(it->second), slot, 1);
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* ExecutionEngine::runFrame(proto::ProtoContext& frame,
                                                    const BytecodeModule& mod,
                                                    const proto::ProtoObject** slots,
                                                    const proto::ProtoObject** sp,
                                                    const Instr* ip) {
    for (;;) {
        std::size_t faultPc = 0;
        try {
            return runLoop(frame, mod, slots, sp, ip, &faultPc);
        } catch (ScalaThrow& t) {
            // Re-root before anything allocates: the frame below has already
            // been destroyed (plan A0-2).
            frame.returnValue = t.value;
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (!h) throw;
            sp = enterHandler(frame, mod, slots, *h, t.value, &ip);
            continue;   // leaves the catch block: the handler body runs with no
                        // live C++ handler, so it suspends like any other code
        } catch (ScalaError& e) {
            const BytecodeModule::Handler* h = mod.handlerFor(faultPc);
            if (!h) throw;                       // the uncaught path allocates nothing
            const proto::ProtoObject* v = materialiseError(&frame, e);
            frame.returnValue = v;
            sp = enterHandler(frame, mod, slots, *h, v, &ip);
            continue;
        }
    }
}
```

`RuntimeLayout::PreludeHooks` gains `std::unordered_map<std::string, const proto::ProtoObject*> throwableClasses;` and `const proto::ProtoObject* runtimeException = nullptr;`, both filled by `bindPreludeHooks` in Task 4. Until Task 4 runs, `materialiseError` has nothing to build from, so **Task 1 stops short of enabling the `ScalaError` branch**: write it, and guard it with `if (!L.hooks.bound || L.hooks.throwableClasses.empty()) throw;` so this task's build is green and Task 4 removes the guard.

- [ ] **Step 6: Point `execute` and `resumeFrames` at `runFrame`**

In `ExecutionEngine.cpp`, change `:487` from `runLoop(frame, mod, slots, slots + stackBase, mod.code().data())` to `runFrame(frame, mod, slots, slots + stackBase, mod.code().data())`, and `:548` from `runLoop(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset)` to `runFrame(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset)`.

**The second change is the one that keeps Phase 5 working**: without it a resumed frame has no handler table active and `try { f.await } catch { … }` silently catches nothing (A0-7 invariant 3). Add a comment at the call site saying so.

- [ ] **Step 7: Implement `THROW` and `RETHROW`**

In `runLoop`'s `switch (op)`, after `case Op::SEND_APPLY`'s block:

```cpp
                case Op::THROW: {
                    const proto::ProtoObject* v = *--sp;
                    if (v == PROTO_NONE)
                        throw ScalaError("NullPointerException", "throw null");
                    // Only a Throwable may be thrown (plan A0-5), tested with the
                    // Phase 2 marker attribute, not protoCore's isInstanceOf (R3).
                    if (v->getAttribute(&frame, L.throwableKey) != PROTO_TRUE)
                        throw ScalaError("IllegalArgumentException",
                                         "throw expects a Throwable, got " + typeName(&frame, L, v));
                    throw ScalaThrow(v);
                }
                case Op::RETHROW: {
                    // A Finally handler saved the in-flight value in slot
                    // `operand`; re-raise it after the cleanup has run (A0-4).
                    const proto::ProtoObject* v = slots[operand];
                    if (v == PROTO_NONE || !v)
                        throw std::logic_error("RETHROW with no saved exception in " + mod.name());
                    throw ScalaThrow(v);
                }
```

`RuntimeLayout` gains `const proto::ProtoString* throwableKey = nullptr; // "@Throwable"`, interned in `Runtime::Runtime`.

- [ ] **Step 8: Unit-test the plumbing with a hand-assembled module**

`EvalHarness::runModule` runs a hand-assembled top-level module, which is exactly how to test the retry loop before any syntax exists. Add to `tests/unit/test_exceptions.cpp`:

```cpp
#include "EvalHarness.h"
#include "compiler/BytecodeModule.h"

// A module that throws a value the harness put in a local, with a Catch handler
// covering the THROW: the retry loop must enter the handler and return its value.
TEST(HandlerTable, ACatchEntryRedirectsExecution) {
    EvalHarness h;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<handler-test>");
    mod->setLocalCount(2);
    mod->setMaxStack(2);
    const std::size_t tryStart = mod->pos();
    mod->emit(Op::PUSH_CONST, mod->addString("boom"), 1);
    mod->emit(Op::POP, 0, 1);                     // stands in for a throwing call
    const std::size_t tryEnd = mod->pos();
    const std::size_t body = mod->pos();
    mod->emit(Op::PUSH_CONST, mod->addInt(7), 1);
    mod->emit(Op::RETURN, 0, 1);
    mod->addHandler({tryStart, tryEnd, body, 0, 0, BytecodeModule::HandlerKind::Catch});
    EXPECT_EQ(mod->handlerFor(tryStart)->handlerPc, body);
    EXPECT_EQ(mod->handlerFor(tryEnd), nullptr);
    EXPECT_EQ(h.runModule(std::move(mod)), "7");
}

TEST(HandlerTable, SearchOrderIsTableOrder) {
    BytecodeModule mod;
    mod.addHandler({10, 20, 100, 0, 0, BytecodeModule::HandlerKind::Catch});   // inner
    mod.addHandler({0, 40, 200, 0, 1, BytecodeModule::HandlerKind::Finally});  // outer
    EXPECT_EQ(mod.handlerFor(15)->handlerPc, 100u);   // innermost wins
    EXPECT_EQ(mod.handlerFor(5)->handlerPc, 200u);
    EXPECT_EQ(mod.handlerFor(50), nullptr);
}
```

**Done when:** `cmake --build build_release` has no warning; `ctest --test-dir build_release -R 'ScalaThrowShape|HandlerTable' --output-on-failure` passes all four cases; `ctest --test-dir build_release --output-on-failure` is fully green (no syntax reaches the new opcodes yet, so behaviour is unchanged); and `PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release` and `=16` are green — that is the check that redirecting `resumeFrames` through `runFrame` did not disturb cooperative `await`.

---

### Task 2: `try` / `catch` with pattern-matched handlers

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/AST.cpp`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`, `src/frontend/Desugar.cpp`, `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `src/compiler/CompilePatterns.cpp`
- Test: `tests/conformance/20-exceptions/*.scala` (new), `tests/unit/test_parser.cpp`, `test_bytecode.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `NodeKind` gains `Try` and `Throw`;
```cpp
// try B catch { case ... } finally F. `cases` empty: no catch clause.
// `finallyBody` null: no finally clause.
struct Try : Node {
    explicit Try(SourcePos p) : Node(NodeKind::Try, p) {}
    NodePtr body;
    std::vector<CaseDef> cases;
    NodePtr finallyBody;
};
struct Throw : Node {
    explicit Throw(SourcePos p) : Node(NodeKind::Throw, p) {}
    NodePtr value;
};
```
- `src/frontend/Parser.h`, private: `NodePtr parseTry();` and `NodePtr parseThrow();`
- `src/compiler/Compiler.h`, private:
```cpp
    void compileTry(const Try& n);
    void compileThrow(const Throw& n);
    // The `catch` cascade: compileMatch's machinery with RETHROW instead of
    // MATCH_ERROR when no case matches (design note 5). `slot` holds the caught
    // value; the scrutinee is that slot.
    void compileCatchCases(const std::vector<CaseDef>& cases, int slot, SourcePos pos);
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/20-exceptions/throw-and-catch.scala`:
```scala
// EXPECT: caught boom
@main def run(): Unit =
  try
    throw new RuntimeException("boom")
  catch
    case e: RuntimeException => println("caught " + e.getMessage)
```

`tests/conformance/20-exceptions/throw-and-catch-braces.scala` is the same program in brace syntax.

`tests/conformance/20-exceptions/catch-by-type.scala`:
```scala
// EXPECT: arithmetic
@main def run(): Unit =
  try
    println(1 / 0)
  catch
    case e: IllegalArgumentException => println("illegal")
    case e: ArithmeticException      => println("arithmetic")
    case e: Throwable                => println("other")
```

`tests/conformance/20-exceptions/catch-is-a-pattern-match.scala`:
```scala
// EXPECT: message was boom
case class AppError(code: Int, detail: String) extends Exception(detail)
@main def run(): Unit =
  try
    throw AppError(7, "boom")
  catch
    case AppError(7, d)              => println("message was " + d)
    case AppError(c, _) if c > 100   => println("big " + c)
    case e: Exception                => println("plain")
```

`tests/conformance/20-exceptions/catch-does-not-match-propagates.scala`:
```scala
// EXPECT-ERROR: IllegalStateException: nope
@main def run(): Unit =
  try
    throw new IllegalStateException("nope")
  catch
    case e: ArithmeticException => println("wrong")
```

`tests/conformance/20-exceptions/nested-try.scala`:
```scala
// EXPECT: inner outer
@main def run(): Unit =
  try
    try
      throw new RuntimeException("a")
    catch
      case e: RuntimeException =>
        print("inner ")
        throw new IllegalStateException("b")
  catch
    case e: IllegalStateException => println("outer")
```

`tests/conformance/20-exceptions/try-is-an-expression.scala`:
```scala
// EXPECT: 42 -1
@main def run(): Unit =
  val good = try "42".toInt catch case e: NumberFormatException => -1
  val bad = try "x".toInt catch case e: NumberFormatException => -1
  println(good.toString + " " + bad)
```

`tests/conformance/20-exceptions/catch-in-a-loop.scala`:
```scala
// EXPECT: 5 5
// The retry loop must not grow the native stack per caught exception
// (plan A0-3 option (c) is what this rules out).
@main def run(): Unit =
  var caught = 0
  var ok = 0
  var i = 0
  while i < 10 do
    try
      if i % 2 == 0 then throw new RuntimeException("x") else ok += 1
    catch
      case e: RuntimeException => caught += 1
    i += 1
  println(caught.toString + " " + ok)
```

`tests/conformance/20-exceptions/throw-a-non-throwable.scala`:
```scala
// EXPECT-ERROR: throw expects a Throwable, got Int
@main def run(): Unit =
  throw 5.asInstanceOf[RuntimeException]
```

`tests/conformance/20-exceptions/throw-null.scala`:
```scala
// EXPECT-ERROR: NullPointerException
@main def run(): Unit =
  val e: RuntimeException = null
  throw e
```

`tests/conformance/20-exceptions/user-defined-exception.scala`:
```scala
// EXPECT: MyError: custom 7
class MyError(msg: String, val code: Int) extends Exception(msg)
@main def run(): Unit =
  try
    throw new MyError("custom", 7)
  catch
    case e: MyError => println(e.toString + " " + e.code)
```

Add `-braces` twins for `catch-by-type`, `catch-is-a-pattern-match`, `nested-try` and `catch-in-a-loop`. Verify `throw-and-catch`, `catch-by-type`, `catch-is-a-pattern-match`, `nested-try`, `try-is-an-expression`, `catch-in-a-loop` and `user-defined-exception` against scalac. `catch-does-not-match-propagates` pins protoScala's own report format (D14: `file:line: error: Class: message`), so its `// EXPECT-ERROR:` line is the substring `IllegalStateException: nope` and a comment records that scalac prints a JVM stack trace instead.

- [ ] **Step 2: Parse `throw` and `try`**

In `src/frontend/Parser.cpp`, replace the two `unsupported` lines of `parseExprNoPlaceholders` (`:252-253`):

```cpp
        case TokenKind::KwThrow:  return parseThrow();
        case TokenKind::KwTry:    return parseTry();
```

and add:

```cpp
// throw Expr
NodePtr Parser::parseThrow() {
    const Token& t = advance();                 // `throw`
    auto n = std::make_unique<Throw>(t.pos);
    while (at(TokenKind::Indent)) advance();    // Layout opens a region after `throw`
    n->value = parseExpr();
    while (at(TokenKind::Outdent)) advance();
    return n;
}

// try Expr [catch CaseClauses] [finally Expr]
// Layout already opens a region after `try`, `catch` and `finally`, and pairs
// `catch` with a pending `try` and `finally` with a pending `try` or `catch`
// (Layout.cpp:65-77, :104-105), so this reads them as ordinary tokens.
NodePtr Parser::parseTry() {
    const Token& t = advance();                 // `try`
    auto n = std::make_unique<Try>(t.pos);
    while (at(TokenKind::Indent)) advance();
    n->body = parseExpr();
    while (at(TokenKind::Newline) || at(TokenKind::Outdent)) advance();
    if (at(TokenKind::KwCatch)) {
        advance();
        n->cases = parseCatchCases();
        while (at(TokenKind::Newline) || at(TokenKind::Outdent)) advance();
    }
    if (at(TokenKind::KwFinally)) {
        advance();
        while (at(TokenKind::Indent)) advance();
        n->finallyBody = parseExpr();
        while (at(TokenKind::Outdent)) advance();
    }
    if (n->cases.empty() && !n->finallyBody)
        fail("a try needs a catch or a finally", t);
    return n;
}
```

`parseCatchCases()` accepts both Scala forms: a braced or indented list of `case` clauses, **and** the single-expression form `catch handler` where `handler` is a function value (`catch h` with `h: Throwable => T`). Implement the second as one synthetic case: `case __ex => h(__ex)`, so the compiler has one shape. Record it as **D84**: `catch someFunction` is accepted and rewritten to `case e => someFunction(e)`, which is what Scala's `catch` of a `PartialFunction` means for a total function; a genuine `PartialFunction` that is not defined for the exception rethrows in Scala and, here, raises `MatchError` from inside the function — so prefer `case` clauses.

Reuse `parseMatch`'s existing clause parser rather than writing a second one: extract the body of `parseMatch` after the `match` keyword into `std::vector<CaseDef> Parser::parseCaseClauses()` and call it from both.

- [ ] **Step 3: Let `Desugar` walk the new nodes**

In `src/frontend/Desugar.cpp`'s `Desugarer::expr`, add:

```cpp
        case NodeKind::Throw: {
            auto& t = as<Throw>(*n);
            t.value = expr(std::move(t.value));
            return n;
        }
        case NodeKind::Try: {
            auto& t = as<Try>(*n);
            t.body = expr(std::move(t.body));
            for (CaseDef& c : t.cases) {
                if (c.guard) c.guard = expr(std::move(c.guard));
                c.body = expr(std::move(c.body));
            }
            if (t.finallyBody) t.finallyBody = expr(std::move(t.finallyBody));
            return n;
        }
```

A `try` whose result is discarded goes through `discardValue` like any other expression; a `finally` body's value is always discarded (Scala's rule), which `compileTry` handles by emitting a `POP`.

- [ ] **Step 4: Compile `throw`**

In `src/compiler/Compiler.cpp`, add `case NodeKind::Throw: compileThrow(as<Throw>(n)); return;` to `compileExpr`'s switch and:

```cpp
// throw e: evaluate e, then THROW. The expression's static type is Nothing in
// Scala; here it simply never produces a value, so the stack effect is -1 and
// the compiler records the frame as terminated for the purposes of the
// following code's depth accounting.
void Compiler::compileThrow(const Throw& n) {
    compileExpr(*n.value);
    emit(Op::THROW, 0, n.pos, -1);
    // `throw` yields Nothing; a context that expects a value (val x = throw e)
    // never reaches the store, but the depth bookkeeping must still balance.
    emit(Op::PUSH_UNIT, 0, n.pos, +1);
}
```

- [ ] **Step 5: Compile `try` / `catch`**

```cpp
// try B catch { case ... } [finally F]
//
// Layout of the emitted code:
//
//     <try body B>            [tryStart, tryEnd)     protected
//     JUMP after              -- normal path skips the handler
//   catchBody:                                       the Catch entry's target
//     <cascade over slot>     -- compileCatchCases; RETHROW when nothing matches
//   after:
//
// The Catch handler-table entry covers [tryStart, tryEnd) only, so an exception
// raised inside the handler body is NOT caught by this try (plan A0-4).
void Compiler::compileTry(const Try& n) {
    const int entryDepth = fn_->depth;
    const int slot = allocHiddenSlot();            // holds the caught value
    const std::size_t tryStart = fn_->mod->pos();
    if (n.finallyBody) pushFinally(n.finallyBody.get(), slot);
    compileExpr(*n.body);
    const std::size_t tryEnd = fn_->mod->pos();
    const std::size_t skip = fn_->mod->emitJump(Op::JUMP, n.pos);
    std::size_t handlerIndex = 0;
    if (!n.cases.empty()) {
        handlerIndex = fn_->mod->addHandler(
            {tryStart, tryEnd, /*handlerPc=*/0, entryDepth, slot,
             BytecodeModule::HandlerKind::Catch});
        const std::size_t body = fn_->mod->pos();
        fn_->mod->patchHandlerBody(handlerIndex, body);
        fn_->depth = entryDepth;                   // the handler starts at the try's depth
        compileCatchCases(n.cases, slot, n.pos);
    }
    fn_->mod->patchJumpTo(skip, fn_->mod->pos());
    if (n.finallyBody) popFinallyAndEmit(n, tryStart, entryDepth, slot);   // Task 3
}
```

`allocHiddenSlot()` is a new private helper that reserves one local slot the user cannot name (the same mechanism `Desugarer::fresh` uses for for-comprehension temporaries, but on the compiler side, because the slot must survive into the handler). `pushFinally`/`popFinallyAndEmit` are Task 3's; for this task, define them as no-ops that `assert(!n.finallyBody)` so Task 2's build is green and Task 3's fixtures are the ones that exercise them.

- [ ] **Step 6: Compile the `catch` cascade**

In `src/compiler/CompilePatterns.cpp`:

```cpp
// The catch cascade. It is compileMatch's machinery with two differences:
// the scrutinee is already in `slot` (the handler wrote it there), and no match
// RETHROWs instead of raising MatchError — Scala propagates an exception the
// handler does not match rather than replacing it (design note 5).
void Compiler::compileCatchCases(const std::vector<CaseDef>& cases, int slot, SourcePos pos) {
    std::vector<std::size_t> exits;
    for (const CaseDef& c : cases) {
        std::vector<std::size_t> misses;
        compilePattern(*c.pattern, slot, misses);
        if (c.guard) {
            compileExpr(*c.guard);
            misses.push_back(fn_->mod->emitJump(Op::JUMP_IF_FALSE, c.pos));
        }
        compileExpr(*c.body);
        exits.push_back(fn_->mod->emitJump(Op::JUMP, c.pos));
        for (std::size_t m : misses) fn_->mod->patchJumpTo(m, fn_->mod->pos());
    }
    emit(Op::RETHROW, static_cast<std::uint64_t>(slot), pos, 0);
    for (std::size_t e : exits) fn_->mod->patchJumpTo(e, fn_->mod->pos());
}
```

`compilePattern(pattern, slot, misses)` is the existing Phase 2 entry point (`Compiler.h`); it takes the scrutinee's slot and appends the jump sites to patch on a miss, which is exactly what is needed here.

- [ ] **Step 7: Unit-test the emission**

```cpp
TEST(Bytecode, TryEmitsAHandlerEntryCoveringOnlyTheTryBody) {
    const std::string asm_ = disassembleSource(
        "def f(): Int = try 1 catch case e: RuntimeException => 2");
    EXPECT_NE(asm_.find("handler ["), std::string::npos) << asm_;
    EXPECT_NE(asm_.find("catch"), std::string::npos) << asm_;
    EXPECT_NE(asm_.find("RETHROW"), std::string::npos) << asm_;
}

TEST(Bytecode, ThrowEmitsTheThrowOpcode) {
    const std::string asm_ = disassembleSource("def f(): Int = throw new RuntimeException(\"x\")");
    EXPECT_NE(asm_.find("THROW"), std::string::npos) << asm_;
}
```

**Done when:** `ctest --test-dir build_release -R '20-exceptions|Bytecode' --output-on-failure` passes the fourteen fixtures of Step 1 (with `finally`-bearing ones deferred to Task 3) and both unit cases; the seven scalac-verifiable fixtures match byte for byte; `build_release/protoscala --disassemble tests/conformance/20-exceptions/throw-and-catch.scala | grep 'handler \['` prints one line; and the full suite is green, also under `PROTOCORE_HEAP_LIMIT_CELLS=20000` and at `PROTOSCALA_ACTOR_WORKERS=1` and `=16`.

---

### Task 3: `finally`

**Files:**
- Modify: `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`
- Test: `tests/conformance/20-exceptions/finally-*.scala` (new), `tests/unit/test_bytecode.cpp` (modify)

**Interfaces:** `src/compiler/Compiler.h`, in `FunctionState`:
```cpp
        // The enclosing `finally` bodies of the code being compiled, innermost
        // last. A `return` emits them innermost-first before its RETURN, and a
        // `try`'s normal exit emits its own inline (plan A0-4).
        struct ActiveFinally { const Node* body; int slot; };
        std::vector<ActiveFinally> finallys;
```
and, private:
```cpp
    void pushFinally(const Node* body, int slot);
    // Emits the Finally handler entry covering the try body and the catch body,
    // patches it, emits the cleanup inline on the normal path, and pops.
    void popFinallyAndEmit(const Try& n, std::size_t tryStart, int entryDepth, int slot);
    // Emits every enclosing finally body, innermost first. Called before RETURN.
    void emitEnclosingFinallys(SourcePos pos);
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/20-exceptions/finally-runs-on-success.scala`:
```scala
// EXPECT: body finally 1
@main def run(): Unit =
  val r =
    try
      print("body ")
      1
    finally
      print("finally ")
  println(r)
```

`tests/conformance/20-exceptions/finally-runs-on-failure.scala`:
```scala
// EXPECT: finally caught
@main def run(): Unit =
  try
    try
      throw new RuntimeException("x")
    finally
      print("finally ")
  catch
    case e: RuntimeException => println("caught")
```

`tests/conformance/20-exceptions/finally-runs-after-catch.scala`:
```scala
// EXPECT: caught finally
@main def run(): Unit =
  try
    throw new RuntimeException("x")
  catch
    case e: RuntimeException => print("caught ")
  finally
    println("finally")
```

`tests/conformance/20-exceptions/finally-runs-when-catch-does-not-match.scala`:
```scala
// EXPECT-ERROR: IllegalStateException: nope
// The cleanup must still run: its output precedes the error report.
@main def run(): Unit =
  try
    throw new IllegalStateException("nope")
  catch
    case e: ArithmeticException => println("wrong")
  finally
    println("cleanup ran")
```

`tests/conformance/20-exceptions/finally-runs-on-return.scala`:
```scala
// EXPECT: finally 7
def f(): Int =
  try
    return 7
  finally
    print("finally ")
@main def run(): Unit = println(f())
```

`tests/conformance/20-exceptions/finally-nested-order.scala`:
```scala
// EXPECT: inner outer done
def f(): Int =
  try
    try
      return 1
    finally
      print("inner ")
  finally
    print("outer ")
@main def run(): Unit =
  f()
  println("done")
```

`tests/conformance/20-exceptions/finally-that-throws-replaces.scala`:
```scala
// EXPECT: replaced
// A finally that throws replaces the in-flight exception (D71).
@main def run(): Unit =
  try
    try
      throw new RuntimeException("original")
    finally
      throw new IllegalStateException("replacement")
  catch
    case e: IllegalStateException => println("replaced")
```

`tests/conformance/20-exceptions/finally-value-is-discarded.scala`:
```scala
// EXPECT: 1
@main def run(): Unit =
  println(try 1 finally 2)
```

Add `-braces` twins for `finally-runs-on-success`, `finally-runs-on-failure`, `finally-runs-after-catch`, `finally-runs-on-return` and `finally-nested-order`. Verify every one of the eight against scalac — `finally-nested-order` and `finally-that-throws-replaces` are exactly the cases where an implementation drifts silently.

- [ ] **Step 2: Emit the Finally entry and the inline copy**

```cpp
void Compiler::pushFinally(const Node* body, int slot) {
    fn_->finallys.push_back({body, slot});
}

// try B [catch C] finally F emits, in order:
//
//     <B>                 [tryStart, tryEnd)
//     JUMP after
//   catchBody:            <C>  (Task 2)
//     JUMP after
//   finallyBody:          the Finally entry's target
//     <F>; POP            the cleanup, its value discarded (Scala's rule)
//     RETHROW slot        re-raise what the handler saved
//   after:
//     <F>; POP            the same cleanup, on the normal path
//
// The Finally entry covers [tryStart, afterCatch), i.e. the try body AND the
// catch body, so a throw from either one — including the RETHROW a non-matching
// cascade emits — runs the cleanup (plan A0-4).
void Compiler::popFinallyAndEmit(const Try& n, std::size_t tryStart, int entryDepth, int slot) {
    const std::size_t protectedEnd = fn_->mod->pos();
    const std::size_t skip = fn_->mod->emitJump(Op::JUMP, n.pos);
    const std::size_t cleanup = fn_->mod->pos();
    const std::size_t index = fn_->mod->addHandler(
        {tryStart, protectedEnd, cleanup, entryDepth, slot,
         BytecodeModule::HandlerKind::Finally});
    (void)index;
    fn_->depth = entryDepth;
    fn_->finallys.pop_back();                 // the cleanup itself is not protected by itself
    compileExpr(*n.finallyBody);
    emit(Op::POP, 0, n.pos, -1);
    emit(Op::RETHROW, static_cast<std::uint64_t>(slot), n.pos, 0);
    fn_->mod->patchJumpTo(skip, fn_->mod->pos());
    compileExpr(*n.finallyBody);              // the normal path
    emit(Op::POP, 0, n.pos, -1);
}
```

`fn_->finallys.pop_back()` **before** compiling the cleanup is what makes `finally-that-throws-replaces` behave: the cleanup is not protected by its own entry, so a throw from it propagates outward rather than looping.

- [ ] **Step 3: Run the enclosing cleanups before a `return`**

```cpp
// Scala runs every enclosing finally body before a `return` leaves the method,
// innermost first. `return` is method-local here (D11), so the whole stack of
// enclosing finallys of the current FunctionState is what must run.
void Compiler::emitEnclosingFinallys(SourcePos pos) {
    for (auto it = fn_->finallys.rbegin(); it != fn_->finallys.rend(); ++it) {
        compileExpr(*it->body);
        emit(Op::POP, 0, pos, -1);
    }
}
```

and call it from `compileReturn`, immediately before the `RETURN` is emitted and **after** the returned value has been evaluated (Scala evaluates the return expression first, then runs the cleanups). Add a one-line comment at the call site naming the order, because reversing it is the classic bug and `finally-runs-on-return` is what catches it.

- [ ] **Step 4: Unit-test the emission**

```cpp
TEST(Bytecode, FinallyEmitsTwoCopiesAndAFinallyEntry) {
    const std::string asm_ = disassembleSource("def f(): Int = try 1 finally 2");
    EXPECT_NE(asm_.find("finally"), std::string::npos) << asm_;   // the handler line
    EXPECT_NE(asm_.find("RETHROW"), std::string::npos) << asm_;
    // The cleanup appears twice: once as the handler body, once inline.
    std::size_t first = asm_.find("PUSH_CONST");
    std::size_t count = 0, at = 0;
    while ((at = asm_.find("PUSH_CONST", at)) != std::string::npos) { ++count; ++at; }
    EXPECT_GE(count, 3u) << asm_;             // 1, 2 (handler), 2 (normal)
    EXPECT_NE(first, std::string::npos);
}

TEST(Bytecode, ReturnInsideTryFinallyEmitsTheCleanupFirst) {
    const std::string asm_ = disassembleSource("def f(): Int = try { return 7 } finally { 9 }");
    const std::size_t nine = asm_.find("9");
    const std::size_t ret = asm_.find("RETURN");
    ASSERT_NE(nine, std::string::npos) << asm_;
    ASSERT_NE(ret, std::string::npos) << asm_;
    EXPECT_LT(nine, ret) << asm_;
}
```

- [ ] **Step 5: Confirm the `try` depth accounting with a stress fixture**

`tests/conformance/20-exceptions/finally-in-a-loop.scala`:
```scala
// EXPECT: 100000 100000
// 100000 try/finally cycles: the operand stack must return to the try's entry
// depth every time, and the retry loop must not grow the native stack.
@main def run(): Unit =
  var cleanups = 0
  var caught = 0
  var i = 0
  while i < 100000 do
    try
      try
        throw new RuntimeException("x")
      finally
        cleanups += 1
    catch
      case e: RuntimeException => caught += 1
    i += 1
  println(cleanups.toString + " " + caught)
```

**Done when:** `ctest --test-dir build_release -R '20-exceptions' --output-on-failure` passes every fixture of Tasks 2 and 3, including `finally-in-a-loop`; all eight scalac-verifiable `finally` fixtures match byte for byte; both unit cases pass; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000` — `finally-in-a-loop` at that ceiling is what proves the hidden slots and the per-frame `try` region leak nothing.

- [ ] **Step 6: Pin the by-name interaction**

By-name parameters are in the tree as of `bca0352` (Preflight Step 0), so this step is required:

`tests/conformance/20-exceptions/byname-thunk-throws-at-the-use-site.scala`:
```scala
// EXPECT: caught at use
def useIt(x: => Int): Int =
  try x catch case e: RuntimeException => { println("caught at use"); -1 }
def boom: Int = throw new RuntimeException("x")
@main def run(): Unit =
  useIt(boom)
```

A by-name argument's thunk runs where the name is *read*, so the exception is raised inside `useIt`'s `try`, not at the call site. That is Scala's behaviour; verify it against scalac. `useIt` is a top-level `def`, which D53 lists as a call site the compiler resolves, so the argument really is lazy here.

And the D53 boundary, which scalac does not have:

`tests/conformance/20-exceptions/byname-unresolved-call-site-is-eager.scala`:
```scala
// EXPECT: escaped
// D53: at a call site the compiler cannot resolve to a declaration, the argument
// is evaluated once AT THE CALL, so the exception is raised before the callee's
// try is entered and escapes it. scalac resolves this statically and prints
// "caught at use" instead.
class Runner:
  def useIt(x: => Int): Int =
    try x catch case e: RuntimeException => -1
def boom: Int = throw new RuntimeException("x")
@main def run(): Unit =
  val r: Any = new Runner()
  try
    println(r.asInstanceOf[Runner].useIt(boom))
  catch
    case e: RuntimeException => println("escaped")
```

Run it and set the `// EXPECT:` line **from the binary**, not from this plan: whether that particular receiver counts as resolved depends on what D53's implementation admits, and the fixture's job is to pin whatever it is, with the divergence from scalac recorded in the comment. If it turns out to be resolved, rewrite the fixture around a receiver that is not — a list element, or a function value returned from another method — and say in the comment which form finally exercised the eager path.

---

### Task 4: The `Throwable` hierarchy and native error translation

**Files:**
- Modify: `lib/prelude.scala`, `src/compiler/ClassInfo.h`, `src/compiler/ClassInfo.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `src/runtime/ExecutionEngine.cpp`, `src/repl/Session.cpp`, `src/main.cpp`
- Test: `tests/conformance/20-exceptions/native-*.scala` (new), `tests/unit/test_exceptions.cpp` (modify), `tests/cli/script-errors.sh`, `tests/cli/repl-errors.sh` (modify)

**Interfaces:**
- `lib/prelude.scala` gains the hierarchy below. The exact class names are what `materialiseError` looks up, so they must match A0-6's table character for character.
- `RuntimeLayout::PreludeHooks` gains:
```cpp
        // The exception classes materialiseError builds, by unqualified name
        // (plan A0-6). Filled by bindPreludeHooks from the prelude's globals.
        std::unordered_map<std::string, const proto::ProtoObject*> throwableClasses;
        const proto::ProtoObject* runtimeException = nullptr;   // the fallback class
```
- `RuntimeLayout` gains `const proto::ProtoString* throwableKey = nullptr; // "@Throwable"`, `const proto::ProtoString* messageKey = nullptr; // "__message__"`, `const proto::ProtoString* causeKey = nullptr; // "__cause__"`.
- `ClassInfo.h` gains `inline constexpr const char* kThrowableKey = "@Throwable";`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/20-exceptions/native-arithmetic.scala`:
```scala
// EXPECT: caught / by zero
@main def run(): Unit =
  try println(1 / 0)
  catch case e: ArithmeticException => println("caught " + e.getMessage)
```

`tests/conformance/20-exceptions/native-index.scala`:
```scala
// EXPECT: caught index
@main def run(): Unit =
  try println(List(1, 2, 3)(9))
  catch case e: IndexOutOfBoundsException => println("caught index")
```

`tests/conformance/20-exceptions/native-number-format.scala`:
```scala
// EXPECT: caught format
@main def run(): Unit =
  try println("abc".toInt)
  catch case e: NumberFormatException => println("caught format")
```

`tests/conformance/20-exceptions/native-no-such-element.scala`:
```scala
// EXPECT: caught None.get
@main def run(): Unit =
  try println(None.get)
  catch case e: NoSuchElementException => println("caught " + e.getMessage)
```

`tests/conformance/20-exceptions/native-match-error.scala`:
```scala
// EXPECT: caught
@main def run(): Unit =
  try
    val x: Any = 5
    x match
      case s: String => println(s)
  catch case e: MatchError => println("caught")
```

`tests/conformance/20-exceptions/native-no-such-method.scala`:
```scala
// EXPECT: caught
@main def run(): Unit =
  try
    val x: Any = 5
    println(x.asInstanceOf[List[Int]].head)
  catch case e: Throwable => println("caught")
```

`tests/conformance/20-exceptions/native-stack-overflow-is-catchable.scala`:
```scala
// EXPECT: caught overflow
def deep(n: Int): Int = deep(n + 1)
@main def run(): Unit =
  try println(deep(0))
  catch case e: StackOverflowError => println("caught overflow")
```

`tests/conformance/20-exceptions/error-is-not-an-exception.scala`:
```scala
// EXPECT: not an Exception
def deep(n: Int): Int = deep(n + 1)
@main def run(): Unit =
  try
    try println(deep(0))
    catch case e: Exception => println("wrongly caught as Exception")
  catch case e: Error => println("not an Exception")
```

`tests/conformance/20-exceptions/hierarchy-ancestors.scala`:
```scala
// EXPECT: true true true true false
@main def run(): Unit =
  val e = new ArithmeticException("x")
  println(e.isInstanceOf[ArithmeticException].toString + " " + e.isInstanceOf[RuntimeException] + " " +
    e.isInstanceOf[Exception] + " " + e.isInstanceOf[Throwable] + " " + e.isInstanceOf[Error])
```

Verify `native-arithmetic`, `native-index`, `native-number-format`, `native-no-such-element`, `native-match-error`, `error-is-not-an-exception` and `hierarchy-ancestors` against scalac. `native-arithmetic`'s message is scalac's `/ by zero`, so check what protoScala's `prim_int_div` says today and make the two agree or record the difference under D72. `native-stack-overflow-is-catchable` depends on the stack guard's own message; write the fixture, run it, and set the `// EXPECT:` line from the binary.

- [ ] **Step 2: Write the hierarchy in the prelude**

Add to `lib/prelude.scala`, **before** the `Try` block (`Failure` will reference `Throwable`):

```scala
// Phase 4: the exception hierarchy (DESIGN §7). These are ordinary protoScala
// classes, so `case e: ArithmeticException` is the same per-class marker test as
// `case p: Point` (DESIGN §5.3), and the runtime materialises a native failure
// into one of them by unqualified name (plan A0-6). The names are the JVM's
// without the `java.lang.` prefix (D72); there is no `java` namespace (D8).
class Throwable(message: String):
  def getMessage: String = message
  def getCause: Throwable = null
  override def toString: String =
    if message == null then getClass else getClass + ": " + message
  def getClass: String = productPrefixOf(this)

class Exception(message: String) extends Throwable(message)
class Error(message: String) extends Throwable(message)

class RuntimeException(message: String) extends Exception(message)
class ArithmeticException(message: String) extends RuntimeException(message)
class ClassCastException(message: String) extends RuntimeException(message)
class IllegalArgumentException(message: String) extends RuntimeException(message)
class IllegalStateException(message: String) extends RuntimeException(message)
class IndexOutOfBoundsException(message: String) extends RuntimeException(message)
class StringIndexOutOfBoundsException(message: String) extends IndexOutOfBoundsException(message)
class NoSuchElementException(message: String) extends RuntimeException(message)
class NoSuchMethodError(message: String) extends Error(message)
class NullPointerException(message: String) extends RuntimeException(message)
class NumberFormatException(message: String) extends IllegalArgumentException(message)
class UnsupportedOperationException(message: String) extends RuntimeException(message)
class UninitializedFieldError(message: String) extends Error(message)
class MatchError(message: String) extends RuntimeException(message)
class InterruptedException(message: String) extends Exception(message)
class StackOverflowError(message: String) extends Error(message)
class OutOfMemoryError(message: String) extends Error(message)

// Constructors the runtime materialises native failures with (plan A0-6). A
// native cannot name a global directly (a REPL redefinition gives `Some#1`,
// D25), so bindPreludeHooks resolves these once after the prelude is compiled.
def __throwableClasses: List[Any] =
  ("Throwable" :: ((s: String) => new Throwable(s)) :: Nil) ::
  ("Exception" :: ((s: String) => new Exception(s)) :: Nil) ::
  ("Error" :: ((s: String) => new Error(s)) :: Nil) ::
  ("RuntimeException" :: ((s: String) => new RuntimeException(s)) :: Nil) ::
  ("ArithmeticException" :: ((s: String) => new ArithmeticException(s)) :: Nil) ::
  ("ClassCastException" :: ((s: String) => new ClassCastException(s)) :: Nil) ::
  ("IllegalArgumentException" :: ((s: String) => new IllegalArgumentException(s)) :: Nil) ::
  ("IllegalStateException" :: ((s: String) => new IllegalStateException(s)) :: Nil) ::
  ("IndexOutOfBoundsException" :: ((s: String) => new IndexOutOfBoundsException(s)) :: Nil) ::
  ("StringIndexOutOfBoundsException" :: ((s: String) => new StringIndexOutOfBoundsException(s)) :: Nil) ::
  ("NoSuchElementException" :: ((s: String) => new NoSuchElementException(s)) :: Nil) ::
  ("NoSuchMethodError" :: ((s: String) => new NoSuchMethodError(s)) :: Nil) ::
  ("NullPointerException" :: ((s: String) => new NullPointerException(s)) :: Nil) ::
  ("NumberFormatException" :: ((s: String) => new NumberFormatException(s)) :: Nil) ::
  ("UnsupportedOperationException" :: ((s: String) => new UnsupportedOperationException(s)) :: Nil) ::
  ("UninitializedFieldError" :: ((s: String) => new UninitializedFieldError(s)) :: Nil) ::
  ("MatchError" :: ((s: String) => new MatchError(s)) :: Nil) ::
  ("InterruptedException" :: ((s: String) => new InterruptedException(s)) :: Nil) ::
  ("StackOverflowError" :: ((s: String) => new StackOverflowError(s)) :: Nil) ::
  ("OutOfMemoryError" :: ((s: String) => new OutOfMemoryError(s)) :: Nil) :: Nil
```

`__throwableClasses` returns a list of two-element `List`s, **never `ProtoTuple`s** and, deliberately, not `Tuple2`s either: `bindPreludeHooks` reads it as raw `ProtoList`s before any hook exists to build a tuple with. Each entry's second element is a one-argument function, so `materialiseError` calls it with `invoke` rather than needing a class prototype and a constructor key.

`productPrefixOf(this)` is a native global added in Step 3 that returns an instance's `__name__`; `getClass` returning a `String` rather than a class object is recorded as **D85**: `Throwable.getClass` returns the class's simple name as a `String`, because protoScala has no `Class[_]` values.

Also change `Failure` and `Try` to carry a `Throwable` and delete `RuntimeError`:

```scala
final case class Failure[+A](exception: Throwable) extends Try[A]:
  def isSuccess: Boolean = false
  def get: A = throw exception
  def map[B](f: A => B): Try[B] = Failure(exception)
  def flatMap[B](f: A => Try[B]): Try[B] = Failure(exception)
  def foreach[U](f: A => U): Unit = ()
  def recover[B >: A](f: Throwable => B): Try[B] = __tryOf(() => f(exception))
  def recoverWith[B >: A](f: Throwable => Try[B]): Try[B] = f(exception)
  def toEither: Either[Throwable, A] = Left(exception)
```

`Failure.get` is now a real `throw`, which removes the `__raise` stand-in from that path. Delete `case class RuntimeError`, `__mkRuntimeError` and `None.get`'s `__raise` call (replace with `throw new NoSuchElementException("None.get")`), and keep `__raise` itself only if a native still needs it — `grep -n '__raise' src/` and remove it from `builtinGlobalNames()` if nothing does. This **retires D44** (A0-16).

- [ ] **Step 3: Resolve the hooks and remove Task 1's guard**

In `src/runtime/Primitives.cpp`'s `bindPreludeHooks`, read `__throwableClasses` and fill the map:

```cpp
    // Plan A0-6: the exception classes materialiseError builds. Each entry of
    // __throwableClasses is a two-element ProtoList [name, factory]; the factory
    // is a one-argument function, so no constructor key is needed.
    {
        const proto::ProtoObject* fn =
            layout.globals->getOwnAttributeDirect(ctx, symbolFor(ctx, globals, "__throwableClasses"));
        if (!fn) throw std::logic_error("prelude: __throwableClasses is missing");
        const proto::ProtoObject* list = activeCallContext()->engine->invoke(ctx, fn, nullptr, 0);
        const proto::ProtoList* entries = list->asList(ctx);
        const auto n = static_cast<long long>(entries->getSize(ctx));
        for (long long i = 0; i < n; ++i) {
            const proto::ProtoList* e = entries->getAt(ctx, static_cast<int>(i))->asList(ctx);
            const std::string name =
                reinterpret_cast<const proto::ProtoString*>(e->getAt(ctx, 0))->toStdString(ctx);
            layout.hooks.throwableClasses[name] = e->getAt(ctx, 1);
        }
        auto it = layout.hooks.throwableClasses.find("RuntimeException");
        if (it == layout.hooks.throwableClasses.end())
            throw std::logic_error("prelude: RuntimeException is missing");
        layout.hooks.runtimeException = it->second;
    }
```

Then rewrite `materialiseError` (Task 1 Step 5) to call the factory with `invoke` rather than `construct`, and delete the `if (!L.hooks.bound || …) throw;` guard:

```cpp
const proto::ProtoObject* ExecutionEngine::materialiseError(proto::ProtoContext* ctx,
                                                            const ScalaError& e) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const proto::ProtoObject** slot = scope.getAutomaticLocals();
    auto it = L.hooks.throwableClasses.find(e.className());
    const proto::ProtoObject* factory = it != L.hooks.throwableClasses.end()
                                            ? it->second
                                            : L.hooks.runtimeException;
    slot[0] = makeString(&scope, it != L.hooks.throwableClasses.end()
                                     ? e.message()
                                     : e.className() + ": " + e.message());
    const proto::ProtoObject* r = invoke(&scope, factory, slot, 1);
    scope.returnValue = r;
    return r;
}
```

Also add `productPrefixOf` as a native global (`{"productPrefixOf", &prim_product_prefix_of}` in the globals table plus `"productPrefixOf"` in `builtinGlobalNames()`), returning `self->getAttribute(ctx, L.nameKey)` as a string or `"Object"` when absent.

- [ ] **Step 4: Report an uncaught `ScalaThrow` the way a `ScalaError` is reported**

`Session::evaluate`'s run block currently catches `const ScalaError&` (`Session.cpp:96-104`). Add a `catch (const ScalaThrow& t)` clause **before** it that prints the same shape, using the value's own `toString`:

```cpp
    } catch (const ScalaThrow& t) {
        std::fflush(stdout);
        // The value's own toString, so a user-defined exception reports what its
        // class says (design note 7). The engine must be active for that, and it
        // is: we are on the same thread that ran it.
        std::string shown;
        try { shown = engine_.showTopLevel(&ctx, t.value); }
        catch (...) { shown = "<exception whose toString failed>"; }
        std::fprintf(stderr, "%s:%d: error: %s\n", sourceName.c_str(), 0, shown.c_str());
        return EvalStatus::Error;
    } catch (const ScalaError& e) {
```

Do the same at the other four `ScalaError` sites of `Session.cpp` (`:66`, `:86`, `:132`, `:216`) and at `main.cpp:87`, where the outermost handler must distinguish a Scala exception (`protoscala: <shown>`) from an internal error (`protoscala: internal error: <what>`). `ScalaThrow` carries no line number; add an `int line = 0;` field to it that `runLoop`'s `catch (ScalaThrow&)` fills the same way it fills `ScalaError::line`, so the report keeps D14's `file:line: error: …` format.

- [ ] **Step 5: Extend the CLI checks**

In `tests/cli/script-errors.sh`, add a case for an uncaught user exception and one for an uncaught native failure inside a `finally`, asserting the exit code is 1 and the message names the class. In `tests/cli/repl-errors.sh`, add a case that throws at the prompt and confirms the REPL survives and keeps its bindings — that is the check that an exception does not leave the session's globals half-written.

- [ ] **Step 6: Unit-test the translation table**

```cpp
TEST(NativeTranslation, EachSourceMapsToItsScalaClass) {
    EvalHarness h;
    EXPECT_EQ(h.eval("try { 1 / 0; 0 } catch { case e: ArithmeticException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { List(1)(9) } catch { case e: IndexOutOfBoundsException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { \"x\".toInt } catch { case e: NumberFormatException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { None.get } catch { case e: NoSuchElementException => 1 }"), "1");
    EXPECT_EQ(h.eval("try { (5: Any).asInstanceOf[List[Int]].head } "
                     "catch { case e: Throwable => 1 }"), "1");
}

TEST(NativeTranslation, AnUnknownClassNameFallsBackToRuntimeException) {
    EvalHarness h;
    // A native that throws a class the prelude does not define must still be
    // catchable, with its original name preserved in the message (plan A0-6).
    EXPECT_EQ(h.eval("try { __raiseUnknownForTest() } "
                     "catch { case e: RuntimeException => e.getMessage }"),
              "SomeUnknownError: for the test");
}

TEST(NativeTranslation, AVmDefectIsNotCatchable) {
    EvalHarness h;
    // A std::logic_error is a compiler or VM bug and must escape every Scala
    // catch, so a defect can never be masked by `case e: Throwable` (D73).
    EXPECT_THROW(h.eval("try { __logicErrorForTest() } catch { case e: Throwable => 1 }"),
                 std::logic_error);
}
```

`__raiseUnknownForTest` and `__logicErrorForTest` are two natives added **only** under `#ifdef PROTOSCALA_TESTING`, which `tests/unit/CMakeLists.txt` defines for the unit binaries and the release binary does not. Say so at their definitions.

**Done when:** `ctest --test-dir build_release -R '20-exceptions|NativeTranslation' --output-on-failure` passes the nine fixtures of Step 1 and the three unit cases; the seven scalac-verifiable fixtures match byte for byte; `bash tests/cli/script-errors.sh build_release/protoscala tests` and `bash tests/cli/repl-errors.sh build_release/protoscala tests` print `OK`; `grep -c 'RuntimeError' lib/prelude.scala` returns `0`; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 5: Exceptions across an actor turn and a suspended `await`

**Files:**
- Modify: `src/runtime/ActorScheduler.cpp`, `src/runtime/Futures.h`, `src/runtime/Futures.cpp`, `src/runtime/ActorPrimitives.cpp`, `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`, `lib/prelude.scala`
- Test: `tests/conformance/14-futures/*.scala` (modify and new), `tests/conformance/20-exceptions/await-*.scala` (new), `tests/unit/test_scheduler.cpp` (modify), `tests/CMakeLists.txt` (modify)

**Interfaces:**
- `src/runtime/ExecutionEngine.h`, private:
```cpp
    // Re-enters a resumed frame as if its in-flight call had thrown `payload`:
    // the handler search runs at the pc of that call instruction (ipOffset - 1,
    // the same index runLoop's own catch reports), and then the ordinary retry
    // loop takes over. This is what makes `try { f.await } catch { ... }` work
    // across a cooperative suspension (plan A0-7 invariant 5, retiring D50).
    const proto::ProtoObject* runFrameRaising(proto::ProtoContext& frame, const BytecodeModule& mod,
                                              const proto::ProtoObject** slots,
                                              const proto::ProtoObject** sp, std::size_t ipOffset,
                                              const proto::ProtoObject* payload);
```
- `src/runtime/ExecutionEngine.h`, public: `resumeFrames` gains a last parameter:
```cpp
    // `injected` is the value the suspending await must return; when
    // `injectedThrow` is non-null the await must instead raise it (a failed
    // future). Exactly one of the two is non-null.
    const proto::ProtoObject* resumeFrames(proto::ProtoContext* parent,
                                           const proto::ProtoList* frames, unsigned idx,
                                           const proto::ProtoObject* injected,
                                           const proto::ProtoObject* injectedThrow);
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/20-exceptions/await-inside-try.scala`:
```scala
// EXPECT: 30
// A cooperative suspension is not an exception: the try must not catch it and
// the finally must not run while the frame is suspended (plan A0-7, 1 and 2).
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = 0
    try
      out = (echo ? 10).await + (echo ? 20).await
    catch
      case e: Throwable => out = -1
    (s, out)
  }
  val f = caller ? 1
  println(f.await)
```

`tests/conformance/20-exceptions/finally-does-not-run-on-suspension.scala`:
```scala
// EXPECT: body=1 cleanups=1
// The cleanup runs exactly once, after the resume finishes the body — not when
// the frame suspends (plan A0-7 invariant 2).
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val counter = Actor.spawn(0) { (s, m) =>
    var cleanups = 0
    var body = 0
    try
      body = (echo ? 1).await
    finally
      cleanups += 1
    (s, "body=" + body + " cleanups=" + cleanups)
  }
  println((counter ? 1).await)
```

`tests/conformance/20-exceptions/await-a-failed-future-inside-try.scala`:
```scala
// EXPECT: recovered boom
// A failed future raises AT the await site, so an enclosing try catches it.
// This is what retires D50.
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      out = (failer ? 1).await.toString
    catch
      case e: IllegalStateException => out = "recovered " + e.getMessage
    (s, out)
  }
  println((caller ? 1).await)
```

`tests/conformance/20-exceptions/await-inside-catch.scala`:
```scala
// EXPECT: caught x then 7
// A catch body is ordinary bytecode, so it suspends like any other code, and the
// bound `e` survives the suspension because it lives in a local slot that the
// frame snapshot saves (plan A0-7 invariant 4).
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      throw new RuntimeException("x")
    catch
      case e: RuntimeException =>
        val v = (echo ? 7).await
        out = "caught " + e.getMessage + " then " + v
    (s, out)
  }
  println((caller ? 1).await)
```

`tests/conformance/14-futures/failed-ask-carries-the-exception.scala`:
```scala
// EXPECT: IllegalStateException boom true
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val f = failer ? 1
  while !f.isCompleted do ()
  val t = f.value.get
  println(t match
    case Failure(e) => e.getClass + " " + e.getMessage + " " + t.isFailure
    case Success(v) => "unexpected " + v)
```

`tests/conformance/20-exceptions/thread-body-throws.scala`:
```scala
// EXPECT: main survived
@main def run(): Unit =
  val t = Thread.start(() => throw new RuntimeException("in thread"))
  t.join()
  println("main survived")
```

`tests/conformance/20-exceptions/actor-survives-a-handler-exception.scala`:
```scala
// EXPECT: 2
// A handler exception fails that message and leaves the actor alive with its
// previous state (DESIGN §8.4), unchanged from Phase 5 but now with a real
// exception value.
@main def run(): Unit =
  val a = Actor.spawn(0) { (s, m) =>
    if m == 0 then throw new RuntimeException("bad") else (s + 1, s + 1)
  }
  val bad = a ? 0
  while !bad.isCompleted do ()
  println((a ? 1).await.toString + (a ? 1).await.toString)
```

Every one of these starts an actor, so extend the `tests/CMakeLists.txt` block that sets `TIMEOUT 120` on `13-actors/` and `14-futures/` to also match `20-exceptions/await-`, `20-exceptions/finally-does-not-run-`, `20-exceptions/thread-body-` and `20-exceptions/actor-survives-`. Pin `PROTOSCALA_ACTOR_WORKERS=1` on `await-inside-try`, `await-inside-catch` and `await-a-failed-future-inside-try`: one worker is what proves the suspension is cooperative rather than blocking, exactly as Phase 5's `await-one-worker.scala` does.

- [ ] **Step 2: Point `resumeFrames` at `runFrame` and add the raising path**

The `runLoop` → `runFrame` change at `ExecutionEngine.cpp:548` was made in Task 1 Step 6; this step adds the raise. Change `resumeFrames`'s recursion and tail:

```cpp
    // The inner frames finish first; their result is what this frame's in-flight
    // call would have returned. The innermost frame receives the awaited value
    // itself — or, when the future failed, raises it at the await's call site so
    // an enclosing try in the suspended handler can catch it (plan A0-7, 5).
    if (idx + 1 < frames->getSize(&frame)) {
        const proto::ProtoObject* r =
            resumeFrames(&frame, frames, idx + 1, injected, injectedThrow);
        slots[base] = r;
        const proto::ProtoObject* out =
            runFrame(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset);
        frame.returnValue = out;
        return out;
    }
    if (injectedThrow) {
        const proto::ProtoObject* out =
            runFrameRaising(frame, mod, slots, slots + base, ipOffset, injectedThrow);
        frame.returnValue = out;
        return out;
    }
    slots[base] = injected;
    const proto::ProtoObject* out =
        runFrame(frame, mod, slots, slots + base + 1, mod.code().data() + ipOffset);
    frame.returnValue = out;
    return out;
```

and add:

```cpp
const proto::ProtoObject* ExecutionEngine::runFrameRaising(proto::ProtoContext& frame,
                                                           const BytecodeModule& mod,
                                                           const proto::ProtoObject** slots,
                                                           const proto::ProtoObject** sp,
                                                           std::size_t ipOffset,
                                                           const proto::ProtoObject* payload) {
    // ipOffset is where the snapshot said to continue, i.e. just past the call
    // instruction that was in flight; that instruction's word index is
    // ipOffset - 1, which is the index runLoop's own catch reports (A0-3).
    frame.returnValue = payload;
    const BytecodeModule::Handler* h = mod.handlerFor(ipOffset - 1);
    if (!h) throw ScalaThrow(payload);
    const Instr* ip = nullptr;
    const proto::ProtoObject** newSp = enterHandler(frame, mod, slots, *h, payload, &ip);
    (void)sp;
    return runFrame(frame, mod, slots, newSp, ip);
}
```

- [ ] **Step 3: Make a failed future resume by raising**

In `src/runtime/Futures.cpp`, the completion path that re-enqueues suspended actor waiters currently passes the value. Give the waiter record a raise flag: when the future completes with a failure, the waiter is re-enqueued with the **exception value** rather than the result, and `ActorScheduler`'s resume path calls `resumeFrames(ctx, frames, 0, /*injected=*/nullptr, /*injectedThrow=*/payload)`. Where the future completed successfully it calls `resumeFrames(ctx, frames, 0, value, /*injectedThrow=*/nullptr)`. Every existing call site of `resumeFrames` gains the fifth argument; `grep -n 'resumeFrames' src/` finds them all and there are three.

In `src/runtime/ActorPrimitives.cpp`, `await`'s **blocking** path (outside an actor) currently raises the carried `RuntimeError` as a `ScalaError`. Change it to `throw ScalaThrow(payload)` where `payload` is the `Throwable` the future carries, so a blocking `await` is catchable by the same `catch` clauses.

- [ ] **Step 4: Fix the actor and thread catch chains**

In `src/runtime/ActorScheduler.cpp`'s `deliver`, the catch chain becomes, in this exact order:

```cpp
    } catch (FutureYield&) {
        // A cooperative suspension: Phase 5's path, untouched. It must stay
        // FIRST, because a Scala catch must never see it (plan A0-7 invariant 1).
        throw;
    } catch (ScalaThrow& t) {
        // DESIGN §8.4: the ask's future fails with the exception value, the
        // error is reported, the actor keeps its previous state and stays alive.
        call.returnValue = t.value;                    // rooted before completeFailure allocates
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n",
                     engine_->showTopLevel(&call, t.value).c_str());
        completeFailureWith(&call, envelope, t.value);
    } catch (ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: actor handler failed: %s\n", e.what());
        completeFailure(&call, envelope, e);
    }
```

`completeFailureWith(ctx, envelope, throwable)` is a new overload that wraps an existing `Throwable` in `Failure`; `completeFailure(ctx, envelope, ScalaError&)` keeps its Phase 5 shape but now materialises the error into a `Throwable` first (call `engine_->materialiseError`, which becomes a public member for this, or add a small `ExecutionEngine::materialise(ctx, e)` public wrapper).

Give `Thread.start`'s thread body (`ActorPrimitives.cpp`) the same three-clause chain, minus the `FutureYield` case, which cannot occur outside an actor turn: report on stderr and return, never terminate the process.

- [ ] **Step 5: Retire D52 — `Priority` becomes an `enum`**

Once Task 8 has landed `enum`, replace the `Priority` object in the globals with a prelude `enum`:

```scala
// Phase 4: a real enum, retiring D52. The ordinals are 0/1/2, which is exactly
// the band index the scheduler already uses, so nothing in ActorScheduler
// changes.
enum Priority:
  case High, Medium, Low
```

and change the `send`/`ask` natives to accept either a `Priority` case (reading `ordinal`) or a plain `Int` (for compatibility with programs written against D52). **Order this step after Task 8**; if Task 8 is deferred, skip it and leave D52 open, saying so in the task notes.

- [ ] **Step 6: Unit tests**

In `tests/unit/test_scheduler.cpp` (the dedicated `test_actors` binary — one runtime and one scheduler per process, R5):

```cpp
TEST(Engine, AFutureYieldIsNotCaughtByAScalaCatch) {
    // FutureYield does not derive from std::exception, and runLoop's catch for it
    // is first, so a `try { f.await } catch { case e: Throwable => }` inside an
    // actor suspends rather than catching. If this ever fails, the catch order in
    // runLoop was changed and cooperative await is broken (plan A0-7 invariant 1).
    static_assert(!std::is_base_of_v<std::exception, FutureYield>);
}

TEST(Scheduler, AHandlerExceptionCompletesTheAskWithTheExceptionValue) {
    ActorTestHarness h;
    const std::string out = h.eval(
        "val a = Actor.spawn(0) { (s, m) => throw new IllegalStateException(\"boom\") }\n"
        "val f = a ? 1\n"
        "while !f.isCompleted do ()\n"
        "f.value.get match { case Failure(e) => e.getMessage; case Success(v) => \"?\" }");
    EXPECT_EQ(out, "boom");
}
```

**Done when:** `ctest --test-dir build_release -R '20-exceptions/await|20-exceptions/finally-does-not|20-exceptions/thread-body|20-exceptions/actor-survives|14-futures' --output-on-failure` passes all seven new fixtures and every existing Phase 5 fixture; `PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release --output-on-failure` and `=16` are both fully green; `bash tests/cli/actors-shutdown.sh build_release/protoscala tests` prints `OK`; `bash tests/cli/actors-stress.sh build_release/protoscala tests` prints `OK`; and `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release` is green. **This task is not done until all four actor configurations pass** — the whole point of A0-7 is that getting this wrong breaks Phase 5 silently.

---

### Task 6: `super[T].m`

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/Parser.cpp`, `src/compiler/BytecodeModule.h`, `src/compiler/BytecodeModule.cpp`, `src/compiler/CompileTemplates.cpp`, `src/runtime/ExecutionEngine.cpp`
- Test: `tests/conformance/22-super-and-extensions/super-*.scala` (new), `tests/unit/test_objectmodel.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `NodeKind` gains `Super`;
```cpp
// super.m or super[T].m. `qualifier` empty: plain super (the next entry in the
// linearization after the defining class). Non-empty: the named ancestor (A0-8).
struct Super : Node {
    explicit Super(SourcePos p) : Node(NodeKind::Super, p) {}
    std::string qualifier;
};
```
- `BytecodeModule::Const` gains `bool exact = false;` and `addSuperSite` gains a parameter:
```cpp
    // `exact`: the site wrote super[T].m, so the search starts AT the named
    // ancestor rather than after the defining class (plan A0-8).
    std::size_t addSuperSite(const std::string& name, std::uint32_t argc,
                             const std::string& ownerKey, bool exact = false);
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/22-super-and-extensions/super-qualified.scala`:
```scala
// EXPECT: 1 2 3
trait A:
  def m: Int = 1
trait B extends A:
  override def m: Int = 2
class C extends B:
  override def m: Int = 3
  def fromA: Int = super[A].m
  def fromB: Int = super[B].m
@main def run(): Unit =
  val c = new C()
  println(c.fromA.toString + " " + c.fromB + " " + c.m)
```

`tests/conformance/22-super-and-extensions/super-qualified-braces.scala` is the same program in brace syntax.

`tests/conformance/22-super-and-extensions/super-qualified-finds-an-inherited-member.scala`:
```scala
// EXPECT: 1
// B does not define m, so super[B].m must find the m B inherits from A
// (plan A0-8 option (a): probe T itself, then continue after T).
trait A:
  def m: Int = 1
trait B extends A
class C extends B:
  override def m: Int = 9
  def viaB: Int = super[B].m
@main def run(): Unit = println(new C().viaB)
```

`tests/conformance/22-super-and-extensions/super-qualified-with-arguments.scala`:
```scala
// EXPECT: 11 21
trait A:
  def f(n: Int): Int = n + 1
trait B extends A:
  override def f(n: Int): Int = n + 2
class C extends B:
  def viaA(n: Int): Int = super[A].f(n)
  def viaB(n: Int): Int = super[B].f(n)
@main def run(): Unit =
  val c = new C()
  println(c.viaA(10).toString + " " + c.viaB(19))
```

`tests/conformance/22-super-and-extensions/super-qualified-unrelated-type.scala`:
```scala
// EXPECT-ERROR: is not in the linearization of
trait A:
  def m: Int = 1
trait Unrelated:
  def m: Int = 0
class C extends A:
  def bad: Int = super[Unrelated].m
@main def run(): Unit = println(new C().bad)
```

`tests/conformance/22-super-and-extensions/super-plain-still-works.scala`:
```scala
// EXPECT: 6
// Phase 2's stackable-traits behaviour is untouched.
trait Doubling:
  def put(n: Int): Int = n * 2
trait Incrementing extends Doubling:
  override def put(n: Int): Int = super.put(n + 1)
class Q extends Incrementing
@main def run(): Unit = println(new Q().put(2))
```

Verify `super-qualified`, `super-qualified-finds-an-inherited-member`, `super-qualified-with-arguments` and `super-plain-still-works` against scalac. `super-qualified-unrelated-type` is a **compile** error in scalac and a run-time error here; say so in the fixture's comment (D4) and pin protoScala's message.

- [ ] **Step 2: Parse `super[T]`**

Replace `Parser.cpp:566-570`:

```cpp
        case TokenKind::KwSuper: {
            advance();
            auto s = std::make_unique<Super>(t.pos);
            if (at(TokenKind::LBracket)) {
                advance();
                s->qualifier = expect(TokenKind::Identifier, "an ancestor type name").text;
                expect(TokenKind::RBracket, "']'");
            }
            if (!at(TokenKind::Dot)) fail("'.' expected after 'super'", peek());
            base = std::move(s);                 // the compiler checks the context
            break;
        }
```

A dedicated `Super` node replaces the `Ident("super")` Phase 2 used, so the qualifier has somewhere to live. Every site that tested `Ident` with the name `"super"` must move to `NodeKind::Super`: `grep -n '"super"' src/` finds them in `Compiler.cpp` and `CompileTemplates.cpp`.

- [ ] **Step 3: Compile the exact site**

In `src/compiler/CompileTemplates.cpp`'s `compileSuperSend`, resolve the qualifier when there is one:

```cpp
// super.m resolves relative to the method's defining class; super[T].m names T
// explicitly. In both cases the site carries a type key and the runtime walks
// the receiver's linearization (DESIGN §4.4, plan A0-8).
void Compiler::compileSuperSend(const std::string& name, const std::vector<NodePtr>& args,
                                const std::string& qualifier, SourcePos pos) {
    if (currentClassKey_.empty())
        throw CompileError("'super' is only allowed inside a class, trait or object body", pos);
    std::string ownerKey = currentClassKey_;
    bool exact = false;
    if (!qualifier.empty()) {
        const ClassInfo* t = globals_.findType(qualifier);
        if (!t) throw CompileError("super[" + qualifier + "]: not a protoScala type", pos);
        ownerKey = t->key;
        exact = true;
    }
    loadThis(pos);
    for (const NodePtr& a : args) compileExpr(*a);
    const std::size_t site = fn_->mod->addSuperSite(
        name, static_cast<std::uint32_t>(args.size()), ownerKey, exact);
    emit(Op::SEND_SUPER, site, pos, -static_cast<int>(args.size()));
}
```

`BytecodeModule::addSuperSite` keys its de-duplication index on `name + "/" + argc + "/" + ownerKey + (exact ? "/x" : "")` so an exact and a plain site on the same owner do not collide.

- [ ] **Step 4: Implement the runtime search**

In `src/runtime/ExecutionEngine.cpp`'s `superSend`, replace the search with:

```cpp
    unsigned long k = 0;
    while (k < n && chain->getAt(ctx, static_cast<int>(k)) != owner) ++k;
    if (k == n)
        throw ScalaError("NoSuchMethodError",
                         ownerName + " is not in the linearization of " +
                             typeName(ctx, L, base[0]) + ", so super" +
                             (site.exact ? "[" + ownerName + "]" : "") + "." + site.sval +
                             " has no meaning here");
    // super[T].m starts AT T, so T's own definition is the one the programmer
    // named; plain super.m starts after the defining class (plan A0-8).
    if (!site.exact) ++k;
    for (; k < n; ++k) {
        const proto::ProtoObject* m =
            chain->getAt(ctx, static_cast<int>(k))->getOwnAttributeDirect(ctx, site.symbol);
        if (m) return callMember(ctx, m, base, site.argc);
    }
    throw ScalaError("NoSuchMethodError",
                     "super" + (site.exact ? "[" + ownerName + "]" : "") + "." + site.sval +
                         " has no implementation after " + ownerName);
```

`super[T].m` inside `T`'s own method would find `T`'s own `m` and recurse forever; reject it at compile time in Step 3 with `super[T].m inside T itself would call itself` when `ownerKey == currentClassKey_ && exact`. Add a fixture for it.

- [ ] **Step 5: Unit test**

```cpp
TEST(ObjectModel, QualifiedSuperProbesTheNamedAncestorFirst) {
    EvalHarness h;
    h.eval("trait A { def m: Int = 1 }");
    h.eval("trait B extends A { override def m: Int = 2 }");
    h.eval("class C extends B { override def m: Int = 3; "
           "def a: Int = super[A].m; def b: Int = super[B].m }");
    EXPECT_EQ(h.eval("new C().a"), "1");
    EXPECT_EQ(h.eval("new C().b"), "2");
    EXPECT_EQ(h.eval("new C().m"), "3");
}

TEST(ObjectModel, QualifiedSuperOnAnUnrelatedTypeFails) {
    EvalHarness h;
    h.eval("trait A { def m: Int = 1 }");
    h.eval("trait U { def m: Int = 0 }");
    h.eval("class C extends A { def bad: Int = super[U].m }");
    EXPECT_NE(h.eval("new C().bad").find("is not in the linearization of"), std::string::npos);
}
```

**Done when:** `ctest --test-dir build_release -R '22-super-and-extensions/super|ObjectModel' --output-on-failure` passes all seven fixtures and both unit cases; the four scalac-verifiable fixtures match byte for byte; Phase 2's `tests/conformance/07-classes/stackable-traits.scala` still passes unchanged; and the full suite is green.

---

### Task 7: Templates nested in an `object`, and multiple constructor parameter lists

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/Parser.cpp`, `src/frontend/Desugar.cpp`, `src/compiler/ClassInfo.h`, `src/compiler/Compiler.cpp`, `src/compiler/CompileTemplates.cpp`
- Test: `tests/conformance/03-definitions/nested-*.scala`, `ctor-param-lists*.scala` (new), `tests/unit/test_desugar.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `TemplateDef` gains `std::string enclosing;` — the dotted path of the enclosing objects (`""` at the top level, `"O"`, `"O.P"`).
- `src/compiler/ClassInfo.h`: `ClassInfo` gains `std::string enclosingKey;` — the type key of the enclosing object's class, or empty.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/03-definitions/nested-class-in-object.scala`:
```scala
// EXPECT: 7 O.C
object O:
  class C(val n: Int):
    override def toString: String = "O.C"
@main def run(): Unit =
  val c = new O.C(7)
  println(c.n.toString + " " + c)
```

`tests/conformance/03-definitions/nested-class-in-object-braces.scala` is the same program in brace syntax.

`tests/conformance/03-definitions/nested-case-class-in-object.scala`:
```scala
// EXPECT: P(1,2) true
object Geometry:
  case class P(x: Int, y: Int)
@main def run(): Unit =
  val p = Geometry.P(1, 2)
  println(p.toString + " " + (p == Geometry.P(1, 2)))
```

`tests/conformance/03-definitions/nested-object-in-object.scala`:
```scala
// EXPECT: 5
object Outer:
  object Inner:
    val v: Int = 5
@main def run(): Unit = println(Outer.Inner.v)
```

`tests/conformance/03-definitions/nested-trait-and-match.scala`:
```scala
// EXPECT: leaf 3
object Ast:
  sealed trait T
  case class Leaf(n: Int) extends T
  case class Node(l: T, r: T) extends T
@main def run(): Unit =
  val t: Ast.T = Ast.Leaf(3)
  println(t match
    case Ast.Leaf(n) => "leaf " + n
    case Ast.Node(_, _) => "node")
```

`tests/conformance/03-definitions/nested-class-in-class-is-rejected.scala`:
```scala
// EXPECT-ERROR: must be defined at the top level of a file or in an object
class Outer:
  class Inner(val n: Int)
@main def run(): Unit = println(1)
```

`tests/conformance/03-definitions/ctor-param-lists.scala`:
```scala
// EXPECT: 3 3
// Multiple constructor parameter lists concatenate into one flat list (D83), so
// both spellings are the same call.
class C(val a: Int)(val b: Int)
@main def run(): Unit =
  println((new C(1)(2)).a + (new C(1)(2)).b)
  println((new C(1, 2)).a + (new C(1, 2)).b)
```

Verify `nested-class-in-object`, `nested-case-class-in-object`, `nested-object-in-object` and `nested-trait-and-match` against scalac. `ctor-param-lists`' second line is protoScala-only (scalac rejects `new C(1, 2)`), so the fixture records that under D83.

- [ ] **Step 2: Parse nested templates and repeated parameter clauses**

In `Parser::parseTemplateStat` (`:1310`), stop rejecting a nested `TemplateDef` and record the enclosing path instead: `parseTemplateBody` gains a `const std::string& path` parameter, and a nested `TemplateDef` gets `enclosing = path`. In `parseTemplateDef`, the path passed down is `path.empty() ? name : path + "." + name` — **only when the enclosing template is an `object`**; for a `class` or `trait` the nested template is still rejected, with the new message `classes, traits and objects must be defined at the top level of a file or in an object` (D79).

In `parseClassParamClause` (`:1170`), loop while the next token is `(`, appending each clause's parameters to one `ctorParams` vector, and record `ctorParamListSizes` on the `TemplateDef` for diagnostics only.

- [ ] **Step 3: Lift nested templates in `Desugar`**

In `Desugarer::stats`, when a `TemplateDef` has a non-empty `enclosing`, rename it to `enclosing + "." + name` and **move it to the top level of the compilation unit**, before the enclosing object's own definition (so the object's body can reference it). Its `body` keeps whatever it had; references to it inside the enclosing object body are rewritten from `C` to `O.C` by name resolution, not by the desugarer — the compiler's type namespace does that in Step 4.

- [ ] **Step 4: Resolve `O.C` in the compiler's type namespace**

`GlobalTable::declareType("O.C")` already returns `"@O.C"` with no change (`.` cannot occur in a Scala identifier, so there is no collision — design note 10). What needs work is resolution:

- a `Select(Ident("O"), "C")` in a **type** position (a parent reference, a type pattern, an `isInstanceOf`) resolves to `findType("O.C")`;
- a `Select(Ident("O"), "C")` in a **term** position resolves to the class's companion (a case class) or, for a nested `object`, to its lazy holder;
- inside `object O`'s own body, a bare `C` resolves to `O.C` first and then to a top-level `C`, which is Scala's scoping.

Implement the third as a lookup list on `FunctionState`/the current template: try `enclosingPath + "." + name` for each enclosing path from innermost outward, then the bare name.

- [ ] **Step 5: Unit-test the lifting**

```cpp
TEST(Desugar, ANestedTemplateIsLiftedWithAQualifiedName) {
    const CompilationUnit u = desugarSource("object O:\n  class C(val n: Int)\n");
    // Two top-level templates: O.C (lifted, first) and O.
    ASSERT_GE(u.stats.size(), 2u);
    const auto& first = as<TemplateDef>(*u.stats[0]);
    EXPECT_EQ(first.name, "O.C");
    const auto& second = as<TemplateDef>(*u.stats[1]);
    EXPECT_EQ(second.name, "O");
}

TEST(Desugar, ANestedTemplateInAClassIsStillRejected) {
    EXPECT_THROW(desugarSource("class Outer:\n  class Inner(val n: Int)\n"), ParseError);
}
```

**Done when:** `ctest --test-dir build_release -R '03-definitions|Desugar' --output-on-failure` passes all eight fixtures and both unit cases; the four scalac-verifiable fixtures match byte for byte; the old message `classes, traits and objects must be defined at the top level of a file` no longer appears in `src/` (`grep -rn 'at the top level of a file"' src/` returns nothing); and the full suite is green.

---

### Task 8: `enum` and sealed hierarchies

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`, `src/frontend/Desugar.cpp`, `src/compiler/ClassInfo.h`, `src/compiler/ClassInfo.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Primitives.cpp`
- Test: `tests/conformance/21-enums/*.scala` (new), `tests/unit/test_desugar.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `TemplateDef::TemplateKind` gains `Enum`, and a new field:
```cpp
    // enum only: the cases, in declaration order. A case with no parameters and
    // no parent arguments becomes a `case object`; anything else a `case class`.
    struct EnumCase {
        std::string name;
        std::vector<Param> params;          // empty: a singleton case
        std::vector<NodePtr> parentArgs;    // `case Red extends Color(0xFF0000)`
        SourcePos pos;
    };
    std::vector<EnumCase> enumCases;
```
- `src/compiler/ClassInfo.h` gains `inline constexpr const char* kEnumKey = "@Enum";`.
- `RuntimeLayout` gains `proto::ProtoObject* enumProto = nullptr;`, `const proto::ProtoString* ordinalKey = nullptr; // "__ordinal__"`, `const proto::ProtoString* enumNameKey = nullptr; // "__enumname__"`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/21-enums/enum-simple.scala`:
```scala
// EXPECT: Red 0 Blue 2 List(Red, Green, Blue) 3
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  println(Color.Red.toString + " " + Color.Red.ordinal + " " + Color.Blue + " " +
    Color.Blue.ordinal + " " + Color.values + " " + Color.values.length)
```

`tests/conformance/21-enums/enum-simple-braces.scala` is the same program in brace syntax.

`tests/conformance/21-enums/enum-match.scala`:
```scala
// EXPECT: warm cool cool
enum Color:
  case Red, Green, Blue
def describe(c: Color): String = c match
  case Color.Red => "warm"
  case Color.Green => "cool"
  case Color.Blue => "cool"
@main def run(): Unit =
  println(describe(Color.Red) + " " + describe(Color.Green) + " " + describe(Color.Blue))
```

`tests/conformance/21-enums/enum-valueof-and-fromordinal.scala`:
```scala
// EXPECT: Green Blue caught
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  print(Color.valueOf("Green").toString + " " + Color.fromOrdinal(2) + " ")
  try println(Color.valueOf("Purple"))
  catch case e: IllegalArgumentException => println("caught")
```

`tests/conformance/21-enums/enum-with-parameters.scala`:
```scala
// EXPECT: 16711680 255 2
enum Color(val rgb: Int):
  case Red extends Color(0xFF0000)
  case Blue extends Color(0x0000FF)
@main def run(): Unit =
  println(Color.Red.rgb.toString + " " + Color.Blue.rgb + " " + Color.values.length)
```

`tests/conformance/21-enums/enum-adt.scala`:
```scala
// EXPECT: 6
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
def sum(t: Tree): Int = t match
  case Tree.Leaf(n) => n
  case Tree.Node(l, r) => sum(l) + sum(r)
@main def run(): Unit =
  println(sum(Tree.Node(Tree.Leaf(1), Tree.Node(Tree.Leaf(2), Tree.Leaf(3)))))
```

`tests/conformance/21-enums/enum-with-methods.scala`:
```scala
// EXPECT: RED true
enum Color:
  case Red, Green
  def shout: String = toString.toUpperCase
@main def run(): Unit =
  println(Color.Red.shout + " " + (Color.Red == Color.Red))
```

`tests/conformance/21-enums/enum-cases-are-also-top-level.scala`:
```scala
// EXPECT: Red true
// D77: an enum case resolves both as Color.Red and as Red, where Scala requires
// the qualified form unless it is imported.
enum Color:
  case Red, Green
@main def run(): Unit =
  println(Red.toString + " " + (Red == Color.Red))
```

`tests/conformance/21-enums/sealed-trait-exhaustiveness-is-not-checked.scala`:
```scala
// EXPECT-ERROR: MatchError
// D4: types are erased, so a non-exhaustive match compiles and fails at run time.
sealed trait Shape
case class Circle(r: Int) extends Shape
case class Square(s: Int) extends Shape
@main def run(): Unit =
  val x: Shape = Square(2)
  println(x match { case Circle(r) => r })
```

Verify `enum-simple`, `enum-match`, `enum-valueof-and-fromordinal`, `enum-with-parameters`, `enum-adt` and `enum-with-methods` against scalac. `Color.values` prints `List(Red, Green, Blue)` here and `Vector(Red, Green, Blue)`-ish (an `Array`, so `[LColor;@…`) in scalac when printed directly — that is D76 and the fixture records it.

- [ ] **Step 2: Parse `enum`**

Replace `Parser.cpp:894-896`'s `KwEnum` rejection with `case TokenKind::KwEnum: return parseEnumDef(mods);` and write `parseEnumDef`, which reads `enum Name[TypeParams](Params) extends Parents:` then a body of `case` clauses and ordinary members. A `case` clause in an `enum` body is:

```
EnumCase ::= 'case' id {',' id}                              -- singletons
           | 'case' id '(' Params ')' ['extends' ParentArgs]  -- a case class
           | 'case' id 'extends' ParentArgs                   -- a parameterised singleton
```

Anything else in the body is an ordinary `TemplateStat` and goes into `body`. `derives` clauses are parsed and **ignored** with no diagnostic (D78); `enum` type parameters are parsed and erased like every other type.

- [ ] **Step 3: Desugar `enum`**

In `Desugar.cpp`, add `expandEnum(TemplateDef&)`, called from `stats` before `synthesizeCompanions`. It replaces one `TemplateDef` of kind `Enum` with, in order:

1. a `sealed abstract class <Name>` (`TemplateKind::Class`, `isAbstract`, `isSealed`), with the enum's own parameters, parents plus `Enum`, and its non-`case` body members;
2. one template per case, named `<Name>.<CaseName>` with `enclosing = Name` (so Task 7's lifting applies), `isCase = true`, kind `Object` for a singleton or `Class` for a parameterised case, extending `<Name>` with the case's `parentArgs`, and with two synthetic `val`s: `__ordinal__ = <i>` and `__enumname__ = "<CaseName>"`;
3. a companion `object <Name>` carrying `values`, `valueOf` and `fromOrdinal`, generated as ordinary AST:

```scala
object Color:
  def values: List[Color] = Color.Red :: Color.Green :: Color.Blue :: Nil
  def valueOf(name: String): Color =
    if name == "Red" then Color.Red
    else if name == "Green" then Color.Green
    else if name == "Blue" then Color.Blue
    else throw new IllegalArgumentException("enum case not found: " + name)
  def fromOrdinal(n: Int): Color =
    if n == 0 then Color.Red
    else if n == 1 then Color.Green
    else if n == 2 then Color.Blue
    else throw new NoSuchElementException(n.toString)
```

Building that as AST is verbose; build it by **parsing generated source text** with a nested `Parser`, the way Task 2's `parseCatchCases` reuses `parseCaseClauses`. Generate the text, parse it, and splice the resulting `TemplateDef` in. Say so in a comment: it is slower at compile time by microseconds and it is the only way the generated code cannot drift from what a user could have written by hand.

D77 (a case resolves unqualified too) falls out of Task 7's resolution list if the desugarer also declares the bare name as an alias; do that explicitly rather than by accident, with a comment naming D77.

- [ ] **Step 4: Add the `Enum` builtin trait**

In `src/compiler/ClassInfo.cpp`'s `builtinTypes()`, add `Enum` with key `@Enum`, kind `Trait`, `builtin = true`, linearization `{"@Enum", kAnyRefKey, kAnyKey}`. In `Runtime.cpp`, create and pin `enumProto` as a child of `anyRefProto` with the `@Enum` marker and the `__name__` `"Enum"`, and intern `__ordinal__` and `__enumname__`. In `Primitives.cpp`, install on `enumProto`:

```cpp
PRIM(prim_enum_ordinal) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* o = self->getAttribute(ctx, L.ordinalKey);
    if (!o) throw ScalaError("NoSuchMethodError", "ordinal is not a member of " +
                                                     typeName(ctx, L, self));
    return o;
}

PRIM(prim_enum_toString) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* n = self->getAttribute(ctx, L.enumNameKey);
    return n ? n : str(ctx, defaultToString(ctx, L, self));
}
```

A parameterised case is a `case class`, whose synthesised `toString` is `Node(1,2)`; a singleton case's `toString` is its simple name. Both match Scala.

- [ ] **Step 5: Unit-test the expansion**

```cpp
TEST(Desugar, AnEnumExpandsToASealedClassCasesAndACompanion) {
    const CompilationUnit u = desugarSource("enum Color:\n  case Red, Green\n");
    std::vector<std::string> names;
    for (const NodePtr& s : u.stats)
        if (s->kind == NodeKind::TemplateDef) names.push_back(as<TemplateDef>(*s).name);
    EXPECT_NE(std::find(names.begin(), names.end(), "Color"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Color.Red"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Color.Green"), names.end());
}

TEST(Desugar, EnumCasesKeepTheirDeclarationOrdinals) {
    EvalHarness h;
    h.eval("enum Color { case Red, Green, Blue }");
    EXPECT_EQ(h.eval("Color.Red.ordinal"), "0");
    EXPECT_EQ(h.eval("Color.Green.ordinal"), "1");
    EXPECT_EQ(h.eval("Color.Blue.ordinal"), "2");
    EXPECT_EQ(h.eval("Color.values.length"), "3");
}
```

**Done when:** `ctest --test-dir build_release -R '21-enums|Desugar' --output-on-failure` passes all nine fixtures and both unit cases; the six scalac-verifiable fixtures match byte for byte; and the full suite is green. **Task 5 Step 5 can now run**, retiring D52.

---

### Task 9: Named and default arguments for Scala-defined methods

**Files:**
- Modify: `src/compiler/BytecodeModule.h`, `src/compiler/BytecodeModule.cpp`, `src/compiler/Opcodes.h`, `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `src/compiler/CompileTemplates.cpp`, `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`
- Test: `tests/conformance/23-named-arguments/*.scala` (new), `tests/unit/test_compiler.cpp`, `test_engine.cpp` (modify)

**Interfaces:**
- `src/compiler/BytecodeModule.h`, inside `class BytecodeModule`:
```cpp
    // The callee's parameter names, in declaration order, interned by
    // linkSymbols. `defaultBlocks[i]` is the block index of parameter i's
    // default expression, or kNoDefault. The keyword ProtoSparseList a caller
    // builds is keyed by the ADDRESS of paramNameSymbols[i] — protoCore's own
    // convention (plan A0-12).
    static constexpr std::size_t kNoDefault = static_cast<std::size_t>(-1);
    void setParamNames(std::vector<std::string> names);
    const std::vector<std::string>& paramNames() const { return paramNames_; }
    const std::vector<const proto::ProtoString*>& paramNameSymbols() const { return paramSymbols_; }
    void setDefaultBlock(std::size_t param, std::size_t blockIndex);
    std::size_t defaultBlock(std::size_t param) const;
    bool hasDefaults() const { return hasDefaults_; }
```
- `src/compiler/Opcodes.h`: `CALL_KW = 80`.
- `src/runtime/ExecutionEngine.h`, private:
```cpp
    // Binds a keyword ProtoSparseList into a callee frame's parameter slots and
    // fills the rest from the callee's default blocks. `filled` marks the slots
    // positional arguments already wrote. Raises IllegalArgumentException for an
    // unknown name, a duplicate and a missing argument (plan A0-11).
    void bindKeywordsAndDefaults(proto::ProtoContext& frame, const BytecodeModule& mod,
                                 const proto::ProtoSparseList* keywords,
                                 const proto::ProtoObject** slots, unsigned positional);
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/23-named-arguments/named-on-a-def.scala`:
```scala
// EXPECT: 11 11 11
def f(a: Int, b: Int): Int = a + b * 10
@main def run(): Unit =
  println(f(1, 1).toString + " " + f(a = 1, b = 1) + " " + f(b = 1, a = 1))
```

`tests/conformance/23-named-arguments/defaults-on-a-def.scala`:
```scala
// EXPECT: 3 12 21
def f(a: Int = 1, b: Int = 2): Int = a + b * 10 - 9
@main def run(): Unit =
  println(f().toString + " " + f(a = 3) + " " + f(b = 3))
```

Compute the three values with scalac before writing the directive — **do not do the arithmetic in the plan.**

`tests/conformance/23-named-arguments/default-reads-an-earlier-parameter.scala`:
```scala
// EXPECT: 1 2 5 6
def f(a: Int, b: Int = a + 1): String = a.toString + " " + b
@main def run(): Unit =
  println(f(1) + " " + f(5))
```

`tests/conformance/23-named-arguments/named-on-a-method.scala`:
```scala
// EXPECT: 30
class Rect(val w: Int, val h: Int):
  def scaled(byW: Int = 1, byH: Int = 1): Int = w * byW * h * byH
@main def run(): Unit =
  println(new Rect(2, 3).scaled(byH = 5))
```

`tests/conformance/23-named-arguments/named-on-a-constructor.scala`:
```scala
// EXPECT: 2 7
class P(val x: Int, val y: Int = 7)
@main def run(): Unit =
  val p = new P(x = 2)
  println(p.x.toString + " " + p.y)
```

`tests/conformance/23-named-arguments/named-on-a-case-class-apply.scala`:
```scala
// EXPECT: P(1,9) P(1,2)
case class P(x: Int, y: Int = 9)
@main def run(): Unit =
  println(P(x = 1).toString + " " + P(1).copy(y = 2))
```

`tests/conformance/23-named-arguments/unknown-name.scala`:
```scala
// EXPECT-ERROR: has no parameter named 'z'
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(a = 1, z = 2))
```

`tests/conformance/23-named-arguments/duplicate-name.scala`:
```scala
// EXPECT-ERROR: received parameter 'a' twice
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(1, a = 2))
```

`tests/conformance/23-named-arguments/missing-argument.scala`:
```scala
// EXPECT-ERROR: is missing argument 'b'
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(a = 1))
```

`tests/conformance/23-named-arguments/named-evaluation-order.scala`:
```scala
// EXPECT: b a 11
// Arguments are evaluated at the call site in SOURCE order, even when the names
// reorder them (Scala's rule, plan A0-11).
var log = ""
def side(tag: String, v: Int): Int =
  log = log + tag + " "
  v
def f(a: Int, b: Int): Int = a + b * 10
@main def run(): Unit =
  val r = f(b = side("b", 1), a = side("a", 1))
  println(log + r)
```

Verify all ten against scalac except `unknown-name`, `duplicate-name` and `missing-argument`, which are **compile** errors in scalac and run-time errors here (D80); those three record the divergence in a comment.

- [ ] **Step 2: Record parameter names and default blocks**

In `Compiler::compileFunction`, after the parameter slots are allocated, call `fn_->mod->setParamNames(names)` and, for each parameter with an initialiser, compile that initialiser as a **block** of the same module (`addBlock`) whose captures are the already-bound parameter slots, and record it with `setDefaultBlock(i, blockIndex)`. A default that reads an earlier parameter therefore reads it through a capture, which is what makes `default-reads-an-earlier-parameter` work. `linkSymbols` interns the names with `createSymbol` alongside the existing name constants (`BytecodeModule.cpp:230`, `:241`).

- [ ] **Step 3: Compile a keyword call site**

In `Compiler::compileArgsAndCall` and `compileNamedSend`, split arguments into positional and `NamedArg` ones **in source order**, emit every argument expression in that source order, and then emit `SEND_KW` (a member call) or `CALL_KW` (a function value). The `KwSendSite` constant already carries `(name, positional count, keyword names)` — `addKwSendSite` (`BytecodeModule.h:94`) needs no change. Delete the `throw CompileError("named arguments are not implemented yet", n.pos)` at `Compiler.cpp:422`.

For `new C(x = 1)` and `Geometry.P(x = 1)`, `NEW` and `SEND_APPLY` need keyword forms too. Rather than two more opcodes: compile `new C(named…)` as `MAKE_CLASS`-less `PUSH_GLOBAL <@C>; args; SEND_KW <init>`, which is what `INVOKE_INIT` already expects, and let `sendKeywords` reach the constructor. Confirm that with `named-on-a-constructor.scala` before writing the rest.

- [ ] **Step 4: Bind in the callee**

```cpp
void ExecutionEngine::bindKeywordsAndDefaults(proto::ProtoContext& frame,
                                              const BytecodeModule& mod,
                                              const proto::ProtoSparseList* keywords,
                                              const proto::ProtoObject** slots,
                                              unsigned positional) {
    const auto& symbols = mod.paramNameSymbols();
    const auto& names = mod.paramNames();
    std::vector<bool> filled(symbols.size(), false);
    for (unsigned k = 0; k < positional && k < symbols.size(); ++k) filled[k] = true;
    if (keywords) {
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            // The key is the ADDRESS of the interned parameter-name symbol —
            // protoCore's own convention (plan A0-12). It is sound because an
            // interned string is perennial: never collected, never moved, so
            // there is nothing for the collector to trace.
            const auto key = reinterpret_cast<unsigned long>(symbols[i]);
            const proto::ProtoObject* v = keywords->getAt(&frame, key);
            if (!v) continue;
            if (filled[i])
                throw ScalaError("IllegalArgumentException",
                                 mod.name() + " received parameter '" + names[i] + "' twice");
            slots[i] = v;
            filled[i] = true;
        }
        // Any key that matched no parameter is a caller mistake, and must be
        // loud: silently dropping it is the failure mode A0-12's trap warns of.
        unsigned long matched = 0;
        for (std::size_t i = 0; i < symbols.size(); ++i)
            if (keywords->has(&frame, reinterpret_cast<unsigned long>(symbols[i]))) ++matched;
        if (matched != keywords->getSize(&frame))
            throw ScalaError("IllegalArgumentException",
                             mod.name() + " has no parameter named '" +
                                 unmatchedKeywordName(&frame, layout_, mod, keywords) + "'");
    }
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (filled[i]) continue;
        const std::size_t block = mod.defaultBlock(i);
        if (block == BytecodeModule::kNoDefault)
            throw ScalaError("IllegalArgumentException",
                             mod.name() + " is missing argument '" + names[i] + "'");
        // Defaults run in parameter order and may read earlier parameters
        // through their captures (plan A0-11).
        slots[i] = execute(&frame, mod.block(block), slots, static_cast<unsigned>(i), nullptr);
    }
}
```

`unmatchedKeywordName` iterates the keyword list and returns the name of the first key that is not one of `mod.paramNameSymbols()`, recovering the name by `reinterpret_cast<const proto::ProtoString*>(key)->toStdString(ctx)` — which is only sound because the key **is** an interned symbol's address. Put that sentence in a comment at the function.

Then call `bindKeywordsAndDefaults` from `execute` (a new overload taking a keyword list) and make `sendKeywords` reach it for a compiled Scala callee, deleting the `throw ScalaError("UnsupportedOperationException", "named arguments are not supported yet for methods written in Scala …")` at `ExecutionEngine.cpp:385-388`. Implement `CALL_KW` in `runLoop` the same way `CALL` is implemented, with the keyword list built as `sendKeywords` builds it.

`execute` must also fill defaults for a call that passed **no** keywords at all (`f()` on `def f(a: Int = 1)`), so its existing arity check becomes: a positional count below `arity` is acceptable when every missing parameter has a default.

- [ ] **Step 5: Unit tests**

```cpp
TEST(NamedArguments, ReorderAndDefaults) {
    EvalHarness h;
    h.eval("def f(a: Int, b: Int = 2): String = a.toString + \"/\" + b");
    EXPECT_EQ(h.eval("f(1)"), "1/2");
    EXPECT_EQ(h.eval("f(a = 1)"), "1/2");
    EXPECT_EQ(h.eval("f(b = 9, a = 1)"), "1/9");
    EXPECT_EQ(h.eval("f(1, b = 9)"), "1/9");
}

TEST(NamedArguments, ErrorsAreLoud) {
    EvalHarness h;
    h.eval("def f(a: Int, b: Int): Int = a + b");
    EXPECT_NE(h.eval("f(a = 1, z = 2)").find("has no parameter named 'z'"), std::string::npos);
    EXPECT_NE(h.eval("f(1, a = 2)").find("received parameter 'a' twice"), std::string::npos);
    EXPECT_NE(h.eval("f(a = 1)").find("is missing argument 'b'"), std::string::npos);
}
```

- [ ] **Step 6: Pin the interning trap with a visible-failure fixture**

`tests/conformance/23-named-arguments/named-argument-really-binds.scala`:
```scala
// EXPECT: w=2 h=3
// The keyword key is the ADDRESS of the interned parameter-name symbol
// (plan A0-12). A key built from a non-interning constructor would match
// nothing and the argument would be SILENTLY dropped, so this fixture prints
// the bound values rather than merely calling without crashing.
def describe(w: Int = 0, h: Int = 0): String = "w=" + w + " h=" + h
@main def run(): Unit = println(describe(h = 3, w = 2))
```

If the key were built with `fromUTF8String`, both parameters would take their defaults and the output would be `w=0 h=0` — a visible, unambiguous failure. Add that sentence to the fixture's comment so a future reader knows what it is guarding.

**Done when:** `ctest --test-dir build_release -R '23-named-arguments|NamedArguments' --output-on-failure` passes all eleven fixtures and both unit cases; the seven scalac-verifiable fixtures match byte for byte; `grep -c 'named arguments are not' src/compiler/Compiler.cpp src/runtime/ExecutionEngine.cpp` returns `0`; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 10: Extension methods

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`, `src/frontend/Desugar.cpp`, `src/compiler/Compiler.h`, `src/compiler/CompileTemplates.cpp`
- Test: `tests/conformance/22-super-and-extensions/extension-*.scala` (new), `tests/unit/test_compiler.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `NodeKind` gains `ExtensionDef`;
```cpp
// extension (x: T) def m(...) = ..., or a collective extension with a body.
// The compiler installs each member as an attribute of T's prototype (D6, D81).
struct ExtensionDef : Node {
    explicit ExtensionDef(SourcePos p) : Node(NodeKind::ExtensionDef, p) {}
    std::string receiverName;      // `x`
    TypePtr receiverType;          // `T`
    std::vector<NodePtr> members;  // DefDef nodes
};
```
- `src/frontend/Parser.h`, private: `NodePtr parseExtension();`
- `src/compiler/Compiler.h`, private: `void compileExtension(const ExtensionDef& n);`

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/22-super-and-extensions/extension-on-a-builtin.scala`:
```scala
// EXPECT: 6 SHOUT 3
extension (n: Int) def triple: Int = n * 3
extension (s: String) def shout: String = s.toUpperCase
extension (xs: List[Int]) def mySum: Int = xs.foldLeft(0)(_ + _)
@main def run(): Unit =
  println(2.triple.toString + " " + "shout".shout + " " + List(1, 2).mySum)
```

`tests/conformance/22-super-and-extensions/extension-on-a-class.scala`:
```scala
// EXPECT: 5
class P(val x: Int, val y: Int)
extension (p: P) def manhattan: Int = p.x + p.y
@main def run(): Unit = println(new P(2, 3).manhattan)
```

`tests/conformance/22-super-and-extensions/extension-with-arguments.scala`:
```scala
// EXPECT: 7 12
extension (n: Int)
  def plus(m: Int): Int = n + m
  def times(m: Int): Int = n * m
@main def run(): Unit = println(3.plus(4).toString + " " + 3.times(4))
```

`tests/conformance/22-super-and-extensions/extension-with-arguments-braces.scala` is the same program in brace syntax.

`tests/conformance/22-super-and-extensions/extension-name-collision.scala`:
```scala
// EXPECT-ERROR: already has a member named 'length'
extension (s: String) def length: Int = 0
@main def run(): Unit = println("x".length)
```

`tests/conformance/22-super-and-extensions/extension-on-an-unknown-type.scala`:
```scala
// EXPECT-ERROR: is not a protoScala type
extension (x: Widget) def m: Int = 1
@main def run(): Unit = println(1)
```

`tests/conformance/22-super-and-extensions/extension-is-global.scala`:
```scala
// EXPECT: 6 6
// D81: an extension is global and session-wide, with no import scoping, so a
// method defined at the top level is visible inside every later definition.
extension (n: Int) def triple: Int = n * 3
def useIt(n: Int): Int = n.triple
@main def run(): Unit = println(useIt(2).toString + " " + 2.triple)
```

Verify `extension-on-a-builtin`, `extension-on-a-class`, `extension-with-arguments` and `extension-is-global` against scalac. `extension-name-collision` and `extension-on-an-unknown-type` are protoScala-only diagnostics (scalac allows shadowing and would reject the unknown type differently); record that in their comments.

- [ ] **Step 2: Write the `StringContext` fixture that closes Phase 3's restriction**

**Only if Phase 3 has landed** (check `grep -c 'StringContext\|CONCAT' src/compiler/Opcodes.h docs/STATUS.md`). Phase 3's A0-3 rejects any interpolator that is not `s`, `f` or `raw`, because defining one needs `extension (sc: StringContext)`. Now it can be defined:

`tests/conformance/22-super-and-extensions/extension-custom-interpolator.scala`:
```scala
// EXPECT: [1|2]
extension (sc: StringContext) def bars(args: Any*): String = args.mkString("[", "|", "]")
@main def run(): Unit = println(bars"$1$2")
```

That requires Phase 3's desugarer to stop rejecting an unknown interpolator and instead lower it to `StringContext(<literals>).<name>(<args>)`, plus a `StringContext` prelude class with a `parts: List[String]` field. Do both in this step and record in `docs/STATUS.md` that Phase 3's restriction is closed. If Phase 3 has **not** landed, skip this step entirely and say so in the task notes — do not add a `StringContext` class that nothing produces.

- [ ] **Step 3: Parse `extension`**

Replace `Parser.cpp:898`'s `notImplemented("'extension' definitions", t)` with `return parseExtension();`, and write `parseExtension`:

```
Extension ::= 'extension' ['[' TypeParams ']'] '(' id ':' Type ')'
              ( DefDef | (':' | '{') {DefDef} )
```

A single `def` on the same line is the short form; a body (braced or indented) is a collective extension. Every member must be a `def` — a `val` extension is rejected with `an extension member must be a def`.

- [ ] **Step 4: Compile it**

```cpp
// extension (x: T) def m(a: A): R  becomes a method installed on T's prototype
// with `x` as slot 0, i.e. exactly the shape a method of T has. Dispatch is
// therefore the ordinary prototype walk (D6), and the installation is global and
// session-wide (D81, D82).
void Compiler::compileExtension(const ExtensionDef& n) {
    const std::string typeName = resolveType(*n.receiverType, n.pos);
    const ClassInfo* t = globals_.findType(typeName);
    if (!t)
        throw CompileError("extension: " + typeName + " is not a protoScala type", n.pos);
    for (const NodePtr& m : n.members) {
        const DefDef& d = as<DefDef>(*m);
        if (t->members.count(d.name))
            throw CompileError("extension: " + typeName + " already has a member named '" +
                                   d.name + "'",
                               d.pos);
        // FnShape::Method puts the receiver in slot 0 and gives the body no
        // enclosing function, which is what `x` must be.
        compileFunction(d.name, prependReceiver(n.receiverName, d.paramLists), *d.body,
                        FnShape::Method, d.pos);
        emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(t->key), n.pos, +1);
        emit(Op::SET_FIELD, fn_->mod->addSymbol(d.name), n.pos, -2);
    }
    // Record it so a later collision is caught at compile time too.
    if (ClassInfo* mut = globals_.mutableTypeByKey(t->key))
        for (const NodePtr& m : n.members)
            mut->members[as<DefDef>(*m).name] =
                MemberInfo{MemberKind::Def, as<DefDef>(*m).name, true};
}
```

`SET_FIELD` requires a mutable receiver, and the runtime prototypes (`intProto`, `stringProto`, `listProto`, a user class's prototype) are created mutable by `Runtime` and `MAKE_CLASS`. Confirm that with `extension-on-a-builtin.scala` before writing the rest; if a prototype turns out to be immutable, the installation needs a dedicated native (`__installExtension(cls, name, fn)`) instead, and that is a Task 0 addendum, not a silent workaround.

- [ ] **Step 5: Unit tests**

```cpp
TEST(Extensions, DispatchIsOnTheRuntimePrototype) {
    EvalHarness h;
    h.eval("extension (n: Int) def triple: Int = n * 3");
    EXPECT_EQ(h.eval("2.triple"), "6");
    EXPECT_EQ(h.eval("(2: Any).asInstanceOf[Int].triple"), "6");
    EXPECT_NE(h.eval("\"x\".triple").find("is not a member of String"), std::string::npos);
}

TEST(Extensions, ACollisionWithAnExistingMemberIsRejected) {
    EvalHarness h;
    EXPECT_NE(h.eval("extension (s: String) def length: Int = 0")
                  .find("already has a member named 'length'"),
              std::string::npos);
}
```

**Done when:** `ctest --test-dir build_release -R '22-super-and-extensions/extension|Extensions' --output-on-failure` passes all eight fixtures (seven if Phase 3 has not landed) and both unit cases; the four scalac-verifiable fixtures match byte for byte; `grep -c "'extension' definitions" src/frontend/Parser.cpp` returns `0`; and the full suite is green.

---

### Task 11: Named arguments reaching a non-Scala callee

**Files:**
- Modify: `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `docs/INTEROP.md`
- Test: `tests/conformance/23-named-arguments/foreign-*.scala` (new), `tests/unit/test_exceptions.cpp` (modify)

**Interfaces:**
- `src/runtime/Primitives.h` gains:
```cpp
// The keyword-argument stand-in for a foreign callable (plan A0-12). `__kwprobe`
// is a global object whose native `call` method reports the positional and
// keyword arguments it received, exactly as a UMD-provided foreign method will:
// it reads protoCore's keywordParameters directly and recovers each name from
// the interned symbol its key is the address of. It exercises the whole
// protoScala half of the convention without UMD, which is Phase 6.
void installKeywordProbe(proto::ProtoContext* ctx, const RuntimeLayout& layout);
```

- [ ] **Step 1: Write the stand-in**

The point of the stand-in is that it is reached through **exactly the same `SEND_KW` path** a foreign object will be, and that it reads `keywordParameters` the way a foreign callee does — nothing about it is Scala-aware.

```cpp
// __kwprobe.call(positional..., named = ...) -> a deterministic report.
//
// A foreign method reached through UMD receives keyword arguments in
// protoCore's `keywordParameters` ProtoSparseList, whose key is the ADDRESS of
// the interned ProtoString symbol for the parameter's name (plan A0-12,
// src/runtime/ExecutionEngine.cpp:389-392). Recovering the name from the key is
// therefore a cast: the key IS that symbol's address, and an interned string is
// perennial — never collected, never moved — so the address stays valid for the
// life of the ProtoSpace. That perenniality is also why an integer-keyed
// ProtoSparseList is the right structure here and ProtoMap is not: ProtoMap
// exists for arbitrary COLLECTABLE object keys the collector must trace, which
// is a different problem (the same principle that makes ProtoTuple interned).
const ProtoObject* prim_kwprobe_call(ProtoContext* ctx, const ProtoObject*,
                                     const proto::ParentLink*, const ProtoList* args,
                                     const proto::ProtoSparseList* keywords) {
    const RuntimeLayout& L = layoutOf();
    std::string out = "pos=[";
    const unsigned long n = args ? args->getSize(ctx) : 0;
    for (unsigned long i = 0; i < n; ++i) {
        if (i > 0) out += ",";
        out += show(ctx, L, args->getAt(ctx, static_cast<int>(i)));
    }
    out += "] kw=[";
    // Sorted by name, so the report is deterministic whatever order the keys
    // happen to sit in (a ProtoSparseList orders by key word, i.e. by address).
    std::vector<std::pair<std::string, std::string>> pairs;
    if (keywords) {
        const proto::ProtoSparseListIterator* it = keywords->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const unsigned long key = it->nextKey(ctx);
            const ProtoObject* v = it->nextValue(ctx);
            const auto* sym = reinterpret_cast<const proto::ProtoString*>(key);
            pairs.emplace_back(sym->toStdString(ctx), show(ctx, L, v));
            it = it->advance(ctx);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) out += ",";
        out += pairs[i].first + "=" + pairs[i].second;
    }
    return str(ctx, out + "]");
}
```

Declare it with the raw `ProtoMethod` signature rather than the `PRIM` macro, because it is the one native in the tree that **uses** its fifth parameter and the macro discards it. Say so in a comment. Wrap the installation in `installKeywordProbe(ctx, layout)`, declared in `src/runtime/Primitives.h` as given under **Interfaces**, and call it from `installPrimitives` immediately after `installActorPrimitives` (`src/runtime/Primitives.cpp:936`). Add `"__kwprobe"` to `builtinGlobalNames()` and install the object in `installPrimitives`.

Check the exact `ProtoSparseListIterator` member names against `protoCore/headers/protoCore.h` before writing this — the `ProtoMapIterator` shown in that header has `hasNext`/`nextKey`/`nextValue`/`advance`, and the sparse-list iterator is expected to match, but **read it, do not assume it**. If it does not offer a key accessor, use `keywords->processElements(...)` with a `void* self` collector instead, which the map helper's API already demonstrates.

- [ ] **Step 2: Write the fixtures that can run today**

`tests/conformance/23-named-arguments/foreign-keywords-arrive.scala`:
```scala
// EXPECT: pos=[1,2] kw=[dtype=float64,order=C]
// A callee that is not a Scala method receives named arguments in protoCore's
// keywordParameters, keyed by the address of the interned parameter-name symbol
// (plan A0-12). __kwprobe reads them exactly as a UMD-provided foreign method
// will, so this fixture exercises the whole protoScala half of the convention.
@main def run(): Unit =
  println(__kwprobe.call(1, 2, dtype = "float64", order = "C"))
```

`tests/conformance/23-named-arguments/foreign-keywords-only.scala`:
```scala
// EXPECT: pos=[] kw=[encoding=utf-8]
@main def run(): Unit =
  println(__kwprobe.call(encoding = "utf-8"))
```

`tests/conformance/23-named-arguments/foreign-keyword-value-is-not-lost.scala`:
```scala
// EXPECT: pos=[] kw=[a=1,b=2,c=3]
// If a key were built with a non-interning constructor, it would match nothing
// and the arguments would be silently dropped, so this prints every value.
@main def run(): Unit =
  println(__kwprobe.call(c = 3, a = 1, b = 2))
```

- [ ] **Step 3: Write the fixtures that are deferred until UMD lands**

`tests/conformance/23-named-arguments/foreign-python-keyword.scala`:
```scala
// XFAIL: requires UMD (Phase 6): `import py.` routing is not implemented
// The motivating case. Python keyword arguments are load-bearing — np.array
// cannot be called without `dtype` — and they cross the boundary with no
// adapter, because a Python callee reached through UMD receives them in
// protoCore's keywordParameters like every other protoCore method (A0-12).
// Phase 6 converts this fixture by changing XFAIL to EXPECT; the expected line
// is recorded here so it is not invented then.
// Expected once UMD lands: EXPECT: float64
import py.numpy as np
@main def run(): Unit =
  val a = np.array(List(1, 2, 3), dtype = "float64")
  println(a.dtype.name)
```

`tests/conformance/23-named-arguments/foreign-python-open-encoding.scala`:
```scala
// XFAIL: requires UMD (Phase 6): `import py.` routing is not implemented
// Expected once UMD lands: EXPECT: utf-8
import py.io as io
@main def run(): Unit =
  val f = io.open("/dev/null", mode = "r", encoding = "utf-8")
  println(f.encoding)
```

Both are `XFAIL`, so the conformance runner **fails loudly if they ever start passing** without the marker being removed — which is exactly the reminder Phase 6 needs.

- [ ] **Step 4: Pin the interning trap with a unit test**

```cpp
TEST(KeywordConvention, ANonInternedKeyMatchesNothing) {
    // createSymbol interns; fromUTF8String does not and returns a different
    // pointer for the same text. A key built from the latter matches no
    // parameter, and the failure is SILENT — the argument simply never binds.
    // protoJS hit this class of bug more than once (plan A0-12).
    EvalHarness h;
    proto::ProtoContext* ctx = h.runtime().rootContext();
    const proto::ProtoString* interned = proto::ProtoString::createSymbol(ctx, "dtype");
    const proto::ProtoString* interned2 = proto::ProtoString::createSymbol(ctx, "dtype");
    const proto::ProtoString* loose = proto::ProtoString::fromUTF8(ctx, "dtype");
    EXPECT_EQ(interned, interned2) << "createSymbol must intern";
    EXPECT_NE(interned, loose) << "fromUTF8 must not intern";

    const proto::ProtoSparseList* kw = ctx->newSparseList();
    kw = kw->setAt(ctx, reinterpret_cast<unsigned long>(loose), proto::makeSmallInt(1));
    EXPECT_FALSE(kw->has(ctx, reinterpret_cast<unsigned long>(interned)))
        << "a key built from a non-interning constructor must not match the interned one";

    const proto::ProtoSparseList* good = ctx->newSparseList();
    good = good->setAt(ctx, reinterpret_cast<unsigned long>(interned), proto::makeSmallInt(1));
    EXPECT_TRUE(good->has(ctx, reinterpret_cast<unsigned long>(interned2)));
}
```

- [ ] **Step 5: Record the convention where an implementer will find it**

Add a section to `docs/INTEROP.md`:

> **Named arguments across the boundary.** protoScala compiles `f(x = 1)` into protoCore's `keywordParameters`, the fifth parameter of every `ProtoMethod`, for Scala-defined methods, native methods and foreign callables alike — there is no special case for the foreign path, because there is no boundary to translate at. The key is the **address of the interned `ProtoString` symbol** for the parameter's name, obtained with `ProtoString::createSymbol`; `fromUTF8String` and the other non-interning constructors return a different pointer for the same text and produce a key that matches nothing, silently. An integer-keyed `ProtoSparseList` is the right structure and not a compromise: interned strings are perennial, so there is nothing for the collector to trace. `ProtoMap` exists for arbitrary *collectable* object keys, which is a different problem — the same principle that makes `ProtoTuple` interned and perennial, and the same reason transient data must never be mapped to it. Each other runtime translates its own surface on its own side of the boundary: Python's `**kwargs` map straight across, JavaScript has no keyword arguments and its provider decides what an options object means, Clojure's trailing map and Smalltalk's keyword selectors likewise. None of that is protoScala's concern.
>
> **What is implemented:** the protoScala side, exercised end to end by `tests/conformance/23-named-arguments/foreign-*.scala` against a stand-in callee. **What is not:** UMD itself (Phase 6), so no real `import py.…` call can be made yet; `foreign-python-keyword.scala` and `foreign-python-open-encoding.scala` are `XFAIL` with their expected output recorded, for Phase 6 to convert.

**Done when:** `ctest --test-dir build_release -R '23-named-arguments/foreign|KeywordConvention' --output-on-failure` passes the three runnable fixtures, reports the two `XFAIL` fixtures as passing-because-still-failing, and passes the unit case; `docs/INTEROP.md` carries the section; and the full suite is green.

---

### Task 12: The performance check the retry loop owes

**Files:**
- Modify: `benchmarks/RESULTS.md`
- Test: none new; this task runs the existing suites

**Interfaces:** none.

- [ ] **Step 1: Re-run the workload suite and compare**

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
uptime
rm -rf build_release && cmake -B build_release -S . && cmake --build build_release
python3 benchmarks/run_benchmarks.py --name phase4 --runs 5 --warmup 2
diff <(grep '^|' benchmarks/reports/*phase4-baseline.md) <(grep '^|' benchmarks/reports/*phase4.md) || true
```

Expected: no FAILED cell, and no workload more than 3 % slower than the Preflight Step 4 baseline. The retry loop adds one C++ `try` region per frame, which on a zero-cost-exceptions ABI should be free on the non-throwing path — but "should be" is not a measurement, and `fib`, `fib30`, `tak` and `attr_lookup` are the call-heavy workloads that would show it.

- [ ] **Step 2: If anything regressed, measure the cause before changing anything**

```bash
perf stat -r 3 build_release/protoscala benchmarks/comparable/fib30.scala 2>&1 | grep -E 'cycles|instructions|elapsed'
```

A regression in a call-heavy workload with **flat instruction counts** is an icache-layout effect (the protoPython "micro-opts can hurt icache layout" lesson), and the fix is to move `runFrame`'s cold path — the whole catch block — into a `[[gnu::noinline, gnu::cold]]` helper so the hot `runLoop` call stays in one cache line. Do that only if the measurement says so, and record both numbers.

- [ ] **Step 3: Re-run the actor suite**

```bash
bash benchmarks/actor-bench.sh 2>&1 | tail -30
```

Expected: every mode's self-reported message count verified, and no mode more than 5 % below the Preflight baseline. Task 5 changed `resumeFrames` and `deliver`, which the `await` and `ping-pong` modes exercise hardest.

- [ ] **Step 4: Record the table**

Add a `## Phase 4 workloads` section to `benchmarks/RESULTS.md` with machine, date, commit, protoCore commit, load average, the before/after columns and one sentence naming what changed in the engine. If the load average was above 2.0, say the comparison is indicative and not claimed.

**Done when:** `benchmarks/reports/<date>-phase4.md` exists with no FAILED cell; `benchmarks/RESULTS.md` carries the before/after table; and either every workload is within 3 % of the baseline or the regression is measured, explained and recorded with its `perf stat -r 3` numbers.

---

### Task 13: Tutorial chapters 11 and 12

**Files:**
- New: `docs/tutorial/11-exceptions.md`, `docs/tutorial/12-enums-and-sealed-hierarchies.md`
- Modify: `docs/TUTORIAL.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-scala-developer.md`, `docs/tutorial/05-functions-and-closures.md`, `docs/tutorial/06-classes-objects-and-traits.md`, `docs/tutorial/13-actors-and-futures.md`
- New: `tests/conformance/tutorial/11-*.scala`, `12-*.scala`; modify `tests/conformance/tutorial/05-*`, `06-*`, `13-*`

**Interfaces:** every runnable snippet in a chapter is the body, verbatim, of a fixture `tests/conformance/tutorial/NN-<topic>-<name>.scala` whose `// EXPECT:` line is the output printed under the snippet.

- [ ] **Step 1: Write chapter 11**

`docs/tutorial/11-exceptions.md` covers, in order: `throw` and the `Throwable` hierarchy; `try`/`catch` as an **expression**; `catch` as a pattern match (the part Scala programmers under-use and Python programmers have no analogue for); `finally`; what the runtime throws on its own (the A0-6 table, as prose); `Try` as the value-oriented alternative; exceptions inside an actor handler and inside an `await`; and a closing "What differs from Scala 3 in this area" section listing D71, D72, D73, D74, D84 and D85 with one sentence each.

For the **Python/JavaScript** reader: `try`/`catch`/`finally` is `try`/`except`/`finally`; `throw` is `raise`; there is no `else` clause; a `catch` clause is a **pattern match**, so `case AppError(code, _) if code > 100 =>` does in one line what Python needs an `if` inside the `except` body for; and there are no exception *groups*. For the **Scala** reader: the hierarchy is the small one of Task 4 with unqualified names, there is no `NonFatal`, no `scala.util.control.ControlThrowable`, no stack traces, and a VM defect (`std::logic_error`) is deliberately not catchable (D73).

- [ ] **Step 2: Write chapter 12**

`docs/tutorial/12-enums-and-sealed-hierarchies.md` covers: `enum` with simple cases; `ordinal`, `values`, `valueOf`, `fromOrdinal`; parameterised cases; `enum` as an algebraic data type and matching on it; `sealed trait` hierarchies and why exhaustiveness is **not** checked here (D4); when to reach for which; and a closing departures section listing D76, D77 and D78.

For the **Python/JavaScript** reader: an `enum` is Python's `Enum` with `ordinal` instead of `value`, and it is also a TypeScript discriminated union — the ADT form (`enum Tree { case Leaf(n: Int); case Node(l: Tree, r: Tree) }`) is the thing neither language has and the one worth the most prose. For the **Scala** reader: no `derives`, `values` is a `List`, and a case resolves unqualified as well as qualified (D77).

- [ ] **Step 3: Turn every snippet into a fixture**

For each runnable snippet, create the matching fixture. For example, the chapter-11 snippet

```scala
def parse(s: String): Int =
  try s.toInt
  catch case e: NumberFormatException => -1
println(parse("42").toString + " " + parse("x"))
```

becomes `tests/conformance/tutorial/11-exceptions-try-is-an-expression.scala`:

```scala
// EXPECT: 42 -1
def parse(s: String): Int =
  try s.toInt
  catch case e: NumberFormatException => -1
@main def run(): Unit =
  println(parse("42").toString + " " + parse("x"))
```

and the chapter prints `42 -1` under it. A snippet shown as failing gets an `// EXPECT-ERROR:` fixture quoting the message the binary actually prints.

- [ ] **Step 4: Extend the existing chapters**

- `docs/tutorial/05-functions-and-closures.md`: a "Named and default arguments" section (D80, D83) with fixtures.
- `docs/tutorial/06-classes-objects-and-traits.md`: a `super[T].m` section (D75), an "Extension methods" section (D81, D82) and a "Templates inside an object" section (D79), each with fixtures.
- `docs/tutorial/13-actors-and-futures.md`: replace the `RuntimeError` text with real exceptions, and add the `try { f.await } catch { … }` example that D50's retirement makes possible, with a fixture.
- `docs/tutorial/02-*.md`: the Python/JavaScript bridge sections of Steps 1 and 2, plus keyword arguments (`describe(h = 3, w = 2)` next to Python's identical call and JavaScript's options object).
- `docs/tutorial/03-*.md`: every new D-id in the departures catalogue, and the **retirement** of D44, D50 and D52 — which means deleting their entries and saying in one line that they were provisional Phase 5 behaviours this phase removed.

- [ ] **Step 5: Update the chapter table**

In `docs/TUTORIAL.md`, replace the `11 | Exceptions | *Planned* (Phase 4).` and `12 | Enums and sealed hierarchies | *Planned* (Phase 4).` rows with links and one-line contents, and update the "What runs today" blockquote to name protoScala 0.5.0 and the new surface.

**Done when:** `ctest --test-dir build_release -R 'tutorial' --output-on-failure` passes every new and modified fixture; `docs/TUTORIAL.md` has no `*Planned* (Phase 4)` row left; and `grep -c 'RuntimeError' docs/tutorial/*.md` returns `0`.

---

### Task 14: Status, deviations, changelog and release 0.5.0

**Files:**
- Modify: `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `docs/DESIGN.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt`

- [ ] **Step 1: Verify everything, from clean, before writing a word of status**

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
rm -rf build_release
cmake -B build_release -S . && cmake --build build_release 2>&1 | grep -Ei 'warning|error' | head
ctest --test-dir build_release --output-on-failure 2>&1 | tail -5
PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release --output-on-failure 2>&1 | tail -5
PROTOSCALA_ACTOR_WORKERS=1 ctest --test-dir build_release 2>&1 | tail -3
PROTOSCALA_ACTOR_WORKERS=16 ctest --test-dir build_release 2>&1 | tail -3
ctest --test-dir build_release -N | tail -1
bash benchmarks/cold-start.sh build_release/protoscala 20
uptime
```

Expected: no warning, no error, `100% tests passed` four times, a test count to quote, and a cold-start figure. **Record the load average with the cold-start number** and, if it is above 2.0, say the budget is not claimed as met — the prelude grew by twenty exception classes and DESIGN §1's < 25 ms budget was already at the line in Phase 5.

- [ ] **Step 2: `docs/STATUS.md`**

Update the "Current state" blockquote to 0.5.0 and the new test counts. Move the Phase 4 rows from "Not yet implemented" into "Implemented" — **including the `Multiple constructor parameter lists.` line, which carries no phase today and must move with D83 attached** (A0-14's ruling). Add the three opcode rows (`CALL_KW` 80, `THROW` 96, `RETHROW` 97) to the opcode table and update the band comments. Add a "Provisional deviations (Phase 4) — pending maintainer decision" table with D71–D89, each naming the plan question it answers or `—`. **Delete D44, D50 and D52** and add one line under the Phase 5 table saying they were provisional behaviours Phase 4 removed (A0-16). Add to "Known issues / platform dependencies": that a suspended actor never runs its `finally` (D74), that named arguments reach a foreign callee through protoCore's `keywordParameters` **but UMD is not implemented, so the foreign half is unexercised and two fixtures are `XFAIL` for Phase 6**, the cold-start figure with its load average, and the Task 12 benchmark comparison.

- [ ] **Step 3: `docs/LANGUAGE.md`**

Mark §2's `try`/`catch`/`finally`, `throw` row ✅ (Phase 4) and §2's `val, var, lazy val, def (… default and named arguments …)` row ✅. In §3, move `enum`, extension methods, `super[T].m`, named and default arguments, multiple constructor parameter lists and templates nested in an `object` from "Later phases" into the delivered list, and leave local and anonymous classes there with D79 as the reason. Add §3.1 documenting the exception hierarchy as a tree. Add D71–D89 to §5 and remove D44, D50 and D52.

- [ ] **Step 4: `docs/ROADMAP.md`**

**Correct Phase 4's done-when first** (A0-14's ruling): add `multiple constructor parameter lists` to the list, between `named and default arguments for Scala-defined methods` and `extension methods`, so ROADMAP, LANGUAGE §3 and STATUS finally agree. Then mark Phase 4 ✅ with the date and 0.5.0, with a "Delivered beyond the criteria" line naming the `Priority` enum (retiring D52), the exception-carrying `Failure` (retiring D44), the catchable failed `await` (retiring D50), and the keyword-argument convention documented in `docs/INTEROP.md` with a stand-in-callee fixture suite. Add to Phase 6's done-when: "convert `tests/conformance/23-named-arguments/foreign-python-*.scala` from `XFAIL` to `EXPECT`, and add the A0-1 boundary catch shape at every UMD and foreign-call site." Add one line under "Later (v0.7+)": "templates nested in a `class` or `trait`, local classes and anonymous classes `new T { … }` — a reasonably common Scala idiom that Phase 4 deliberately left out (D79), because a class-nested template captures the enclosing instance and needs a per-instance class."

- [ ] **Step 5: `docs/DESIGN.md`**

Four places need a dated note rather than a silent edit:

- **§7** — amend it to record A0-2 (the payload is re-rooted by every frame, in `ProtoContext::returnValue`), A0-3 (the handler search is a retry loop *outside* the C++ catch, and why), A0-4 (`finally` is a handler-table entry plus an inline copy) and A0-6's full translation table. The existing sentence "Scala has no resumable exceptions, so protoST's separate handler stack is not needed" is confirmed, not changed.
- **§8.3** — add that a `catch` or `finally` body suspends like any other bytecode, that **no `finally` runs on a suspension**, and that a failed future raises at the `await` site (A0-7 invariants 2 and 5), and that this retires D50.
- **§5.2** — add the A0-12 rationale paragraph verbatim: the key is the address of the interned parameter-name symbol; `createSymbol` interns and the other constructors do not; an integer-keyed `ProtoSparseList` is correct rather than a concession, because interned strings are perennial, and `ProtoMap` exists for collectable object keys, which is a different problem — the same principle that makes `ProtoTuple` interned. **Add A0-11's rationale in the same place, in the maintainer's own words:** *"la plataforma es lazy binding, aunque Scala no lo sea"* — the platform is late-binding even when the source language is not, so a protoCore runtime resolves named arguments in the callee at run time by design, not as a workaround for erased types; late detection is accepted, silent failure is not.
- **§4.4** — add A0-8's rule for `super[T].m` and D75.

- [ ] **Step 6: `docs/DECISIONS-LOG.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt`**

Add one row per A0-n with the Phase 5 shape (date, decision, taken by, where). **A0-12's row is marked `maintainer`, not `agent, pending review`** — the maintainer settled it, including the key convention and the perenniality rationale. Add a `## 0.5.0` section to `CHANGELOG.md` naming the protoCore commit the release was built against. Update `README.md`'s feature list, its "protoScala in 10 minutes" sections (one exception example and one `enum` example per audience, each a `tests/conformance/tutorial/` fixture), and its pending list — UMD is still pending and the named-argument story says so. Bump `project(protoScala VERSION 0.5.0 …)`.

- [ ] **Step 7: Commit**

One commit per task is the norm; where several tasks are one code path a combined commit is acceptable and its message says why, as the Phase 5 log does for Tasks 3–7. Tasks 1–4 are one code path (the exception machinery) and Tasks 5 and 9 each stand alone. **Do not create the `v0.5.0` tag** — the maintainer tags.

**Done when:** `ctest --test-dir build_release -N | tail -1` reports the new total; `100% tests passed` in all four configurations of Step 1; `build_release/protoscala --version` prints `0.5.0`; `grep -c 'D44\|D50\|D52' docs/STATUS.md docs/LANGUAGE.md` shows only the retirement notes and no live entries; `grep -c 'D7[7-9]\|D8[0-9]\|D9[0-5]' docs/STATUS.md docs/LANGUAGE.md` shows every new id in both files; and `git status --short` is clean after the commits.

---

## Task 0 triage: what needs the maintainer, and what was decided on cost

The maintainer's standing rule, 2026-09-23: *"En la medida que sea razonable, least surprise para el programador. Por otro lado, si alguien escribe usando esos detalles está jugando con fuego y no creo que sea tan común."* — **follow Scala where matching is cheap; where matching would cost real machinery and only unusual code could notice, document the divergence and move on.** Escalation is reserved for what genuinely needs a maintainer: a contradiction inside DESIGN or between the documents, a change to a public surface or a protoCore convention, anything touching the GC or the threading protocol, and anything where following Scala would cost machinery that a **common** usage depends on. Everything else below is decided, with its cost stated.

### Escalated — needs a maintainer ruling

Four items, each qualifying on the line above and not on taste.

| # | Question | Why it is escalated | Provisional behaviour | Cost if reversed |
|---|---|---|---|---|
| E1 | **Where does the in-flight exception value live while the stack unwinds?** (A0-2) | **GC protocol.** It decides when a `ProtoObject*` is reachable during C++ unwinding, which is P1's territory and not a language question | Every frame re-roots it in its own `ProtoContext::returnValue`; the only unrooted window is between one context's destruction and the next frame's catch, and nothing in that window allocates | A per-thread pin anchored in a root slot is additive and costs one attribute write per throw |
| E2 | **Must the handler search sit outside the C++ catch block?** (A0-3) | **Threading protocol.** Doing it inside makes a `catch` or `finally` body run with a live C++ handler, so a `FutureYield` from it propagates out of a handler with an exception in flight, and a resumed frame re-enters a handler body with nothing to handle. **It breaks Phase 5 silently** | A retry loop around `runLoop` with `continue`, so the handler body runs with no live C++ handler | Doing it inside is a smaller diff and a latent, silent defect; this is the decision most worth confirming |
| E3 | **Are A0-7's six invariants the right contract between exceptions and the actor/`await` machinery?** | **Threading protocol**, and the same silent-breakage risk as E2. Specifically: no `finally` on a suspension; `resumeFrames` must call `runFrame`; a failed `await` raises at its call site (retiring D50) | As stated, each with a named test (D74 records the one residual limit: an actor parked on a future that never completes never runs its `finally`) | Each invariant is one code path; getting one wrong is not visible in the suite unless its test exists, which is why they are stated as invariants |
| E4 | **Are global, session-wide extension methods acceptable, with no import scoping?** (A0-13) | **Public surface, and a common usage depends on it.** Scoping is how Scala 3 code organises extensions; a global monkey-patch can collide across files and cannot be undone within a session | Global and session-wide (D81), with a compile-time error on a collision with an existing member (D82) | Scoping extensions needs an import mechanism, which arrives with UMD in Phase 6 — so this is a "schedule it" question as much as a design one |

### Ruled by the maintainer (2026-09-23)

| # | Item | Ruling |
|---|---|---|
| R1 | Named arguments across the UMD boundary (A0-12) | **Settled, not a question.** protoScala compiles `f(x = 1)` into protoCore's `keywordParameters` for Scala, native and foreign callables alike; the key is the address of the interned parameter-name symbol via `createSymbol`; an integer-keyed `ProtoSparseList` is correct because interned strings are perennial |
| R2 | Bind named arguments in the callee, making a name typo a run-time error? (A0-11) | **Confirmed.** *"La plataforma es lazy binding, aunque Scala no lo sea."* Late detection is accepted; silent failure is not, so all three errors name what was expected and what arrived |
| R3 | `super[T].m` accepting any ancestor where Scala requires a direct parent (A0-8) | **Stands** (D75). Permissiveness, not a semantic mismatch: no program that scalac accepts behaves differently here |
| R4 | LANGUAGE §3 assigns multiple constructor parameter lists to Phase 4; ROADMAP's done-when omits them; STATUS lists them with no phase (A0-14) | **LANGUAGE is right — deliver them** (D83). Task 14 Steps 2 and 4 correct ROADMAP's done-when and STATUS so all three agree |

### Decided on cost, recorded for review

Each is implemented as stated, recorded in `docs/DECISIONS-LOG.md` as "agent, decided on cost", with the cost that decided it. **None needs a reply.**

| # | Divergence or choice | Decision, and the cost that decided it |
|---|---|---|
| C1 | No protoST-style handler stack (A0-1) | **None.** DESIGN §7 already prescribes it, and a handler stack buys `resume:`, which Scala does not have, at the price of a snapshot per suspension |
| C2 | `finally` as a handler-table entry plus an inline copy, not RAII (A0-4) | **Handler entry** (D71 records that a throwing `finally` replaces the in-flight exception, as in Scala). RAII would run cleanup inside a destructor, where a Scala throw crosses a `noexcept` boundary and terminates the process — not a cost trade-off but a correctness one |
| C3 | `throw` accepts only a `Throwable` (A0-5) | **Follow Scala.** Free: one marker test at `THROW`. No deviation recorded |
| C4 | The native-error translation table (A0-6) | **As tabulated** (D72, D73). Each row is one `catch` clause. The one deliberate hole — `std::logic_error` from a VM or compiler defect is **not** catchable — is least-surprise in the right direction: a `catch { case e: Throwable }` must never mask a protoScala bug |
| C5 | `enum` desugared in the frontend to a sealed class, cases and a companion (A0-9) | **Frontend.** A runtime `Enum` model would be a second object model for something the first already expresses. Consequences kept: `values` returns a `List`, not an `Array` (D76) — matching Scala needs an `Array` type that does not exist; a case resolves unqualified as well as qualified (D77) — permissive; `derives` parsed and ignored (D78) |
| C6 | Nested templates only in an `object`, not in a `class` (A0-10) | **Keep the limit** (D79). A class-nested template captures the enclosing instance, which needs a per-instance class — real machinery. It is the one item here a reasonably common Scala idiom touches, so Task 14 Step 4 records it in ROADMAP as a candidate for a later phase rather than leaving it only as a deviation |
| C7 | Three new opcodes: `CALL_KW` 80, `THROW` 96, `RETHROW` 97 (A0-15) | **Three.** Each is additive inside a band DESIGN §3.5 already reserves; nothing else needs one |
| C8 | Which Phase 5 deviations retire (A0-16) | **D44, D50 and D52 retire; D43 stays.** Retiring them is the point of the phase; D43's limit is a property of a recursive VM, not a choice |
| C9 | `catch someFunction` accepted and rewritten to `case e => f(e)` (Task 2 Step 2) | **Accept** (D84). Permissive and cheap; rejecting it later would break programs. A genuine `PartialFunction` would rethrow in Scala and raise `MatchError` here, and the deviation says so |
| C10 | `Throwable.getClass` returns a `String` (Task 4 Step 2) | **Keep** (D85). Matching Scala needs `Class[_]` values — a new type with no other use in this dialect — and `.getClass` on an exception is almost always fed to string concatenation anyway |

## Self-review against the done-when criteria

**ROADMAP Phase 4 "Done when", clause by clause:**

| Clause | Where it is met |
|---|---|
| `try`/`catch`/`finally`/`throw` **with pattern-matched handlers** | **Tasks 1, 2, 3** — the carrier, handler table and retry loop (Task 1); `throw`, `try`, `catch` as a full `compileMatch` cascade with `RETHROW` on no match (Task 2); `finally` on the normal, exceptional and `return` paths (Task 3). `tests/conformance/20-exceptions/`, 22 fixtures including `catch-is-a-pattern-match.scala` (constructor pattern + guard + type pattern in one handler) |
| native error translation | **Task 4** — the full A0-6 table implemented as `runLoop` branches plus lazy `materialiseError`; nine `native-*` fixtures, the `Error`-is-not-an-`Exception` fixture, the hierarchy-ancestors fixture, and three unit cases including "a VM defect is not catchable" |
| `super[T].m` | **Task 6** — `tests/conformance/22-super-and-extensions/super-*.scala`, seven fixtures; Phase 2's `stackable-traits.scala` still passes |
| `enum` | **Task 8** — `tests/conformance/21-enums/`, nine fixtures: simple, match, `valueOf`/`fromOrdinal`, parameterised cases, an ADT, methods in the body, unqualified case resolution |
| sealed hierarchies | **Task 8** — Phase 2 already delivers `sealed trait` + case classes (`11-pattern-matching/sealed-adt.scala`); this phase adds `enum` as the other spelling and the fixture that pins the absence of exhaustiveness checking (D4) |
| named and default arguments for Scala-defined methods | **Task 9** — parameter names and default blocks in `BytecodeModule`, `SEND_KW` extended to Scala callees, `CALL_KW` for function values, callee-side binding with three loud errors; eleven fixtures including source-order evaluation and the visible-failure binding fixture |
| **named arguments across the UMD boundary** (maintainer addition, A0-12) | **Task 11** — the convention recorded as settled, the Scala side exercised end to end against a stand-in foreign callee (three fixtures), the interning trap pinned by a unit test, two `XFAIL` fixtures for the real `import py.…` call, and `docs/INTEROP.md` documenting it. **The foreign half is not exercised and the plan says so** |
| extension methods | **Task 10** — `tests/conformance/22-super-and-extensions/extension-*.scala`, eight fixtures (seven if Phase 3 has not landed), including the one that closes Phase 3's custom-interpolator restriction |
| templates nested in an `object` | **Task 7** — `tests/conformance/03-definitions/nested-*.scala`, six fixtures, with class-nested templates still rejected (D79) |
| its tutorial chapters are written (Documentation track) | **Task 13** — chapters 11 and 12, with a fixture per runnable snippet, plus chapters 2, 3, 5, 6 and 13 extended |
| STATUS.md is updated and the full suite is green | **Task 14**, in four configurations |
| (implied by P5 and the Global Constraints) no performance regression | **Task 12** — the workload suite and the actor suite re-run against the Preflight baseline, with `perf stat -r 3` as the gate if anything moved |

**DESIGN clause by clause:** §1.1's P1 is honoured by A0-2's per-frame re-rooting and by keeping every payload in a traced slot; P2 by a `catch`/`finally` body running in the *same* frame and context as its `try`; P3 by needing nothing from protoCore; P4 by D71–D89 and the three retirements; P5 by Task 12; P6 by adding no stop-the-world work. §3.5's `96..127` exception band gets `THROW` and `RETHROW`, and the `80..95` object-model band gets `CALL_KW`, with the `128..159` actor band untouched. §3.6's recursive VM stays recursive — `runFrame` wraps `runLoop`, it does not replace it. §4.4 gains `super[T].m` (A0-8). §4.5's "`sealed trait` / `enum` produce a prototype and one child per case; exhaustiveness is not checked" is implemented exactly, with the non-exhaustive-match fixture pinning the second half. §4.6's `ProtoTuple` prohibition is honoured everywhere and the Global Constraints enumerate the sites. §5.2's named-and-default mechanism is implemented as written, with A0-12 adding the key convention DESIGN did not state. §5.3's pattern cascade is **reused** for `catch` rather than reimplemented. §7 is implemented and amended by Task 14 Step 5 to record A0-2, A0-3, A0-4 and A0-6. §8.3 is preserved and amended to record A0-7. §9's UMD is untouched, and Task 11 Step 5 writes the named-argument convention into `docs/INTEROP.md` for Phase 6. §10's testing strategy is followed throughout (unit, conformance in both syntaxes, CLI, GC pressure, self-reporting benchmarks, TDD — every task writes its fixtures first). §11's R3 is why `case e: ArithmeticException` uses `TEST_PROTO` markers and not protoCore's `isInstanceOf`; R5 is why the actor unit tests stay in their own binary.

**Gaps found while reviewing this plan, and closed inline:**

- **Putting the handler search inside `runLoop`'s catch would have broken Phase 5 silently.** Found by reading `ExecutionEngine.cpp:917-927` against DESIGN §8.3 rather than by testing. Closed by A0-3's retry loop and stated as Q3, the decision most worth the maintainer's confirmation.
- **`resumeFrames` calls `runLoop`, not `runFrame`** (`:548`). Left unchanged, a resumed frame would have no handler table and `try { f.await } catch { … }` would catch nothing. Closed by Task 1 Step 6, with a comment at the call site and an A0-7 invariant naming it.
- **`std::out_of_range` and `std::invalid_argument` derive from `std::logic_error`**, which A0-6 makes deliberately uncatchable. A naive `catch (const std::logic_error&)` clause would have made every VM defect catchable by `case e: Throwable`. Closed by catching the two by their exact types and never adding a `logic_error` clause, pinned by a unit test.
- **`ScalaThrow` deriving from `std::runtime_error` would make `runLoop` rewrite every Scala throw into a `RuntimeException`.** Closed by deriving from `std::exception` and pinning it with a `static_assert` plus a throw/catch test, because the failure would be silent and total.
- **`finally` must not run when a frame suspends.** Nothing in DESIGN says so, and protoST's spec explicitly requires the opposite bookkeeping for *its* handler stack. Closed by A0-7 invariant 2, which falls out of A0-1's choice, and pinned by `finally-does-not-run-on-suspension.scala`.
- **A failed `await` must raise at its call site, not return.** D50 records the Phase 5 behaviour as a known gap; nothing in DESIGN says how to close it. Closed by A0-7 invariant 5 and `runFrameRaising`, using the pc convention A0-3 fixes, and recorded as D50's retirement.
- **`foldLeft`-style curried natives, `Try`'s payload and `Priority`'s type all straddle Phase 3 and Phase 4.** Closed by the ordering notes below rather than by assuming an order.
- **`enum` companions are verbose to build as raw AST.** Closed by generating source text and parsing it with a nested `Parser` — the same technique Task 2 uses for `catch` clauses — so generated code cannot drift from what a user could write.
- **Named arguments toward a foreign callee had no home in any phase.** Raised by the maintainer and settled by them; closed by A0-12 and Task 11, including the `createSymbol`-not-`fromUTF8String` trap, its two fixtures, the unit test, the perenniality rationale, and an explicit statement of which half cannot be exercised until UMD lands.
- **`docs/LANGUAGE.md` §3 assigns multiple constructor parameter lists to Phase 4 and ROADMAP's done-when omits them.** **Ruled 2026-09-23 in favour of LANGUAGE**: A0-14 delivers them, D83 records the flattening, and Task 14 Steps 2 and 4 correct ROADMAP's done-when and STATUS so all three documents agree.
- **Binding named arguments in the callee needed a reason, not an excuse.** The maintainer supplied it — "the platform is late-binding even when Scala is not" — and A0-11 and DESIGN §5.2 now record it as a platform principle, with the unchanged bar that every error names what was expected and what arrived.
- Re-triaged the whole of Task 0 against the maintainer's cost-proportional rule (*follow Scala where matching is cheap; document the divergence where it is expensive and obscure*). **Four items stay escalated and each qualifies on the line the maintainer drew**, not on taste: E1 and E2 touch the GC and threading protocols, E3 is the actor/`await` contract whose breakage is silent, and E4 (global extension methods) is a public surface a common Scala idiom depends on. Ten items moved to decided-on-cost with the cost stated, four are recorded as ruled. The one borderline case is called out rather than buried: class-nested templates (C6) are a reasonably common idiom, so Task 14 Step 4 records them in ROADMAP as a candidate for a later phase instead of leaving them as a deviation alone.
- **`docs/STATUS.md` lists "Nested, local and anonymous classes (`new T { … }`) — Q6; nested templates inside objects arrive in Phase 4 with `enum`"** — which is right, but it bundles three features under one line. Task 14 Step 2 splits it: nested-in-an-`object` is delivered, nested-in-a-`class`, local and anonymous classes are not (D79).

**Ordering constraints with Phase 3:**

1. **`Failure`'s payload is the one hard coupling.** Phase 3's A0-13 requires Phase 3 to extend `Try` **without** re-pointing `Failure` off `RuntimeError`; Phase 4 Task 4 Step 2 does the migration and retires D44. If Phase 4 runs **first**, Phase 3's Task 5 Step 4 must write `recover(f: Throwable => B)` from the start and A0-13 becomes a no-op. Either order works; both phases must not both try to own the migration.
2. **`Priority` as an `enum`** (Task 5 Step 5) requires Task 8. Within Phase 4, Task 5 Step 5 is therefore the only step that must run out of task order — do it after Task 8, or skip it and leave D52 open with a note.
3. **The custom-interpolator restriction** (Phase 3's A0-3) is closed by Phase 4 Task 10 Step 2, which needs `StringContext` and the interpolation desugarer to exist. If Phase 3 has not landed, **skip that step**; do not add a `StringContext` class nothing produces.
4. **The benchmark baseline.** Phase 4 adds a per-frame C++ `try` region and Phase 3 changes the arithmetic fast paths; each phase's Task 12/Task 1 comparison must be measured against **its own** dated Preflight baseline. The two must not both claim "no regression" against the same undated numbers.
5. **Nothing else couples.** Exceptions, `super[T].m`, `enum`, nested templates, named/default arguments and extension methods need nothing from Phase 3's collections, string interpolation or fast paths; Phase 3's `Map`, `Set`, `Vector`, `Range`, `List` surface and interpolation need nothing from Phase 4 — with one convenience: once Phase 4 has landed, Phase 3's `Map`/`List` natives could raise typed exceptions directly instead of `ScalaError`, which is a simplification and never a requirement.
6. **Both phases must re-check their deviation-id range** against `docs/STATUS.md` before writing ids, because a by-name-parameters change in flight on `main` claims ids from D53 and takes opcode 38.
