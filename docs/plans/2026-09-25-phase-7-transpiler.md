# Phase 7 — The C++ transpiler (`protoscalac`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **This plan is written, not executed.** A sibling agent is working in **protoPython** as this file is created. Nothing in protoPython may be read for edit, written, built or committed by the executor of this plan; `protoPython/docs/PROTOPYC_SPECIFICATION.md`, `protoPython/src/compiler/ProtopycMain.cpp`, `protoPython/src/compiler/CppGenerator.cpp` and `protoPython/src/library/CompiledModuleProvider.cpp` are **read-only reference material** and are cited here so that the executor never needs to open protoPython at all. Task 1 Step 1 is the gate.

**Goal:** `protoscalac`, a transpiler that turns protoScala source into C++ which calls protoScala's runtime and protoCore directly, compiled into a shared library that loads as a UMD module — the same artefact `protopyc` produces for Python, and the same artefact a hand-written C++ UMD module is. With this phase in place, **a UMD module can be produced in C++, in Python or in Scala, and the producing language becomes an implementation detail of the module**: three toolchains, one `.so`, one `dlopen`, one `proto_module_init`, one entry in the provider registry. What the phase adds to the family is not speed and not start-up: it is that **a protoScala function becomes a `proto::ProtoMethod`**, and a `proto::ProtoMethod` is callable by any runtime in the family, which protoScala bytecode is not.

**This is not a performance feature.** protoScala's positioning is an *agile, interoperable, easily integrable and very simple* Scala — **explicitly not a fast one** (memory: protoScala positioning). The generated C++ calls the runtime dynamically, exactly as `protopyc`'s generated C++ calls `PythonEnvironment` dynamically: `a + b` still goes through protoScala's numeric semantics, `x.foo(y)` still walks the prototype chain through `ExecutionEngine::send`, and a field read is still `getAttribute` on an interned symbol. **The transpiler removes the front end and the dispatch loop's `switch`; it removes neither dynamic dispatch nor the cost of a send, and it does not make arithmetic faster.** Anyone reading "transpiler to C++" as "protoScala gets fast" has read this phase backwards. If a measurement in Task 15 shows a large speed-up on a benchmark, that is a finding to explain, not a goal that was met — and a variant that buys speed by fragmenting protoCore's conceptual model is rejected under P5 regardless of what it measures.

**Cold start is already solved and is not the justification.** Phase 6 measured the start-up split and the precompiled prelude image already took the large part of it:

| Stage | Share of start-up | Who removed it |
|---|---|---|
| parse | **59.6 %** | the prelude image (Phase 6) |
| desugar | **2.6 %** | the prelude image (Phase 6) |
| compile | **22.5 %** | the prelude image (Phase 6) |
| `linkSymbols` | **7.7 %** | nobody — needs a protoCore space image (escalation E5) |
| run the compiled prelude | **4.0 %** | nobody |

Phase 6 met DESIGN §1's budget at **23.73 ms** for a script and **23.89 ms** for the REPL against a 25 ms target, and proved the image moved the number by measuring the same binary with `PROTOSCALA_PRELUDE_NO_IMAGE=1` at 25.65 / 26.01 ms. **The 82 % that parse + desugar + compile represent is already gone.** What a transpiled *user program* additionally removes is its own parse and compile — which for a one-file script is small and for the prelude is already zero. The remaining protoScala-side headroom is the **4.0 %** that running the compiled prelude costs, and `linkSymbols`' 7.7 % is a protoCore item this phase does not touch. **Nobody may re-justify this phase on start-up.** Task 15 measures start-up anyway, and its acceptance criterion is *no regression*, not an improvement.

**What it actually unlocks is cross-runtime calls.** Track Y proved that values cross a runtime boundary **without copying** — one cell, two `ProtoSpace`s, the same address and the same `getHash` printed from both sides by `umd/protost-interop` — and it proved just as clearly that a foreign runtime **cannot call** a protoST method, because a protoST method is `__bc_ptr__` plus protoST's own engine, not a `proto::ProtoMethod`. protoScala has exactly the same shape: a compiled function is an object carrying `__code__` (a `SmallInteger` holding a `BytecodeModule` address) and `__captures__`, and only `ExecutionEngine::execute` can run it. **A transpiled module's functions are `proto::ProtoMethod`s** — ordinary C function pointers installed with `ctx->fromMethod(nullptr, fn)` and reached through `ProtoObject::asMethod` — so any runtime that holds the object can call it with no knowledge of protoScala. That is the capability this phase adds, it is the reason it comes before the remaining Track Y work, and Task 10 Step 6 demonstrates it with a caller that links **no protoScala header**.

Two limits of that claim, stated here so nobody infers more (the project's standing rule after Track Y):

- Callable **within one `ProtoSpace`**. A `proto::ProtoMethod` is a raw code pointer; the arguments and result are cells, and cells belong to the space that allocated them. A caller in another space can hold the method object (Track Y's no-copy property) and can call it, but the call allocates in the *caller's* context, which is what `ProtoContext* ctx` is for — and the method's body reads protoScala's `RuntimeLayout`, which is per-space. So a cross-**space** call needs the module's host runtime to be reachable from that space, which is R5's open question and **not** resolved here. Task 10 Step 6 demonstrates the same-space case and records the cross-space case as still undemonstrated.
- Callable **without protoScala's front end**, not **without protoScala's runtime**. See §D2.

**It does not gate the libtorch demonstration.** A hand-written C++ UMD module that wraps an external library needs Phase 6's `dlopen` provider loading and nothing from this phase: it defines `proto_module_init`, it links whatever it likes, and `CompiledModuleProvider` (Task 10) loads it. That demonstration can be built before, during or after this phase, by a different person, and **must not be sequenced behind it**. It is out of scope here, and the only thing this phase owes it is the provider that loads it — which is Task 10, and which is written so that a module it did not generate works unchanged.

---

## Architecture

Six artefacts. Two are new build products, three are new source components, one is a document.

- **`libprotoScala.so`** (`SOVERSION 1`) — protoScala's runtime, today five `STATIC` libraries linked only into the `protoscala` executable, published as **one shared library with one installed facade header**. This is the phase's enabling change and §D3 argues it. A generated module's link line is `-lprotoScala -lprotoCore`, exactly as `protopyc`'s generated Makefile writes `-lprotoPython -lprotoCore`, so `ldd module.so` tells the truth about what loading it costs.
- **`include/protoScala/GeneratedModule.h`** — the *entire* installed C++ surface generated code may use: the frame type, the static-table record types, and one free function per runtime operation the generator emits. One file, so the ABI is bounded and reviewable. Generated code includes this and `protoCore.h`, nothing else.
- **`src/runtime/OpcodeOps.h`** — the opcode bodies as `inline` functions, called by **both** `ExecutionEngine::runLoop` and generated code. This is the phase's correctness spine: there is **one** implementation of `ADD`, of `SEND`, of `UNCONS`, and the transpiler cannot drift from the interpreter because there is nothing to drift from. §D1 argues it and Task 3 carries the cycle gate that protects the dispatch loop's code layout (memory: micro-opts can hurt icache layout).
- **`protoscalac`** (`src/compiler/TranspilerMain.cpp`, `src/compiler/CppEmitter.{h,cpp}`) — the command-line tool. It runs protoScala's own lexer, parser, desugarer and compiler, then emits C++ from the resulting `BytecodeModule` tree instead of running it. §D1 argues the input choice.
- **`CompiledModuleProvider`** (`src/umd/CompiledModuleProvider.{h,cpp}`) — alias `compiled`, GUID `protoScala-compiled-v1`. A `proto::ModuleProvider` that `dlopen`s a `.so`, looks up `proto_module_init`, registers the module under its `proto::ModuleIdentity` and roots it with `ProtoSpace::addModuleRoot`. It loads any conforming module, generated or hand-written.
- **`docs/PROTOSCALAC_SPECIFICATION.md`** — the companion specification, in the shape `PROTOPYC_SPECIFICATION.md` has for `protopyc`. Task 16 writes it and §D10 fixes its location and contents.

**How it fits together.** `protoscalac foo.scala --build-so` runs `tokenize` → `Parser::parseCompilationUnit` → `desugarModule` → `Compiler::compileUnit`, producing a `CompiledUnit` whose `BytecodeModule` tree is exactly what the interpreter would have run. `CppEmitter` then walks that tree and writes `foo.cpp`: one static table block per module (constants, handler table, capture specs, class specs, exports), one `static const proto::ProtoObject* blk<N>(...)` function per `BytecodeModule` block, and `extern "C" void* proto_module_init()`. Each generated function reproduces `ExecutionEngine::execute`'s prologue — a child `ProtoContext` with `arity + localCount + maxStack + 1` automatic locals — and then the block's bytecode, with the operand stack living in those traced slots (P1) and `ip` resolved at emit time into C++ labels and `goto`s. Every opcode body is a call into `protoScala::gen::`, which forwards to the same `inline` function `runLoop` calls.

**One structural difference from `protopyc` that shapes the whole design.** `protopyc`'s generated code opens with `PythonEnvironment::getCurrentContext()` and `PythonEnvironment::get(ctx)` — a single facade object with a thread-local current context. **protoScala has neither.** Its equivalent is split in three by design: `RuntimeLayout` is a plain struct of ~110 public fields (every prototype, every interned key) rather than a class of getters; `Runtime` builds and pins it; `ExecutionEngine` holds the behaviour. And `getCurrentContext` **does not exist anywhere in `protoScala/src/`** — a `proto::ProtoContext*` is an explicit parameter of every engine method, every `Values.h` function and every native, and what is thread-local is instead the `ActiveCallContext { ExecutionEngine* engine; const RuntimeLayout* layout; }` that `activeCallContext()` returns and `ActiveCallGuard` installs. So generated code threads `ctx` through every function, never looks a context up mid-body, and reaches the engine and the layout the way every existing native does. `gen::currentContext()` exists for exactly one caller — `proto_module_init`, which is entered across a `dlopen` boundary with no `ctx` argument — and it throws `std::logic_error` when no call context is installed rather than inventing one.

## Tech Stack

C++20 (no extensions), CMake ≥ **3.20** (protoScala's declared floor), protoCore **2.5.0** / `PROTOCORE_ABI_SOVERSION 3` (2.4.0 when this plan was written; take the version from the configure output rather than from here — protoScala pins the soversion and hard-errors on a mismatch; its declared *version* floor is still `2.1.0`), GoogleTest 1.14 via the existing `FetchContent` block with `INSTALL_GTEST OFF`, GNU `make` (the generated build file, as `protopyc` does), `bash` for the differential harness (the existing `tests/conformance/run.sh` idiom), `perf stat -r 3` for the Task 3 cycle gate, CPack DEB/TGZ for Task 14. No new third-party dependency.

## Spec

This plan is the spec of record until **Task 16** writes `protoScala/docs/PROTOSCALAC_SPECIFICATION.md`. Normative sources this plan cites and must not contradict:

- `protoScala/docs/DESIGN.md` §1.1 (P1–P6), §3.1–§3.6 (the pipeline), §4.6 (`ProtoTuple`), §9 (UMD).
- `protoScala/docs/INTEROP.md` §2, §3, §3.1 (`protoScala-provider-1`), §6, §6.1, §7.
- `protoScala/docs/STATUS.md` — the deviation register; highest id in use is **D102**, so this phase starts at **D103**.
- `protoScala/docs/plans/2026-09-24-phase-6-umd-packaging.md` — the boundary catch shape, the prelude image, the start-up profile.
- `protoScala/docs/plans/2026-09-24-phase-p3-global-interning.md` §D11 — module identity is provider GUID + logical path + version; the unversioned value is the **empty string**.
- `protoCore/docs/MemoryModel.md` — the process sizing rule, quoted in Global Constraints.
- `protoCore/docs/GarbageCollector.md` §7 and Phase 1–4 — the participation obligations the generated code must honour.
- `protoScala/docs/plans/2026-09-25-phase-p4-embedder-conformance.md` — rules 1, 2, 3, 4, 5 and 12 apply to **generated code** as much as to hand-written code; Task 6 Step 8 and Task 13 Step 4 are where they are enforced.

---

## Global Constraints

- **Workspace safety.** Create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. **No `/tmp`** — scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/`. No `cmake --install` outside a scratch prefix: when an installed-package path must be exercised, install into `$SCRATCH/prefix` with `-DCMAKE_INSTALL_PREFIX`, **never** `/usr/local` and never any system prefix. `perf stat` only, never `perf record`. No `rm -rf` outside that scratch directory and the build directories.
- **Stay out of protoPython.** A sibling agent is working there. `protoPython/` is read-only reference; the executor does not need to open it, because every fact this plan takes from it is quoted here. Do not build it, do not run its tests, do not commit in it.
- **Principles P1–P6 are binding** (DESIGN §1.1), and P1 is the one this phase can break most easily:
  - **P1 — values live in protoCore structures.** Every value the GC must see lives in a `ProtoContext` slot or in a protoCore structure reachable from one. **Generated code may not hold a `const proto::ProtoObject*` in a C++ local across an allocation.** This is not advice: the operand stack of a generated function lives in the frame's automatic locals, an in-flight exception value lives in a reserved slot, and the emitter has no expression-temporary mechanism at all. Task 6 Step 2 fixes the slot layout that makes this structural rather than a rule to remember.
  - **P2 — one `ProtoContext` per invocation**, chained through `previous`. Every generated function opens exactly one `proto::ProtoContext frame(ctx->space, ctx)`.
  - **P3 — a missing capability extends protoCore**, preferably as a new type, never by changing an existing model. This phase needs nothing from protoCore: `ModuleIdentity`, `registerModule`, `findModule` and `addModuleRoot` all shipped in 2.2.0 and are present in 2.4.0.
  - **P4 — every deviation is documented** with a stable `D<n>` in STATUS.md. This phase records **D103–D107** (Task 17 Step 2).
  - **P5 — purity over performance.** A generated-code variant that fragments protoCore's or protoScala's conceptual model for a measured win is rejected (memory: purity > performance). The model this phase must keep intact: *one semantics, two consumers*. A second implementation of an opcode inside the emitter is the failure mode to avoid.
  - **P6 — no mutable-heap GC premises.** Generated code adds no stop-the-world work.
- **Never map transient data to `ProtoTuple`.** DESIGN §4.6, verbatim: *"protoCore interns every `ProtoTuple` node and interned tuples are perennial (`core/ProtoTuple.cpp:214`, `core/ProtoSpace.cpp:449`): each transient `(a, b)` would stay alive for the life of the process. `TupleN` are therefore modelled as they are in Scala — case classes `Tuple2(_1, _2)` … — with a specialised constructor primitive."* A `ProtoTuple` used for a transient is a leak no reclamation metric can see (P4 rule 5). Generated code builds no `ProtoTuple`; the static tables are plain C arrays of PODs and C strings, and the operand stack is automatic locals. The only tuples a generated module creates are Scala `TupleN` **case-class instances** through `gen::makeTuple`, which are ordinary objects, not `proto::ProtoTuple` — the same distinction `BytecodeModule::Handler` already relies on.
- **The generated code is an embedder and obeys the embedder rules.** Specifically: `JUMP_BACK` emits `frame.safepoint()` (P4 rule 1 — an unsubmitted young chain is live by construction and reclaims nothing while looking healthy); attribute keys come from `ProtoString::createSymbol`, never `fromUTF8String` (P4 rule 4 — `getOwnAttributeDirect` silently misses a non-symbol key, and ≤ 6 ASCII bytes match by accident so short names hide the bug); and no generated function holds a `ProtoObject*` in a C++ local across an allocation (P4 rule 3). Task 13 Step 4 runs protoCore's static checker over the *generated* output, not only over the emitter.
- **The process sizing rule.** `protoCore/docs/MemoryModel.md` §1, verbatim:

  > **The working set of a process is the size of its perennials, plus the sum of every `ProtoSpace`'s *peak* working set, plus all memory not managed by protoCore.**

  ```text
  process working set  =  perennials
                        + Σ  peak(ProtoSpace_i)        over every live space
                        + memory not managed by protoCore
  ```

  followed immediately by *"If that much memory is not available, the process does not fit. The answer is a larger machine, not a cleverer allocator."* A module that drags a runtime adds a space, and therefore a term. This is why §D2's question — can a module be emitted protoCore-pure? — matters, and why its answer must be stated rather than assumed. The answer this plan recommends is **no, and the honest form of "nearly" is a visible `libprotoScala.so` in the module's `NEEDED` list**.
- **Dual-audience tutorial rule.** Every phase extends `docs/TUTORIAL.md` and `docs/tutorial/` for both audiences (traditional Scala programmers, and developers arriving from Python or JavaScript), chapter 2 (the Python/JavaScript bridge) and chapter 3 (departures keyed to the D-ids) included, and **every runnable snippet has a fixture in `tests/conformance/tutorial/`**. Task 17 Step 3 does it, and a snippet without a fixture is not delivered.
- **Baselines, and a clean rebuild.** Before and after:

  | Project | Baseline | Build discipline |
  |---|---|---|
  | protoCore | **499/499** ctest, version **2.5.0**, `PROTOCORE_ABI_SOVERSION 3` | unchanged by this phase; `-j4` maximum if rebuilt |
  | protoScala | **1344** ctest cases, 0 failed, 7 skipped | `build_release/`, `-j4` maximum |

  Both rows re-measured from clean on **2026-09-25** against protoCore `df8406a3`;
  they superseded 488/488 at 2.4.0 and 1263/1263 within a day. **Do not trust
  either number on sight — take the baseline from
  `ctest --test-dir build_release -N | tail -1` and a green run at the moment the
  phase starts**, and record it. protoScala's total moves with protoCore's rule
  list as well as with its own fixtures: 15 → 16 embedder-conformance cases is the
  whole of the 1343 → 1344 step, from protoCore 2.5.0's new `mutable.graph_cycles`.

  "From clean" means a fresh build directory. This phase does not change protoCore, so no sibling embedder needs rebuilding — but Task 2 changes protoScala's own link structure from static to shared, which is exactly the shape that produces the ABI-mismatch class of crash against a stale object file (memory: ABI mismatch crashes stale binaries). **`rm -rf build_release` before the first build of Task 2 and after it**, path checked.
- **`ctest` is run with stdin at EOF, everywhere**: every invocation in this plan appends `< /dev/null`. protoScala's REPL CLI tests block forever on an open stdin, and a harness that hangs is indistinguishable from a harness that found a hang.
- **Every new test must be shown to fail.** Task 12 is the mutation matrix and it is a gate, not a report: a differential case that has not been seen red under a named mutation of the generator is not implemented. A test that cannot fail is the failure this project has now caught five times.
- **Benchmarks self-report** the work they did and the runner verifies it (memory: benchmarks must self-report; silent bench failures fooled the harness). Task 15's transpiled benchmarks print the computed result and the harness asserts the value, never the exit code.
- **Opcodes.** The highest opcode in use is **`RETHROW = 97`**; the bands are general `0–39` (lowest free **40**), object model `64–80` (lowest free **81**) and exceptions `96–97` (lowest free **98**). **This phase consumes no opcode**: it is a second consumer of the existing instruction set, not an extension of it. Task 1 Step 4 re-checks this against `src/compiler/Opcodes.h` rather than trusting this paragraph, and Task 13 Step 2 makes the emitter fail loudly on an opcode it does not know, so a future opcode cannot be silently dropped.
- **Deviation ids start at D103.** `docs/STATUS.md`'s highest id in use is **D102** (Track F, file I/O). Task 1 Step 3 re-derives the floor from the file rather than trusting this paragraph.
- **Git:** work on branch `feature/transpiler-phase7`. Never push. Stage explicitly by path (`git add <file> <file>`), never `git add -A` or `git add .`. Commit with the repository's configured identity (name **"Gustavo Marino"**; never "Gustavo Adrian Marino"; never override `user.email`). End every commit message with:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```
- All code comments, documentation, commit messages and error strings in **professional English**, regardless of the language of the request.

---

## Task 0: Maintainer decisions

Ten decisions, each with options and a recommendation. **These are recommendations, not agent decisions.** §D1, §D2 and §D3 change the shape of every later task and must be answered before Task 2; the rest may be answered before the task that needs them. §D1 is the maintainer's "what does the transpiler consume"; §D2 is "can a module be protoCore-pure"; §D4 is module identity; §D5 is language coverage; §D6 is where the boundary catch template goes.

### D1 — What the transpiler consumes — **Recommendation: (c) the compiled `BytecodeModule` tree, with every opcode body extracted into `inline` functions that the interpreter and the generated code both call**

**(a) The parser's AST.** `Parser::parseCompilationUnit()` returns `std::unique_ptr<CompilationUnit>`; the emitter walks `Node`s and emits C++ expressions. This is `protopyc`'s shape (`CppGenerator::generateNode` dispatches on `dynamic_cast<ConstantNode*>`, `NameNode*`, `CallNode*`, … over ~45 node types).
- **Rejected, and this is the decision the plan most wants argued.** protoScala's semantics are not in the AST; they are in the *lowering*. Between the AST and the bytecode sit `desugar`, `desugarModule`, `Linearizer` (Scala trait linearization, unit-tested against `scalac`-documented examples), `CompilePatterns` (every DESIGN §5.3 pattern), `CompileTemplates` (classes, traits, objects, companions, case-class synthesis, `copy`, `Product`), `ClassInfo`/`GlobalTable` (binding, private-member key qualification D5, forward-reference rules), `CaptureAnalysis` (by-name masks D47, boxing decisions), and default/keyword-argument binding. An AST-based emitter must reproduce all of it or call into it, and the parts it reproduces are a **second Scala semantics**. The project's own record says what happens then: the five deep bugs of 2026-09-23/24 were all *a documented rule an implementation did not follow*, and none failed loudly.
- It is also the most work by a wide margin: `protopyc`'s `CppGenerator.cpp` is **2002 lines** for Python, a language with no trait linearization, no case-class synthesis and no pattern matching of protoScala's breadth.

**(b) The desugared AST.** Same walk, after `desugar()`.
- Better than (a): the for-comprehension, string-interpolation, multiple-parameter-list and nested-template rewrites are already done. Still rejected: linearization, pattern compilation, template synthesis, scope/slot assignment, capture analysis and keyword binding are all *after* desugar, in the compiler. (b) buys the cheapest third of the problem and leaves the expensive two thirds.

**(c) The compiled `BytecodeModule` tree. — RECOMMENDED**
- The entire front end is reused **unchanged**: `tokenize(source)` → `Parser(toks).parseCompilationUnit()` → `desugarModule(unit, objectName)` → `Compiler(globals).compileUnit(unit, UnitMode::Script)`. Everything Scala-specific — linearization, patterns, templates, slots, captures, by-name masks, D5 key qualification, `@main` detection — is already decided, by the code the conformance suite validates (its size at the time of writing was 1263; see Step 2 for why that number is not to be relied on).
- The remaining job is the **54 opcodes actually defined** in `src/compiler/Opcodes.h` (the enum spans 0–97 with three reserved gaps). Of those, roughly 20 vanish into C++ (`PUSH_CONST` is a slot write, `JUMP`/`JUMP_IF_FALSE`/`JUMP_BACK` are labels and `goto`s, `POP`/`DUP` are `sp` arithmetic, `RETURN` is `return`); **only six** map onto an already-public `ExecutionEngine` member — `CALL`/`CALL_SPREAD`→`invoke`, `SEND`→`send`, `NEW`→`construct`, `FORCE`→`force`, with `callTopLevel`, `showTopLevel`, `materialise`, `resumeFrames` and `run` completing the public surface; and the rest need a shared body. **The useful half of `ExecutionEngine` is `private`** — `dispatch`, `callMember`, `callWithReceiver`, `bindMethod`, `forceMember`, `instantiate`, `makeClass`, `makeTuple`, `superSend`, `sendKeywords`, `callKeywords`, `bindKeywordsAndDefaults`, `testType`, `execute`, `callNative`, `slowBinary`, `siteName` and `throwMissingMember` all are — which is precisely why the extraction below is the mechanism rather than an optimisation: the alternative is promoting eighteen private members to public and enlarging the class's contract instead of narrowing it into a header of free functions. That is a small, enumerable surface with a total ordering for review.
- **It makes the differential harness meaningful rather than merely large.** With (a) or (b), a fixture that passes transpiled proves two independent Scala implementations agree today; with (c), it proves the *emitter* reproduced one implementation. The first is a coincidence detector, the second is a code-generator test.
- **It preserves debuggability.** `BytecodeModule::lineAt(pc)` is a parallel line table the compiler already fills, so the emitter writes `#line <lineAt(pc)> "<source>.scala"` and GDB, LLDB and compiler diagnostics point at Scala source lines — the same property `protopyc` gets from AST node lines.
- **Cost, stated plainly:** the opcode bodies live inside `ExecutionEngine::runLoop`'s `switch` today and must be extracted into `inline` functions in a new header. That is a refactor of a hot loop, and this project has already measured a case where a change that looked free cost **8 % of `sum_loop`'s cycles through code layout** (Phase 3 Task 1 Step 2, backed out under its own 3 % rule). Task 3 therefore carries the same gate: `perf stat -r 3` on the Phase 3 benchmark set before and after, and the extraction is backed out for any opcode whose extraction costs more than **3 %** of `fib`, `tak` or `sum_loop` cycles (memory: micro-opts can hurt icache layout).

**(d) The bytecode as static data, plus a generic C++ driver.** Generalise the precompiled prelude image: emit `PreludeConstRec`-style tables for the user module and let the existing `buildPreludeImage`-shaped reconstruction rebuild `BytecodeModule`s, which the ordinary interpreter then runs.
- **This is the cheapest option by a long way — roughly two days — and it is the one to reject explicitly**, because it is the obvious answer and it does not deliver the phase. `src/tools/PrecompileMain.cpp` (502 lines) and `src/runtime/PreludeImage.cpp` (193 lines) already do exactly this for `lib/prelude.scala`, with a format version and an FNV-1a-64 source-hash guard. Pointing it at a user file is a small generalisation.
- **But the module's functions stay bytecode-backed.** Nothing becomes a `proto::ProtoMethod`; `linkSymbols` still runs at load; `ExecutionEngine::execute` still runs every call. The framing point that justifies the phase — a foreign runtime can call a transpiled function — is **not delivered at all**. It also re-adds `linkSymbols`, which is the 7.7 % nobody has removed.
- **Partially adopted, and this is the shape to build.** (d) is the right mechanism for the module's *data* and the wrong one for its *code*. Task 5 reuses `PrecompileMain`'s emitters — refactored into a shared `src/compiler/CppTables.{h,cpp}` so there is one string-escaping, one `exactDouble`, one `PreludeConstRec` writer — for constants, handler tables, capture specs, class specs and the exports table. Task 6–8 emit the code as functions. **The prelude image and the transpiler become two users of one table emitter**, which is the elegant form.

**Recommendation: (c) for code, (d)'s emitter reused for data.** If the maintainer wants a two-day proof of the packaging path before committing to (c), (d) alone is a legitimate **spike** — but it must be labelled a spike and deleted, because shipping it would make "protoScala produces UMD modules" true and "a foreign runtime can call one" false, which is the misreading this plan exists to prevent.

### D2 — Can a module be emitted protoCore-pure? — **Recommendation: no, and do not offer a `--pure` emission mode. Offer the honest form instead: a module whose dependency on protoScala is visible in `ldd`, and a `--report-purity` analysis that says what a module needs and why**

The question matters because of the sizing rule: `protopyc`'s generated Makefile links `-lprotoPython -lprotoCore`, so a Python-produced module drags `PythonEnvironment`; each runtime owns its own `ProtoSpace`; and `protoCore/docs/MemoryModel.md` sizes a process as perennials + the **sum** of each space's peak working set + non-protoCore memory. Every dragged runtime is a term in that sum. A module that needed only protoCore would add no term.

**(a) A `--pure` mode that emits protoCore-only C++ for an eligible unit.**
- **Eligibility is empty for anything a Scala programmer would write, and the reason is not a missing feature — it is D1 and D2 of STATUS.md.** protoScala's `Int` *is* `Long` *is* `BigInt`, and `Double` *is* `Float`. `a + b` therefore means "SmallInteger fast path, promote to protoCore `LargeInteger` on overflow, `Double` if either side is one, `String` concatenation if the left side is a string, otherwise send `+` as a method" — and that is `src/runtime/Values.cpp`, protoScala code. A pure module would have to reimplement it. **A second implementation of arithmetic is the exact failure this phase's whole architecture (§D1) is designed to avoid**, and getting `2^54` wrong in the pure path while the interpreted path is right is the "wrong answer, no error" class.
- Beyond arithmetic: `println` is a prelude global; every `List`, `Option`, `Map`, `Vector`, `Range`, `Try`, `Either` and `TupleN` is a prelude type in `lib/prelude.scala`; string interpolation needs the prelude `StringContext`; `throw` needs the prelude `Throwable` hierarchy; `class`/`trait`/`object` need `makeClass` and the linearization the compiler computed. A unit that touches none of these is a unit that computes nothing and prints nothing.
- **The one genuinely pure case is the one that needs no transpiler.** A module that wraps an external C or C++ library — the libtorch case — touches no protoScala semantics and is protoCore-pure by construction. It is **hand-written C++**, and framing point 4 keeps it out of scope. The pure set and the transpiler's domain do not intersect.

**(b) A C ABI vtable (`ModuleHostV1`) fetched with one `dlsym`, so the module's link line is `-lprotoCore` only.**
- Superficially attractive: `ldd module.so` would show only `libprotoCore.so.3`, and any host able to produce a protoScala runtime could serve the module.
- **Rejected on two grounds.** First, it is cosmetic: the module still cannot run without protoScala, so the link line would be *misleading* rather than pure, and the failure would move from link time to `dlopen` time, or worse to the first call. Second, a hand-maintained struct of ~40 function pointers, versioned by hand, duplicating declarations that already exist in a header, is precisely the "parallel mechanism" P5 rejects — and the family's own precedent is the opposite: protoCore is a shared library with a `SOVERSION`, not a vtable.

**(c) Link the runtime and say so. — RECOMMENDED**
- The generated Makefile writes `LIBS = -lprotoScala -lprotoCore`, mirroring `protopyc`'s `-lprotoPython -lprotoCore`, and `-Wl,-rpath,<dir>` for every library directory so the module loads without `LD_LIBRARY_PATH`. `ldd module.so` then states the cost: this module brings a protoScala runtime, one `ProtoSpace`, one term in the sizing sum.
- **`--report-purity` is built, and `--pure` is not.** The analysis is cheap — a pass over the emitted opcode set plus the set of global keys and `ClassSpec` constants the unit references — and it produces the sentence a module author actually needs: *"this module needs protoScala because it uses `ADD` (protoScala numeric semantics, D1/D2), `PUSH_GLOBAL println` (prelude) and `MAKE_CLASS Point` (linearization)"*. It documents the sizing consequence at the place it is incurred and it costs about 80 lines. Emitting under `--pure` is deferred until a real eligible module exists; Task 13 Step 5 builds the analysis and two fixtures, one eligible and one not, so the classification itself is tested.
- **What determines eligibility, recorded for the day it is revisited:** a unit is protoCore-pure iff (i) its emitted opcode set is a subset of `{NOP, EXTEND, PUSH_CONST, PUSH_UNIT, PUSH_NULL, PUSH_TRUE, PUSH_FALSE, POP, DUP, PUSH_LOCAL, STORE_LOCAL, JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE, JUMP_BACK, RETURN}`, (ii) it references no `PUSH_GLOBAL`/`STORE_GLOBAL` key, (iii) it allocates no `ClassSpec`, and (iv) its constant pool holds no `Symbol`, `SendSite`, `SuperSite`, `KwSendSite` or `ClassSpec` entry. Conditions (i)–(iv) together mean: no arithmetic, no send, no class, no prelude. **`--report-purity` computes exactly this and names the first condition each ineligible unit violates.**

### D3 — Making protoScala's runtime loadable by a `.so` — **Recommendation: one shared library `libprotoScala.so` with `SOVERSION 1`, plus one installed facade header**

protoScala today builds five `STATIC` libraries (`protoscala_support`, `protoscala_frontend`, `protoscala_compiler`, `protoscala_runtime`, `protoscala_repl`) and links them into the `protoscala` executable. `install(TARGETS protoscala ...)` ships the executable, `LICENSE` and `README.md` — **no library and no header**. A generated module therefore has nothing to link against today, which is why this is a Task 0 decision and not an implementation detail.

**(a) Link the static libraries into every module.**
- Each `.so` gets a private copy of the runtime, **including a private copy of the precompiled prelude image** and a private `Runtime`/`RuntimeLayout`. Two modules in one process would hold two prelude images, two sets of prototypes and two `ProtoSpace` registrations, and the host's `List` would not be the module's `List`. Rejected — it is not a size problem, it is a correctness problem.

**(b) Leave the runtime static and resolve the module's undefined symbols from the host executable** (`ENABLE_EXPORTS` / `-rdynamic` on `protoscala`, `dlopen(RTLD_GLOBAL)`, `-Wl,--allow-shlib-undefined` on the module).
- Cheapest: no new library, no soname, no installed headers beyond the facade. Works, and `protopyc`-style modules would load.
- **Rejected.** The ABI becomes C++ name mangling with no version marker at all, so a module built against one protoScala and loaded into another fails at `dlopen` with an undefined-symbol name rather than a version message. And the module is loadable **only** by a `protoscala` executable — not by a test binary, not by a host that embeds protoScala, not by the Task 10 driver — which contradicts the phase's own goal that the artefact be a module, not an appendix to one binary.

**(c) One shared `libprotoScala.so`, `SOVERSION 1`, one installed facade header. — RECOMMENDED**
- It is what `protopyc` relies on (`-lprotoPython`), it is what protoCore itself is (`SOVERSION 3`), and it makes the dependency `ldd`-visible per §D2.
- The five static libraries stay as `OBJECT` libraries and are combined into one shared target, so the internal structure is unchanged and only the final link differs. The `protoscala` executable links the shared library, which also shrinks it.
- **The installed header surface is exactly one file**, `include/protoScala/GeneratedModule.h`. Nothing else is installed, so the published ABI is bounded, reviewable in one diff, and cannot grow by accident. Everything generated code needs is declared there; everything else stays in `src/`.
- **Cost:** a real ABI commitment for a 0.6 project, `-fPIC` on every source, a `SOVERSION` to maintain, and a package that now ships a library and a header. `PROTOSCALA_ABI_SOVERSION 1` is introduced and the rule recorded: it moves when `GeneratedModule.h` changes incompatibly, and only then.

**Open sub-question for the maintainer:** whether `protoscalac` itself should be installed. Recommendation **yes** — a transpiler no user can run is a build tool, and Task 14 Step 3 installs it with the `protopyc`-style build-tree/installation path resolution. `protoscala-precompile` stays uninstalled, as it is today.

### D4 — Module identity and version — **Recommendation: the logical path comes from the provider's request, the version from a `--module-version` option defaulting to the empty string, and both are declared by two `extern "C"` accessors in the generated file**

P3 ruled the key: `providerGUID + '\x1F' + logicalPath + '\x1F' + version`, with `'\x1F'` as separator, the provider **GUID** rather than the alias, and **the empty string as the permanent, first-class version of a module that declares none** — not a wildcard, not a synonym for any declared version. The forward-compatibility rule is that an unversioned module's key is `G\x1FP\x1F` today and byte-identical after manifests exist, so introducing versions cannot re-alias any module that exists now. `proto::ModuleIdentity` and `ProtoSpace::registerModule` / `findModule` / `addModuleRoot` all shipped in protoCore 2.2.0 and are present in 2.4.0.

- **The provider GUID** is `CompiledModuleProvider`'s own: `protoScala-compiled-v1`. It is **not** the source provider's `protoScala-source-v1`, and that is correct under P3 — a compiled module and a source module of the same path are two modules. Task 10 Step 3 adds a fixture proving they do not alias.
- **The logical path** is the path the provider was asked for, not the file name. `tryLoad("util.Strings", ctx)` keys on `util.Strings` even if the file is `/opt/x/util/Strings.so`. This matches `ScalaModuleProvider` and keeps the two providers' keys comparable.
- **The version.** protoScala has no module manifest and **this phase does not invent one** — P3 said so explicitly and it remains out of scope. `protoscalac --module-version <v>` records a version; without it the version is the empty string and the generated accessor returns `""`. Two rejected alternatives, both because they would re-alias: a `// @version` source pragma (it would be a manifest, invented here, ungoverned) and defaulting to protoScala's own `PROJECT_VERSION` (every module built today would then key on `0.7.0`, and rebuilding under 0.8.0 would silently become a different module).
- **The accessors.** Every generated file defines:
  ```cpp
  extern "C" const char* proto_module_version_v1() { return ""; }
  extern "C" const char* proto_module_language_v1() { return "protoScala"; }
  ```
  `proto_module_version_v1` is **optional** for a hand-written module: `CompiledModuleProvider` treats its absence as the empty string, so a hand-written C++ module that defines only `proto_module_init` loads unchanged (framing point 4). `proto_module_language_v1` is diagnostic only and never part of the identity.
- **The honest consequence, recorded as D106:** the same `.so` loaded by protoScala's `compiled` provider and by another runtime's compiled provider has two different GUIDs and is therefore **two modules** in one process, with two module objects and two top-level runs. That is what P3's ruling means and it is correct; it is recorded so that nobody reads "one artefact" as "one instance".

### D5 — Language coverage of the first cut — **Recommendation: everything the compiler can lower, minus cooperative suspension; and every exclusion is a transpile-time refusal with a named message, never a mistranslation**

Because the input is bytecode (§D1), coverage is decided per **opcode**, not per language feature — which is why the list below can be exhaustive rather than hopeful. The emitter has a table of 98 entries and an entry that is not implemented **throws**; Task 13 Step 2 makes that structural.

| protoScala 0.6.0 feature | First cut | Why |
|---|---|---|
| `val`/`var`/`def`, `if`/`while`, lambdas, recursion, integer/`Double`/`String` arithmetic, `println` | **supported** | opcodes 2–39; `gen::` bodies shared with `runLoop` |
| classes, traits, objects, companions, case classes, linearization, `super`, `super[T].m` | **supported** | `MAKE_CLASS`/`NEW`/`INVOKE_INIT`/`SEND_SUPER`; the `ClassSpec` is static data, the linearization was computed by `Linearizer` at transpile time |
| pattern matching, all DESIGN §5.3 patterns | **supported** | `TEST_TYPE`/`TEST_PROTO`/`UNAPPLY_FIELDS`/`UNCONS`/`MATCH_ERROR`; `CompilePatterns` already lowered them |
| for-comprehensions, `List`, `Vector`, `Range`, `Map`, `Set`, `Option`, `Either`, `Try`, `TupleN` | **supported** | desugared to sends against the prelude, which the module reaches through the host runtime |
| string interpolation (`s`, `f`, `raw`, custom) | **supported** | `CONCAT` plus sends to the prelude `StringContext` |
| named and default arguments | **supported** | `SEND_KW`/`CALL_KW` and `BytecodeModule::setDefaultBlock`; the default block is an ordinary block and becomes an ordinary generated function |
| by-name parameters (`x: => T`) | **supported** | `MAKE_LAZY`/`FORCE`/`FORCE_THUNK`; the by-name masks were computed at transpile time |
| `enum`, sealed hierarchies | **supported** | desugared to case classes with `@Enum` as a parent; no opcode of its own |
| extension methods | **supported** | ordinary methods after desugar; D82 keeps them session-wide, which a module does not change |
| `try`/`catch`/`finally`/`throw`, native error translation | **supported** | Task 8; the handler table is static data and the retry loop is a faithful port of `runFrame` |
| file I/O (`scala.io.Source`, `FileIO`) | **supported** | ordinary native sends |
| `import` of a source module, of a compiled module, of a foreign module | **supported** | resolved at transpile time for types (`ModuleExports`) **and** re-performed at load time for values (Task 9 Step 4) |
| actors: `Actor.spawn`, `!`, `?`, `send`/`ask`, `Priority`, `value`, `Actor.stats` | **supported** | an actor handler is invoked through `ExecutionEngine::invoke`, which already calls native methods; a transpiled handler is a native method |
| futures: `Future.apply`, `map`, `flatMap`, `recover` | **supported** | ordinary sends |
| **`await` inside transpiled code** | **REFUSED at transpile time** | **D103.** Cooperative suspension snapshots a *bytecode* frame: `resumeFrames` rebuilds a frame from `__mod__`, `__ip__`, `__fbase__` and `__fslots__`, and `nativeReentryDepth()` already refuses to suspend above depth 1 (D43) because extra C++ frames cannot be snapshotted. A transpiled function has no `ip` to record and its C++ frame cannot be rebuilt. Refusing is the only honest first cut; mistranslating would produce a hang or a lost continuation. §D9 records what a later phase would have to build. |
| the REPL, `res0` echo, `:load` | **out of scope** | **D105.** `protoscalac` compiles files; `UnitMode::Repl` is not offered and `--emit-cpp` on REPL input is refused. The REPL keeps the interpreter. |
| `--disassemble` of a transpiled module | **out of scope** | there is no bytecode at run time to disassemble; `protoscalac --emit-cpp` plus the `#line` directives are the inspection path |

**The refusal mechanism, because "reject with a clear error" is the load-bearing half.** The emitter walks the whole `BytecodeModule` tree **before** writing any output and collects every unsupported construct; it then fails with every one of them reported as `<file>:<line>: error: <message>`, using `BytecodeModule::lineAt(pc)` for the position. It never emits a partial file and never emits a file with a `// TODO` in it. `await` is detected as a `SEND`/`SEND_APPLY` site whose `SendSite` name is `await` — an over-approximation (a user method named `await` is refused too), which is the safe direction and is recorded in the specification.

### D6 — Where the boundary catch template goes in generated code — **Recommendation: at exactly two places, both inside `gen::`, never in emitter output**

Phase 6 fixed the shape and the order is load-bearing. `src/umd/ForeignBoundary.h` carries it verbatim, and the six clauses are:

```cpp
catch (FutureYield&)            { throw; }   // a cooperative suspension, not an error
catch (ScalaThrow&)             { throw; }   // a Scala exception already in flight
catch (ScalaError&)             { throw; }   // a native throw site's own translation
catch (const std::logic_error&) { throw; }   // D74: a VM defect stays uncatchable
catch (const std::exception& e) { throw ScalaError("RuntimeException", e.what()); }
catch (...)                     { throw ScalaError("RuntimeException", "native exception"); }
```

`catch (const std::logic_error&) { throw; }` sits **before** the `std::exception` arm because `std::logic_error` derives from `std::exception`; without it, D74 — *a compiler or VM defect must never be maskable by `catch { case e: Throwable => }`* — **retires silently**. ROADMAP's prescribed five clauses do not mention it. `tests/unit/test_exceptions.cpp` has one case per clause.

**(a) The emitter writes the six clauses at every boundary site in the generated file.**
- **Rejected.** It is the template duplicated into machine output, where nobody diffs it, and a generator bug that drops the fourth clause is invisible. It would also be the first place in the repository where D74's protection exists in a file no human wrote.

**(b) Two `translateForeignException` call sites inside `libprotoScala.so`. — RECOMMENDED**
- **Site 1 — the module entry.** `gen::runModuleBody(...)` wraps the generated module body: `proto_module_init` is called by `CompiledModuleProvider` across a `dlopen` boundary, and an exception escaping a `dlopen`'d initializer must be translated exactly once, by the same template every other foreign entry uses.
- **Site 2 — the native-method entry.** `gen::enterMethod` wraps the block body. A transpiled function is a `proto::ProtoMethod` called through `ExecutionEngine::callNative`, so it is a foreign callable from the VM's point of view and must present the same boundary the VM's other native methods do. A constructor cannot wrap the code that follows it, so **each block is emitted as a thunk plus a body**: the thunk is the `proto::ProtoMethod` that goes into `BlockRec::blocks[]` and its whole body is one `return gen::enterMethod(&blk3_body, ctx, self, pl, args, kwargs);`. Task 6 Step 3 emits the pair, and the six clauses live inside `enterMethod`, in `libprotoScala.so`.
- Both sites are hand-written, reviewed C++ in `src/runtime/GeneratedSupport.cpp`, covered by the existing per-clause unit cases, and **the emitter writes no `catch` at all** except the retry-loop `catch` of Task 8, which catches `ScalaThrow`, `ScalaError` and `std::runtime_error` — the frame's own handler search plus the protoCore-error bridge `runLoop` already has — and lets everything else through. That is deliberately *not* the boundary template: a frame's handler search is not a boundary translation, and `std::logic_error` is not a `std::runtime_error`, so D74 holds there by construction. Task 8 Step 6 adds a unit case that a `std::logic_error` raised inside a generated frame is **not** caught by a Scala `catch { case e: Throwable => }`, which is D74 tested on the generated path.

**(c) One site only, at the module entry.**
- Rejected: a generated method called from the VM would then leak a raw `std::exception` into `runLoop`, which would translate it with the *interpreter's* wording. Two sites cost one function and keep the messages identical on both paths.

### D7 — Does `protoscalac` own a `Session`? — **Recommendation: yes**

A transpiled unit that writes `import util.Strings` needs the imported module's **types** at transpile time (`ModuleLoader::load` returns `ModuleExports` with `ClassInfo`s, because a Scala programmer imports types and `case Point(x, y) =>` must compile), and D90 says an import is resolved by *loading*, which runs the module's top level — which needs a `ProtoSpace`.

- **(a) `protoscalac` creates a `Session`. — RECOMMENDED.** Imports behave at transpile time exactly as they do in the interpreter, because it is the same `Session` that is the `ModuleLoader` and the `ModuleHost`. Cost: `protoscalac` links the runtime and creates a space, unlike `protoscala-precompile` which deliberately does neither. Accepted, and stated in the specification.
- **(b) A stub `ModuleLoader` that only parses the imported file.** Cheaper, no space — and wrong: it would diverge from D90 for any module with a side-effecting top level, and it is a second module-loading semantics.
- **(c) Refuse `import` in the first cut.** Rejected: it would exclude all 30 `24-modules` fixtures, all 19 `25-interop` fixtures and the chapter-15 tutorial fixtures from the differential harness — about **8 %** of the coverage, and the part that exercises the very mechanism this phase ships.

### D8 — Is a transpiled script with an `@main` a module? — **Recommendation: it is a module that additionally exports `proto_module_main`, and the provider refuses to import it**

D91 says a module may not define an `@main`, and `desugarModule` throws `ParseError` when it finds one — a silently ignored `@main` would be a trap. But **733 of the 859 conformance fixtures use `@main`**, so a differential harness that cannot run them covers nothing.

- **Recommended.** `protoscalac` has two modes. `--as-module` (the default when the file defines no `@main`) calls `desugarModule` and emits `proto_module_init` alone. `--as-script` (the default when the file defines an `@main`) calls `desugar` and emits both `proto_module_init` and:
  ```cpp
  extern "C" int proto_module_main(int argc, char** argv);
  ```
  `CompiledModuleProvider::tryLoad` **refuses** a `.so` that exports `proto_module_main`, with the message `"a module may not define an @main method"` — the same wording D91 already uses, so the existing fixture text matches on both paths. `protoscala --run-module` (Task 10) calls `proto_module_init` and then `proto_module_main` when present, which is what the differential harness drives.
- **Rejected: one mode that always emits `proto_module_main`.** It would make every transpiled module unimportable.
- **Rejected: refuse `@main` entirely.** It would cost 87 % of the harness's coverage to protect a rule that a one-line provider check already protects.

### D9 — Cooperative suspension in transpiled code — **Recommendation: out of scope, refused loudly, and the cost of doing it recorded**

`await` inside a transpiled function is refused (§D5, D103). Recorded for the phase that revisits it: making it work needs the generated function to be resumable, which means either (i) emitting each block as a state machine with an explicit program counter and every live slot in the frame — which `protopyc` already does for Python generators (`py_cont_*` plus a `switch (pc)` over `gi_pc`, with locals in `gi_locals`), so the shape is known and is a large piece of work; or (ii) a stackful coroutine per actor turn, which changes protoScala's threading model and is a DESIGN §8 decision, not a code-generator decision. **Option (i) is the one to cost when it is wanted**, and it is not wanted here: the 38 fixtures it would unlock are already green on the interpreted path, and the phase's purpose is callability, not concurrency.

### D10 — Where the companion specification lives — **Recommendation: `protoScala/docs/PROTOSCALAC_SPECIFICATION.md`**

- **Recommended:** `docs/PROTOSCALAC_SPECIFICATION.md`, mirroring `protoPython/docs/PROTOPYC_SPECIFICATION.md` exactly in location and in shape. A reader who knows one finds the other.
- **Rejected:** `docs/platform/TRANSPILER-SPEC.md`. `docs/platform/` holds protoCore specifications (`PROTOMAP-SPEC.md`, `PMQ-SPEC.md`, `CONFORMANCE-SPEC.md`) — specifications of *platform* work in another repository. The transpiler is a protoScala tool and changes no platform.
- Contents are fixed by Task 16 Step 2 and follow `PROTOPYC_SPECIFICATION.md`'s section order: command line; loading generated modules; debugging; generated code; **not implemented**. Three sections are added because protoScala needs them: **module identity and version** (§D4), **what a transpiled module needs and why** (§D2's purity report), and **the differential harness** as the specification's own conformance statement. The `Not implemented` section is mandatory and is written from §D5's refusal table, because `PROTOPYC_SPECIFICATION.md`'s own §5 exists precisely to retract an earlier specification that described features `protopyc` did not have.

---

## Task 1: Gate, baselines, and the numbers nobody may re-derive later

**Files:** `.agent_scratch/phase7-transpiler/baseline.md` (created), `docs/DECISIONS-LOG.md` (appended in Step 6).

**Interfaces:** none — this task runs commands and records outputs.

- [ ] **Step 1 — The gate.** Confirm the family is quiet and that nothing in protoPython is touched.
  ```bash
  cd /home/gamarino/Documentos/proyectos/protoScala && git status --porcelain
  cd /home/gamarino/Documentos/proyectos/protoCore  && git status --porcelain
  ```
  Both must print nothing. If either is dirty, stop and ask the maintainer. Then create the scratch directory and record that protoPython is out of bounds:
  ```bash
  mkdir -p /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler
  ```
- [ ] **Step 2 — Record the baselines.** protoScala from clean, protoCore as installed.
  ```bash
  cd /home/gamarino/Documentos/proyectos/protoScala
  rm -rf build_release && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release
  cmake --build build_release -j4
  ctest --test-dir build_release --output-on-failure < /dev/null 2>&1 | tail -5
  ```
  Append the `tests passed` line to `baseline.md`, and **take the total from the run rather than from this plan** — it has already moved twice (1104 in the Phase 6 plan, 1263 here, 1344 on 2026-09-25). What must hold is **0 failed**, with only the process-isolation skips. Record the composition, because a later count that moves must be attributable; as measured on 2026-09-25 against protoCore 2.5.0 it is **919** `conformance/<rel-path>.scala`, **370** GoogleTest cases, **24** `cli/*`, **16** `embedder-conformance/*`, **12** `benchmarks/*`, **2** explicit `unit/modules` and `unit/actors`, **1** `umd/protost-interop` = **1344**, 0 failed, 7 skipped. The `embedder-conformance` row is **not** protoScala's to hold still: it is parameterised over protoCore's rule list, and protoCore 2.5.0's new `mutable.graph_cycles` is the whole 1343 → 1344 step. Record protoCore's version and soversion from the configure output line `protoCore ... (SOVERSION 3)`; as of 2026-09-25 it is **2.5.0** and **3**.
- [ ] **Step 3 — Re-derive the deviation floor.** Do not trust this plan's `D103`.
  ```bash
  grep -oE '\bD[0-9]+\b' /home/gamarino/Documentos/proyectos/protoScala/docs/STATUS.md \
    | sed 's/D//' | sort -n | uniq | tail -1
  ```
  Record the result in `baseline.md` as `deviation_floor = D<n+1>`. If it is not `D103`, every `D10x` in this plan shifts by the same amount and Task 17 Step 2 uses the recorded value.
- [ ] **Step 4 — Re-derive the opcode floor, and confirm the phase needs none.**
  ```bash
  grep -nE '= *[0-9]+' /home/gamarino/Documentos/proyectos/protoScala/src/compiler/Opcodes.h
  ```
  Record the highest value in use and the lowest free value in each band. Expected: **54 opcodes defined**, highest **97** (`RETHROW`), lowest free **40** (general band), **81** (object-model band), **98** (exceptions band). Record also that the `128–159` range the header's comment reserves for actors (`SEND_ASYNC`, `ASK`, `AWAIT`) is **a comment only** — those identifiers exist nowhere in `src/`, actors and futures are entirely native methods, and the range is wholly unused. Record the sentence *"Phase 7 consumes no opcode: it adds a second consumer of the existing instruction set."* If a new opcode has appeared since this plan was written, Task 6 Step 9's exhaustiveness check will fail on it, which is the intended behaviour.
- [ ] **Step 5 — Record the start-up profile, measured now rather than quoted.** Run the Phase 6 start-up benchmark three rounds interleaved and record the script and REPL medians, then run the same binary with the image disabled:
  ```bash
  cd /home/gamarino/Documentos/proyectos/protoScala
  PROTOSCALA_PRELUDE_NO_IMAGE=0 ./build_release/protoscala --version
  ```
  Then run the repository's start-up benchmark as `benchmarks/RESULTS.md` documents it for 0.6.0, with and without `PROTOSCALA_PRELUDE_NO_IMAGE=1`, and record both numbers. Write into `baseline.md`, verbatim:

  > Phase 6 start-up split: parse 59.6 %, desugar 2.6 %, compile 22.5 %, `linkSymbols` 7.7 %, run 4.0 %. Budget met at 23.73 ms (script) / 23.89 ms (REPL) against 25 ms; the same binary with `PROTOSCALA_PRELUDE_NO_IMAGE=1` measures 25.65 / 26.01 ms. **The transpiler's remaining cold-start headroom is the 4.0 % that running the compiled prelude costs. This phase is not justified by start-up, and Task 15's criterion is no regression.**

- [ ] **Step 6 — Record the framing in `DECISIONS-LOG.md`.** Append a Phase 7 section with four numbered entries, each one sentence: (1) not a performance feature — the generated code dispatches dynamically; (2) cold start is already solved, the headroom is 4.0 %; (3) what it unlocks is cross-runtime calls, because a transpiled function is a `proto::ProtoMethod` and bytecode is not; (4) it does not gate the libtorch demonstration, which needs only Phase 6's `dlopen` provider loading. Mark the entry `[agent, pending review]`.

**Done when:** `cat /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/baseline.md` shows the measured green line (**0 failed**; the total is whatever that run reports, not a number from this plan), protoCore's measured version / SOVERSION 3, the deviation floor, the four opcode numbers and both start-up measurements; and `git -C /home/gamarino/Documentos/proyectos/protoScala diff --stat docs/DECISIONS-LOG.md` shows exactly one added section.

---

## Task 2: `libprotoScala.so` and the installed facade header

**Files:**
- `CMakeLists.txt` (modify) — `OBJECT` libraries, one shared target, `PROTOSCALA_ABI_SOVERSION`, install rules.
- `include/protoScala/GeneratedModule.h` (**create**) — the entire installed C++ surface.
- `src/runtime/GeneratedSupport.cpp` (**create**) — its implementation.
- `cmake/protoScalaConfig.cmake.in` (**create**), `cmake/protoScalaConfigVersion.cmake` (generated) — so a consumer can `find_package(protoScala 0.7 CONFIG)`.
- `tests/unit/test_generated_support.cpp` (**create**).

**Interfaces** — `include/protoScala/GeneratedModule.h` declares exactly the following and nothing else. `protoScala::gen` is the only namespace generated code names.

```cpp
#ifndef PROTOSCALA_GENERATED_MODULE_H
#define PROTOSCALA_GENERATED_MODULE_H

#include <protoCore.h>
#include <cstddef>
#include <cstdint>

namespace protoScala::gen {

/** Bumped only when this header changes incompatibly; equals PROTOSCALA_ABI_SOVERSION. */
inline constexpr std::uint32_t kGeneratedModuleABI = 1;

// --- static tables the generated file defines ------------------------------

/** One constant-pool entry, in the order the compiler created it. */
struct ConstRec {
    std::uint8_t kind;          // BytecodeModule::ConstKind
    long long ival;             // Int; Char code point
    double dval;                // Double
    int base;                   // BigInt
    std::uint32_t argc;         // SendSite/SuperSite argc; KwSendSite positional; ClassSpec parents
    std::uint32_t flags;        // ClassSpec flag bits
    bool exact;                 // SuperSite: super[T].m
    const char* sval;           // String bytes; BigInt digits; a site's name
    const char* key;            // ClassSpec type key; SuperSite owner key; SendSite fallback (D5)
    int namesFirst, namesCount; // Names; ClassSpec members; KwSendSite keywords
    int fieldsFirst, fieldsCount;
};

struct HandlerRec {
    std::uint32_t startPc, endPc, handlerPc;
    int stackDepth, slot;
    std::uint8_t kind;          // BytecodeModule::HandlerKind
};

/** Everything one generated block needs that is not code. */
struct BlockRec {
    const char* name;
    std::uint32_t arity, localCount, maxStack;
    bool variadic, method, paramless;
    const ConstRec* consts; std::size_t constCount;
    const HandlerRec* handlers; std::size_t handlerCount;
    const char* const* strings; std::size_t stringCount;
    /** Interned symbols, one per ConstRec that needs one; filled once by linkModule. */
    const proto::ProtoString** symbols;
    /** Nested blocks, indexed by the operand MAKE_FN carries. */
    proto::ProtoMethod const* blocks; std::size_t blockCount;
};

// --- module lifecycle ------------------------------------------------------

/**
 * The context the caller is running in, from activeCallContext(). Throws
 * std::logic_error naming the caller when there is none: a generated module
 * reached outside a protoScala call context is a host defect, and D74 keeps a
 * defect uncatchable. protoScala has no getCurrentContext(); this is the one
 * place a generated file may obtain a context without being handed one.
 */
proto::ProtoContext* currentContext();

/** The () singleton of this space (RuntimeLayout::unit). */
const proto::ProtoObject* unitValue(proto::ProtoContext* ctx);

/** The __captures__ list of a closure object, or nullptr when it has none. */
const proto::ProtoObject* capturesOf(proto::ProtoContext* ctx, const proto::ProtoObject* self);

/**
 * Interns every symbol of `blocks[0..n)` in `ctx`'s space, exactly once per space,
 * and installs the module's ClassInfo/GlobalTable exports. Idempotent; throws
 * std::logic_error if asked to link the same module into a second space.
 */
void linkModule(proto::ProtoContext* ctx, const BlockRec* const* blocks, std::size_t n);

/**
 * Script mode (D8): looks up the @main global, calls it through
 * ExecutionEngine::callTopLevel, reports an uncaught Scala exception in the same
 * shape Session::runScript does, and returns the process exit code.
 */
int runMain(proto::ProtoContext* ctx, const char* mainKey, bool takesArgs,
            int argc, char** argv);

/**
 * D6 site 2: the boundary every generated block's thunk presents to the VM.
 * Calls `body` inside translateForeignException, whose six clauses are in
 * src/umd/ForeignBoundary.h. The emitter writes the thunk; the clauses live here.
 */
const proto::ProtoObject* enterMethod(proto::ProtoMethod body, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* self,
                                      const proto::ParentLink* pl,
                                      const proto::ProtoList* args,
                                      const proto::ProtoSparseList* kwargs);

/**
 * Runs the module body through translateForeignException (D6 site 1) and returns
 * the module object the provider publishes.
 */
const proto::ProtoObject* runModuleBody(proto::ProtoContext* ctx, proto::ProtoMethod body,
                                        const char* logicalPath, const char* version);

// --- the frame -------------------------------------------------------------

/**
 * One invocation's ProtoContext and traced slots (P1, P2). Reproduces
 * ExecutionEngine::execute's prologue: arity + localCount slots, then maxStack
 * operand slots, then ONE reserved slot for an in-flight exception value.
 * Every value a generated function touches lives in slots(); no generated code
 * ever holds a ProtoObject* in a C++ local across an allocation.
 */
class Frame {
public:
    Frame(proto::ProtoContext* parent, const BlockRec& blk,
          const proto::ProtoObject* self, const proto::ProtoList* args,
          const proto::ProtoSparseList* kwargs, const proto::ProtoObject* captures);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    proto::ProtoContext* ctx();
    const proto::ProtoObject** slots();
    /** arity + localCount: where the operand stack starts. */
    unsigned stackBase() const;
    /** The reserved in-flight-exception slot index. */
    unsigned pendingSlot() const;
    /** Publishes `v` as this frame's returnValue and returns it. */
    const proto::ProtoObject* finish(const proto::ProtoObject* v);
};

// --- operations, one per opcode group -------------------------------------
// Each forwards to the SAME inline function ExecutionEngine::runLoop calls
// (src/runtime/OpcodeOps.h). There is no second implementation.

const proto::ProtoObject* constant(proto::ProtoContext*, const BlockRec&, std::size_t idx);
const proto::ProtoObject* pushGlobal(proto::ProtoContext*, const BlockRec&, std::size_t idx);
void storeGlobal(proto::ProtoContext*, const BlockRec&, std::size_t idx, const proto::ProtoObject* v);

const proto::ProtoObject* add(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* sub(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* mul(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* lt (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* le (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* gt (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* ge (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* eq (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* ne (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* neg(proto::ProtoContext*, const proto::ProtoObject* a);
const proto::ProtoObject* notOp(proto::ProtoContext*, const proto::ProtoObject* a);
/** JUMP_IF_FALSE / JUMP_IF_TRUE: raises when `v` is not a Boolean, as the VM does. */
bool truthy(proto::ProtoContext*, const proto::ProtoObject* v);
const proto::ProtoObject* concat(proto::ProtoContext*, const proto::ProtoObject** vals, unsigned n);

const proto::ProtoObject* makeCell(proto::ProtoContext*);
const proto::ProtoObject* cellGet(proto::ProtoContext*, const proto::ProtoObject* cell);
const proto::ProtoObject* cellSet(proto::ProtoContext*, const proto::ProtoObject* cell,
                                  const proto::ProtoObject* v);
const proto::ProtoObject* makeLazy(proto::ProtoContext*, const proto::ProtoObject* thunk);
const proto::ProtoObject* force(proto::ProtoContext*, const proto::ProtoObject* v);
const proto::ProtoObject* forceThunk(proto::ProtoContext*, const proto::ProtoObject* v);

const proto::ProtoObject* makeFn(proto::ProtoContext*, const BlockRec& enclosing,
                                 std::size_t blockIndex, const proto::ProtoObject** captures,
                                 unsigned captureCount);
const proto::ProtoObject* call(proto::ProtoContext*, const proto::ProtoObject* callee,
                               const proto::ProtoObject** args, unsigned argc);
const proto::ProtoObject* callSpread(proto::ProtoContext*, const proto::ProtoObject* callee,
                                     const proto::ProtoObject** args, unsigned argc,
                                     const proto::ProtoObject* rest);
const proto::ProtoObject* callKw(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                 const proto::ProtoObject* callee, const proto::ProtoObject** args,
                                 unsigned argc, const proto::ProtoObject** kwVals, unsigned kwCount);
const proto::ProtoObject* send(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                               const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* sendApply(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* sendKw(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                 const proto::ProtoObject** base, unsigned argc,
                                 const proto::ProtoObject** kwVals, unsigned kwCount);
const proto::ProtoObject* sendSuper(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base, unsigned argc);

const proto::ProtoObject* makeClass(proto::ProtoContext*, const BlockRec&, std::size_t specIdx,
                                    const proto::ProtoObject** base);
const proto::ProtoObject* construct(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* constructSpread(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                          const proto::ProtoObject** base, unsigned argc,
                                          const proto::ProtoObject* rest);
const proto::ProtoObject* invokeInit(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                     const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* storeField(proto::ProtoContext*, const BlockRec&, std::size_t symIdx,
                                     const proto::ProtoObject* recv, const proto::ProtoObject* v);
void setField(proto::ProtoContext*, const BlockRec&, std::size_t symIdx,
              const proto::ProtoObject* obj, const proto::ProtoObject* v);

bool testType(proto::ProtoContext*, const proto::ProtoObject* v, std::uint32_t typeCode);
bool testProto(proto::ProtoContext*, const BlockRec&, std::size_t symIdx, const proto::ProtoObject* v);
unsigned unapplyFields(proto::ProtoContext*, const BlockRec&, std::size_t namesIdx,
                       const proto::ProtoObject* v, const proto::ProtoObject** out);
void uncons(proto::ProtoContext*, const proto::ProtoObject* list, const proto::ProtoObject** outHead,
            const proto::ProtoObject** outTail);
[[noreturn]] void matchError(proto::ProtoContext*, const proto::ProtoObject* v);
[[noreturn]] void castFail(proto::ProtoContext*, const BlockRec&, std::size_t strIdx,
                           const proto::ProtoObject* v);
const proto::ProtoObject* makeTuple(proto::ProtoContext*, const proto::ProtoObject** elems, unsigned n);

[[noreturn]] void throwValue(proto::ProtoContext*, const proto::ProtoObject* v);
[[noreturn]] void rethrow(proto::ProtoContext*, const proto::ProtoObject* v);
/** The handler entry for `pc`, or nullptr. Same search order as the VM's table. */
const HandlerRec* handlerFor(const BlockRec&, std::size_t pc);
/** A ScalaError as a prelude Throwable instance, for the retry loop's catch. */
const proto::ProtoObject* materialise(proto::ProtoContext*, const char* cls, const char* msg);

/** JUMP_BACK's GC obligation (P4 rule 1). Inlined to one call. */
void safepoint(proto::ProtoContext*);

/** Resolves an import at load time, exactly as the interpreter's D90 path does. */
const proto::ProtoObject* importModule(proto::ProtoContext*, const char* providerSpec,
                                       const char* logicalPath, const char* importerDir);

}  // namespace protoScala::gen
#endif
```

- [ ] **Step 1 — Turn the five static libraries into `OBJECT` libraries and add the shared target.** In `CMakeLists.txt`, after the `project()` block, add
  ```cmake
  set(PROTOSCALA_ABI_SOVERSION "1")
  ```
  and change each `add_library(protoscala_<x> STATIC` to `add_library(protoscala_<x> OBJECT`, adding `set_property(TARGET protoscala_<x> PROPERTY POSITION_INDEPENDENT_CODE ON)` to each. Then, after `protoscala_repl`:
  ```cmake
  add_library(protoScala SHARED src/runtime/GeneratedSupport.cpp)
  target_link_libraries(protoScala
      PRIVATE protoscala_repl protoscala_runtime protoscala_compiler
              protoscala_frontend protoscala_support
      PUBLIC  ${PROTOCORE_LIBRARY} pthread ${CMAKE_DL_LIBS})
  target_include_directories(protoScala
      PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
             $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      PRIVATE src)
  set_target_properties(protoScala PROPERTIES
      OUTPUT_NAME protoScala
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROTOSCALA_ABI_SOVERSION}
      PUBLIC_HEADER "include/protoScala/GeneratedModule.h")
  ```
  and change the executable's link line to `target_link_libraries(protoscala PRIVATE protoScala)`.
- [ ] **Step 2 — Write `include/protoScala/GeneratedModule.h`** exactly as the Interfaces block above. Nothing is added to it in later tasks without an ABI note in `docs/PROTOSCALAC_SPECIFICATION.md`; that rule is written into the header's leading comment together with the sentence *"This file is the whole published surface: a generated module includes this and `protoCore.h`, nothing else."*
- [ ] **Step 3 — Write `src/runtime/GeneratedSupport.cpp` with the two boundary sites (§D6).** Every `gen::` function body is a forward; the two translations are here and nowhere else:
  ```cpp
  #include <protoScala/GeneratedModule.h>
  #include "runtime/OpcodeOps.h"
  #include "umd/ForeignBoundary.h"
  #include "runtime/ExecutionEngine.h"

  namespace protoScala::gen {

  const proto::ProtoObject* runModuleBody(proto::ProtoContext* ctx, proto::ProtoMethod body,
                                          const char* logicalPath, const char* version) {
      // D6 site 1: proto_module_init is called across a dlopen boundary.
      return translateForeignException([&] {
          return moduleObjectFor(ctx, body, logicalPath, version);
      });
  }

  }  // namespace protoScala::gen
  ```
  plus site 2, which is `enterMethod` and is equally one expression:
  ```cpp
  const proto::ProtoObject* enterMethod(proto::ProtoMethod body, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self, const proto::ParentLink* pl,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* kwargs) {
      // D6 site 2: a transpiled function is a proto::ProtoMethod, so from the
      // VM's point of view it is a foreign callable and presents the same
      // boundary every other native method does.
      return translateForeignException([&] { return body(ctx, self, pl, args, kwargs); });
  }
  ```
  Leave every operation body as `return ops::add(ctx, a, b);` and so on — Task 3 creates `ops::`.
- [ ] **Step 4 — Install the library, the header and the CMake package.**
  ```cmake
  install(TARGETS protoScala
      EXPORT protoScalaTargets
      LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
      PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/protoScala
      COMPONENT protoScala)
  install(EXPORT protoScalaTargets
      FILE protoScalaTargets.cmake NAMESPACE protoScala::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/protoScala
      COMPONENT protoScala)
  ```
  plus `configure_package_config_file` / `write_basic_package_version_file` with `COMPATIBILITY SameMajorVersion`, matching what protoCore's Phase I generated. Keep the executable's `INSTALL_RPATH` as it is and add the same to `protoScala`.
- [ ] **Step 5 — A unit test that the facade links and runs.** `tests/unit/test_generated_support.cpp`: create a `Session`, take its `space()`, open a child context, and assert `gen::add(ctx, ctx->fromInteger(2), ctx->fromInteger(3))->asLong(ctx) == 5`; assert `gen::add` promotes at the boundary by adding `1LL << 53` twice and checking the result is still exact; assert `gen::truthy(ctx, PROTO_NONE)` throws `ScalaError` with class `ClassCastException`.
- [ ] **Step 6 — Rebuild from clean, twice.** `rm -rf build_release` (path checked), configure, build, `ctest ... < /dev/null`. The count must be **the Step 2 baseline + 3** (the three new unit cases) — the baseline as measured then, not 1263, which was already stale a day after this plan was written. Then `rm -rf build_release` and repeat, to prove nothing in the build depends on a stale object file.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala
ls -l build_release/libprotoScala.so.1 && \
readelf -d build_release/libprotoScala.so.1 | grep -E 'SONAME|NEEDED' && \
ctest --test-dir build_release --output-on-failure < /dev/null 2>&1 | tail -3
```
prints `SONAME  libprotoScala.so.1`, a `NEEDED libprotoCore.so.3` line, and `1266 tests passed, 0 tests failed`.

---

## Task 3: One semantics, two consumers — `src/runtime/OpcodeOps.h`

**Files:**
- `src/runtime/OpcodeOps.h` (**create**) — every extracted opcode body as an `inline` function in `namespace protoScala::ops`.
- `src/runtime/ExecutionEngine.cpp` (modify) — `runLoop`'s arms call `ops::`.
- `src/runtime/GeneratedSupport.cpp` (modify) — `gen::` forwards to `ops::`.
- `.agent_scratch/phase7-transpiler/opcode-cycles.md` (created) — the cycle gate's evidence.

**Interfaces** — `namespace protoScala::ops`, all `inline`. The signatures are the `gen::` ones of Task 2 with the `BlockRec&` parameter replaced by what the interpreter already has, so that both callers can reach them. The four that differ:

```cpp
namespace protoScala::ops {

/** ADD..NOT, CONCAT, truthy, makeCell..forceThunk, uncons, testType, makeTuple:
 *  identical signatures to the gen:: declarations — no call-site data needed. */

/** A send, keyed by the already-interned site symbol rather than a pool index. */
inline const proto::ProtoObject* sendNamed(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                           const proto::ProtoObject** base,
                                           const proto::ProtoString* name, unsigned argc,
                                           const proto::ProtoString* fallback, bool applied);

/** MAKE_CLASS, on the spec the caller resolved. */
inline const proto::ProtoObject* makeClassFrom(proto::ProtoContext* ctx, ExecutionEngine& eng,
                                               const BytecodeModule::Const& spec,
                                               const proto::ProtoObject* const* base);

/** UNAPPLY_FIELDS, on the resolved key vector. */
inline unsigned unapplyFieldsWith(proto::ProtoContext* ctx,
                                  const std::vector<const proto::ProtoString*>& keys,
                                  const proto::ProtoObject* v, const proto::ProtoObject** out);

/** TEST_PROTO, on the resolved type key. */
inline bool testProtoKey(proto::ProtoContext* ctx, const proto::ProtoString* key,
                         const proto::ProtoObject* v);
}
```

- [ ] **Step 1 — Enumerate the 54 defined opcodes into three classes, in a table in the header's leading comment.** Class **V** (vanishes in generated code: `NOP`, `EXTEND`, `PUSH_CONST`, `PUSH_UNIT`, `PUSH_NULL`, `PUSH_TRUE`, `PUSH_FALSE`, `POP`, `DUP`, `PUSH_LOCAL`, `STORE_LOCAL`, `PUSH_CELL`/`STORE_CELL`'s slot halves, `JUMP`, `JUMP_IF_FALSE`, `JUMP_IF_TRUE`, `JUMP_BACK`, `RETURN`) — **no `ops::` entry**, because there is no body to share. Class **P** — already reachable through a *public* `ExecutionEngine` member, which is only `CALL`/`CALL_SPREAD`→`invoke`, `SEND`→`send`, `NEW`→`construct` and `FORCE`→`force`; `ops::` is a one-line forward. Class **B** — everything else, a real body inside the `switch` today, extracted. **Class B is the large class, not the small one**, because `dispatch`, `makeClass`, `makeTuple`, `instantiate`, `sendKeywords`, `callKeywords`, `superSend`, `testType`, `callNative` and `slowBinary` are all `private`; extracting into free functions is what avoids promoting eighteen members to public. The table is the review artefact: an opcode not in exactly one class is a bug in the table.
- [ ] **Step 2 — Extract class B, one opcode per commit-sized edit.** For each, cut the body out of `runLoop`'s arm into an `inline` function and replace the arm with a call. `ADD` is the model:
  ```cpp
  // src/runtime/OpcodeOps.h
  inline const proto::ProtoObject* add(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* a,
                                       const proto::ProtoObject* b) {
      // <the body that was in runLoop's case Op::ADD, moved verbatim>
  }
  ```
  ```cpp
  // src/runtime/ExecutionEngine.cpp, case Op::ADD:
  sp -= 1; sp[-1] = ops::add(&frame, sp[-1], sp[0]); break;
  ```
  **Move the body verbatim.** An improvement made during the move is a behaviour change with no test to catch it; improvements go in a separate commit after the suite is green.
- [ ] **Step 3 — Keep the interpreter's fast paths in the interpreter where they are layout-sensitive.** `ADD`'s SmallInteger fast path is measured to matter. Extract it *inside* `ops::add` as the first branch, so both callers get it, and mark `ops::add` `[[gnu::always_inline]] inline` **only if** Step 5's measurement asks for it. Do not decorate speculatively (memory: micro-opts can hurt icache layout).
- [ ] **Step 4 — Point `gen::` at `ops::`.** Each `gen::` function resolves its `BlockRec` index into the already-interned symbol from `BlockRec::symbols` and forwards. Example:
  ```cpp
  const proto::ProtoObject* send(proto::ProtoContext* ctx, const BlockRec& blk, std::size_t siteIdx,
                                 const proto::ProtoObject** base, unsigned argc) {
      const ConstRec& c = blk.consts[siteIdx];
      return ops::sendNamed(ctx, *activeEngine(), base, blk.symbols[siteIdx], argc,
                            c.key ? internedFallback(blk, siteIdx) : nullptr, /*applied*/false);
  }
  ```
  `activeEngine()` is `activeCallContext()->engine`, which `run()` installs and every entry restores on every exit path; `GeneratedSupport.cpp` asserts it is non-null and throws `std::logic_error` naming the function if it is, because a generated method reached without an active call context is a VM defect and D74 says a VM defect stays uncatchable.
- [ ] **Step 5 — The cycle gate.** Before and after the extraction, on an otherwise idle machine:
  ```bash
  cd /home/gamarino/Documentos/proyectos/protoScala
  for b in fib tak sum_loop; do
    perf stat -r 3 ./build_release/protoscala benchmarks/$b.scala 2>&1 | grep -E 'cycles|elapsed'
  done
  ```
  Record both runs in `opcode-cycles.md` with the machine, the date and the governor. **Back out the extraction of any opcode whose extraction costs more than 3 % of `fib`, `tak` or `sum_loop` cycles**, and record which and why — Phase 3 backed out a SmallInteger fast path under exactly this rule after measuring an 8 % `sum_loop` regression from code layout. An opcode backed out of `ops::` gets its generated-code body by calling the public `ExecutionEngine` member instead, and the reason is recorded at the call site.
- [ ] **Step 6 — Prove the two consumers share one body.** Add `tests/unit/test_opcode_ops.cpp` with one case per class-B opcode, calling `ops::` directly with values chosen from the existing conformance expectations (integer overflow boundary ±2^53, `String` + `Int`, `null` receiver, empty list `UNCONS`), and assert the result equals what the fixture for that behaviour expects. These cases are the contract the generated code inherits.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && cmake --build build_release -j4 && \
ctest --test-dir build_release --output-on-failure < /dev/null 2>&1 | tail -3 && \
grep -c 'inline' src/runtime/OpcodeOps.h && \
cat ../.agent_scratch/phase7-transpiler/opcode-cycles.md
```
shows the suite still green at its new count, `OpcodeOps.h` with one `inline` per class-P and class-B opcode, and a before/after cycle table with every deviation above 3 % accounted for by a named back-out.

---

## Task 4: `protoscalac` — command line, toolchain resolution, Makefile

**Files:**
- `src/compiler/TranspilerMain.cpp` (**create**).
- `src/compiler/protoscalac_paths.h.in` (**create**) → configured to `build_release/generated/protoscalac_paths.h`.
- `CMakeLists.txt` (modify) — the `protoscalac` target and the configured header.
- `tests/cli/transpiler-cli.sh` (**create**).

**Interfaces:**

```cpp
// src/compiler/TranspilerMain.cpp
/** Compiler, include and library directories written into the generated Makefile. */
struct Toolchain {
    std::string cxx;
    std::vector<std::string> includeDirs;
    std::vector<std::string> libraryDirs;
};
static Toolchain resolveToolchain(const char* argv0);
static bool generateMakefile(const std::filesystem::path& outRoot,
                             const std::vector<std::filesystem::path>& sources,
                             const Toolchain& tc);
static int processFile(const std::filesystem::path& sourcePath,
                       const std::filesystem::path& outRoot,
                       const std::string& logicalPath, const std::string& moduleVersion,
                       bool asScript, bool reportPurity);
int main(int argc, char* argv[]);
```

```cmake
# protoscalac_paths.h.in
#define PROTOSCALAC_DEFAULT_CXX          "@CMAKE_CXX_COMPILER@"
#define PROTOSCALAC_BUILD_BINDIR         "@CMAKE_BINARY_DIR@"
#define PROTOSCALAC_BUILD_INCLUDE_DIRS   "@PROTOSCALAC_BUILD_INCLUDE_DIRS@"
#define PROTOSCALAC_BUILD_LIBRARY_DIRS   "@PROTOSCALAC_BUILD_LIBRARY_DIRS@"
#define PROTOSCALAC_INSTALL_INCLUDE_DIRS "@PROTOSCALAC_INSTALL_INCLUDE_DIRS@"
#define PROTOSCALAC_INSTALL_LIBRARY_DIRS "@PROTOSCALAC_INSTALL_LIBRARY_DIRS@"
```

- [ ] **Step 1 — The command line, deliberately `protopyc`-shaped.** The source path is the first argument; without a mode option `--emit-cpp` is assumed.

  | Option | Effect |
  |---|---|
  | `--emit-cpp` | generate C++ source only |
  | `--emit-make` | also write a `Makefile` |
  | `--build-so` | write the `Makefile` and run `make` |
  | `--as-module` | module mode: `desugarModule`, no `proto_module_main` (§D8) |
  | `--as-script` | script mode: `desugar`, emit `proto_module_main` (§D8) |
  | `--module-name <dotted>` | the logical path the module declares; defaults to the file stem |
  | `--module-version <v>` | the version component of the identity; defaults to `""` (§D4) |
  | `--report-purity` | print what the module needs and why, and exit 0 without emitting (§D2) |

  Neither `--as-module` nor `--as-script` given: script mode when the unit defines an `@main` (`CompiledUnit::mainName` non-empty), module mode otherwise. `--as-module` on a unit with an `@main` fails with `desugarModule`'s own D91 message. A directory argument mirrors the tree under `./out/` and writes one `Makefile` there, as `protopyc` does.
- [ ] **Step 2 — Toolchain resolution, both trees.** Copy the shape `protopyc` uses, because a relocated prefix must keep working: `executableDir(argv0)` from `/proc/self/exe` with `fs::canonical(argv0)` as fallback; `fs::equivalent(exeDir, PROTOSCALAC_BUILD_BINDIR)` decides build tree versus installation; installed relative entries resolve against the executable's directory. Overrides `PROTOSCALAC_CXX`, `PROTOSCALAC_INCLUDE_DIRS`, `PROTOSCALAC_LIBRARY_DIRS` (`:`-separated) replace the corresponding value. Build-tree include dirs are protoScala's `include/` and protoCore's include dirs; build-tree library dirs are the directory holding `libprotoScala.so` and the one holding `libprotoCore.so`.
- [ ] **Step 3 — Emit the Makefile.** Refuse whitespace first, because `make` splits words on it:
  ```cpp
  if (hasWhitespace(d)) {
      std::cerr << "protoscalac: directory contains whitespace, which make cannot handle: \"" << d
                << "\" (set PROTOSCALAC_INCLUDE_DIRS / PROTOSCALAC_LIBRARY_DIRS to a path without spaces)\n";
      return false;
  }
  ```
  then write:
  ```make
  ifeq ($(origin CXX),default)
  CXX = <PROTOSCALAC_DEFAULT_CXX>
  endif
  CXXFLAGS = -O2 -fPIC -std=c++20
  INCLUDES = -I<dir> ...
  LDFLAGS  = -L<dir> -Wl,-rpath,<dir> ...
  LIBS     = -lprotoScala -lprotoCore
  SRCS = foo.cpp
  OBJS = $(SRCS:.cpp=.o)
  TARGET = module.so
  all: $(TARGET)
  $(TARGET): $(OBJS)
  	$(CXX) -shared $(LDFLAGS) -o $@ $^ $(LIBS)
  %.o: %.cpp
  	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
  clean:
  	rm -f $(OBJS) $(TARGET)
  ```
  `-O2`, not `protopyc`'s `-O3`: a generated module is one long function per block and `-O3`'s inlining makes compile time superlinear in block size, which Task 15 Step 4 measures. Record the choice in the specification. `-Wl,-rpath` for every library directory, so the module loads without `LD_LIBRARY_PATH` — the property `protopyc` documents and Task 14 Step 5 verifies with `env -u LD_LIBRARY_PATH`.
  The target is `module.so`, as `protopyc`'s is, and the specification says to rename it to `<module>.so` before placing it on the search path.
- [ ] **Step 4 — Errors and exit status.** Parse and compile errors print `file:line:column: message` from `ParseError`/`CompileError`'s `SourcePos` and stop at the first one, as `protopyc` does. Exit 0 on success; **1** when no arguments are given (usage printed), the source path does not exist, an option is unknown, parsing, compilation or emission fails, a `Makefile` path contains whitespace, or `make` fails.
- [ ] **Step 5 — The target.** In `CMakeLists.txt`:
  ```cmake
  set(PROTOSCALAC_BUILD_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include:${PROTOCORE_INCLUDE_DIRS}")
  set(PROTOSCALAC_BUILD_LIBRARY_DIRS "${CMAKE_BINARY_DIR}:${PROTOCORE_LIB_DIR}")
  set(PROTOSCALAC_INSTALL_INCLUDE_DIRS "../${CMAKE_INSTALL_INCLUDEDIR}")
  set(PROTOSCALAC_INSTALL_LIBRARY_DIRS "../${CMAKE_INSTALL_LIBDIR}")
  configure_file(src/compiler/protoscalac_paths.h.in generated/protoscalac_paths.h @ONLY)
  add_executable(protoscalac src/compiler/TranspilerMain.cpp src/compiler/CppEmitter.cpp
                             src/compiler/CppTables.cpp)
  target_link_libraries(protoscalac PRIVATE protoScala)
  target_include_directories(protoscalac PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(protoscalac PRIVATE -Wall -Wextra -Wpedantic)
  ```
- [ ] **Step 6 — The CLI test.** `tests/cli/transpiler-cli.sh`, registered in the existing `foreach(cli_test ...)` list, checks: no arguments prints usage and exits 1; an unknown option exits 1; a missing file exits 1; `--emit-cpp` on `tests/conformance/00-binary/hello.scala` writes `hello.cpp` containing `proto_module_init`; `--emit-make` also writes a `Makefile` whose `LIBS` line is exactly `LIBS = -lprotoScala -lprotoCore`; `PROTOSCALAC_INCLUDE_DIRS="/a b"` exits 1 with the whitespace message.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler && \
/home/gamarino/Documentos/proyectos/protoScala/build_release/protoscalac \
  /home/gamarino/Documentos/proyectos/protoScala/tests/conformance/00-binary/hello.scala --emit-make && \
grep -n 'LIBS\|rpath' Makefile && test -f hello.cpp && echo OK
```
prints the `LIBS = -lprotoScala -lprotoCore` line, at least one `-Wl,-rpath,` entry, and `OK`; and `ctest --test-dir build_release -R cli/transpiler-cli --output-on-failure < /dev/null` passes.

---

## Task 5: Static tables — one emitter, two users

**Files:**
- `src/compiler/CppTables.h` / `.cpp` (**create**) — the table emitters, factored out of `src/tools/PrecompileMain.cpp`.
- `src/tools/PrecompileMain.cpp` (modify) — uses `CppTables` instead of its own copies.
- `src/compiler/CppEmitter.h` / `.cpp` (**create**) — declares the emitter; Step 5 fills the table half.
- `tests/unit/test_cpp_tables.cpp` (**create**).

**Interfaces:**

```cpp
// src/compiler/CppTables.h
namespace protoScala::tables {

/** A C++ string literal that round-trips every byte, never starts a trigraph. */
std::string quoted(const std::string& s);
/** A double as a hexadecimal floating literal; throws on a non-finite value. */
std::string exactDouble(double d);

/** Flattens a BytecodeModule tree depth-first; index 0 is the root. */
struct FlatBlock { const BytecodeModule* mod; int parent; std::string cppName; };
std::vector<FlatBlock> flatten(const BytecodeModule& root);

/** Writes `static const protoScala::gen::ConstRec <name>[] = { ... };` */
void emitConsts(std::ostream& o, const std::string& name, const BytecodeModule& mod,
                const std::string& stringsName, std::vector<std::string>& stringPool);
void emitHandlers(std::ostream& o, const std::string& name, const BytecodeModule& mod);
void emitStrings(std::ostream& o, const std::string& name, const std::vector<std::string>& pool);
/** Writes `static const protoScala::gen::BlockRec <name> = { ... };` */
void emitBlockRec(std::ostream& o, const std::string& name, const FlatBlock& blk,
                  const std::string& constsName, const std::string& handlersName,
                  const std::string& stringsName, const std::string& symbolsName,
                  const std::string& blocksName);
/** Writes the ClassInfo / GlobalBinding / by-name-selector export tables (Task 9). */
void emitExports(std::ostream& o, const std::string& name, const GlobalTable& globals,
                 const std::string& objectName);
}
```

- [ ] **Step 1 — Move, do not copy.** Cut `emitString`, `quoted` and `exactDouble` out of `PrecompileMain.cpp` into `CppTables.cpp` verbatim (including the `'?'` → `\?` trigraph guard and the `std::hexfloat` round-trip with its non-finite refusal) and have `PrecompileMain.cpp` include the header. The prelude image and the transpiler then share one escaper, which is the point: two escapers that disagree on one byte is a class of bug with no symptom until a string literal is wrong.
- [ ] **Step 2 — `flatten`.** Depth-first, `parent` = enclosing index, `-1` for the root, `cppName` = `"blk" + std::to_string(index)`. **The order must be the order `MAKE_FN`'s operand means**, which is the order `BytecodeModule::addBlock` created — the same invariant `PreludeModuleRec`'s comment records ("reconstruction adds blocks in index order, so every block index baked into a MAKE_FN operand stays exactly what the compiler emitted"). Add a `static_assert`-grade runtime check in `flatten`: for every block, `root.blockAt(i) == flat[i].mod`, or throw `std::logic_error`.
- [ ] **Step 3 — `emitConsts`.** One `ConstRec` per `BytecodeModule::constAt(i)`, in pool order, with `sval`/`key` as indices into a per-block string pool emitted by `emitStrings` and `namesFirst`/`namesCount`, `fieldsFirst`/`fieldsCount` as ranges into the same pool. Example output for a `SendSite`:
  ```cpp
  static const char* const blk0_strings[] = { "println", "", "Point", "x", "y" };
  static const protoScala::gen::ConstRec blk0_consts[] = {
      /* 0 SendSite */ { 6, 0, 0.0, 10, 1, 0, false, blk0_strings[0], nullptr, 0, 0, 0, 0 },
  };
  ```
  The `kind` byte is `static_cast<std::uint8_t>(BytecodeModule::ConstKind::...)`, and `CppTables.cpp` carries a `static_assert` on each enumerator's value so a reordering of `ConstKind` breaks the build rather than the output.
- [ ] **Step 4 — `emitHandlers`.** One `HandlerRec` per `mod.handlers()` entry, **in table order**, because the compiler appends nested `try`s before enclosing ones and a `try`'s `Catch` entry before its `Finally` entry, so table order *is* search order. `gen::handlerFor` returns the first entry whose `[startPc, endPc)` contains `pc`, which is `BytecodeModule::handlerFor`'s rule.
- [ ] **Step 5 — `emitBlockRec` and the symbol array.** Each block emits
  ```cpp
  static const proto::ProtoString* blk0_symbols[3] = {};
  static const proto::ProtoMethod blk0_blocks[] = { &blk1, &blk2 };
  static const protoScala::gen::BlockRec blk0_rec = {
      "main", /*arity*/0, /*localCount*/2, /*maxStack*/4,
      false, false, false,
      blk0_consts, 1, blk0_handlers, 0, blk0_strings, 5,
      blk0_symbols, blk0_blocks, 2 };
  ```
  and, after the last block, the flat array `gen::linkModule` and Task 9's `proto_module_init` take:
  ```cpp
  static const protoScala::gen::BlockRec* const kAllBlocks[] = { &blk0_rec, &blk1_rec, &blk2_rec };
  static const std::size_t kAllBlockCount = 3;
  ```
  `blk0_symbols` is written **once per process** by `gen::linkModule`, which is what replaces `BytecodeModule::linkSymbols`. It holds `createSymbol` symbols, which are strong and never collected, so the array is not a GC root and needs none — the same reasoning `BytecodeModule`'s P1-boundary comment already records. `linkModule` refuses to run twice for one space and throws `std::logic_error` if asked to link the same module into a second space, because a module's symbols are a single space's strong symbols.
- [ ] **Step 6 — Test the emitter against the prelude.** `tests/unit/test_cpp_tables.cpp`: compile `lib/prelude.scala` in-process, run `tables::flatten`, and assert the flattened order matches `PrecompileMain`'s own flattening for the same input (the two must agree, since they are now the same function); assert `quoted("a\"b\\c\nd?e")` round-trips through a C++ compiler by writing it to a scratch `.cpp`, compiling it and comparing bytes; assert `exactDouble` throws on infinity.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && cmake --build build_release -j4 && \
ctest --test-dir build_release -R 'unit' --output-on-failure < /dev/null 2>&1 | tail -3 && \
grep -c 'emitString\|exactDouble' src/tools/PrecompileMain.cpp
```
shows the unit suite green and `0` — `PrecompileMain.cpp` no longer defines either, so there is exactly one escaper in the repository.

---

## Task 6: Code emission — the frame, the slots, the straight-line opcodes

**Files:** `src/compiler/CppEmitter.h` / `.cpp` (modify), `src/runtime/GeneratedSupport.cpp` (modify), `tests/unit/test_cpp_emitter.cpp` (**create**).

**Interfaces:**

```cpp
// src/compiler/CppEmitter.h
namespace protoScala {

struct EmitOptions {
    std::string sourcePath;      // for #line
    std::string logicalPath;     // the module's declared logical path
    std::string moduleVersion;   // "" when none (D4)
    bool asScript = false;       // emit proto_module_main (D8)
};

/** One construct the first cut refuses, with the position to report it at. */
struct Refusal { std::string message; int line; };

class CppEmitter {
public:
    CppEmitter(std::ostream& out, EmitOptions opts);
    /** Collects every Refusal first; emits nothing and returns them if any. */
    std::vector<Refusal> check(const CompiledUnit& unit) const;
    /** Emits the whole translation unit. Precondition: check() returned empty. */
    bool emit(const CompiledUnit& unit, const GlobalTable& globals);

private:
    std::ostream& out_;
    EmitOptions opts_;
    std::vector<tables::FlatBlock> flat_;
    /** Emits one block as a proto::ProtoMethod. */
    bool emitBlock(const tables::FlatBlock& blk, std::size_t index);
    /** One instruction at word index `pc`; advances `pc` past an EXTEND pair. */
    bool emitInstr(const BytecodeModule& mod, std::size_t& pc, int& depth);
    /** Labels for every jump target and every handler body, computed before emission. */
    std::set<std::size_t> labelTargets(const BytecodeModule& mod) const;
};
}
```

- [ ] **Step 1 — Compute the label set before emitting a single line.** Walk the code once, decoding `EXTEND` pairs, and collect every `JUMP`/`JUMP_IF_FALSE`/`JUMP_IF_TRUE`/`JUMP_BACK` target and every `handlers()[i].handlerPc`. Those word indices become C++ labels `L<pc>:`. Every other instruction emits no label, so `-O2` sees straight-line code.
- [ ] **Step 2 — The slot layout, which is what makes P1 structural.** `Frame`'s constructor resizes the context's automatic locals to `arity + localCount + maxStack + 1`:
  - `[0, arity)` — parameters, bound from `args` exactly as `ExecutionEngine::execute` does, including the variadic tail as `frame.newList(argc - fixed, args + fixed)` and the capture slots from `BlockRec`;
  - `[arity, arity + localCount)` — locals;
  - `[stackBase, stackBase + maxStack)` — **the operand stack**;
  - `[stackBase + maxStack]` — **the reserved in-flight-exception slot**, `pendingSlot()`.

  The emitter tracks `depth` at emit time, so every stack access is a constant index: `S[7]`, never `*sp++`. **No generated expression ever names a `const proto::ProtoObject*` C++ temporary.** A binary operation is emitted as two slot reads and one slot write:
  ```cpp
  S[7] = protoScala::gen::add(C, S[7], S[8]);
  ```
  which is P1-safe by construction because both operands are traced slot reads evaluated before the call and the result lands in a traced slot. This is the single most important paragraph of the task: it is why generated code cannot commit P4 rule 3 by accident.
- [ ] **Step 3 — The thunk, the body and the D6 site-2 boundary.** Each block emits a **pair**: a thunk that is the `proto::ProtoMethod` stored in `BlockRec::blocks[]`, and the body that carries the code. The thunk is one line and is the only place the boundary appears:
  ```cpp
  static const proto::ProtoObject* blk3_body(proto::ProtoContext*, const proto::ProtoObject*,
                                             const proto::ParentLink*, const proto::ProtoList*,
                                             const proto::ProtoSparseList*);

  static const proto::ProtoObject* blk3(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                        const proto::ParentLink* pl, const proto::ProtoList* args,
                                        const proto::ProtoSparseList* kwargs) {
      return protoScala::gen::enterMethod(&blk3_body, ctx, self, pl, args, kwargs);
  }

  #line 12 "/abs/path/Point.scala"
  static const proto::ProtoObject* blk3_body(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                             const proto::ParentLink*, const proto::ProtoList* args,
                                             const proto::ProtoSparseList* kwargs) {
      protoScala::gen::Frame F(ctx, blk3_rec, self, args, kwargs,
                               protoScala::gen::capturesOf(ctx, self));
      proto::ProtoContext* C = F.ctx();
      const proto::ProtoObject** S = F.slots();
      std::size_t pc = 0;
      (void)pc;
  ```
  `Frame`'s constructor is where the argument-count check lives, raising the same `IllegalArgumentException` wording `execute` raises — `"wrong number of arguments for <name>: expected <n>, got <m>"` — so the two paths produce one message. It also binds parameters, the variadic tail and the capture slots in `execute`'s order: positional, then variadic, then captures, **then** keywords and defaults, because a default block mirrors this frame's parameters and captures and both must already be in place. `Frame`'s destructor publishes nothing; `F.finish(v)` sets `returnValue` and is what `RETURN` emits.
- [ ] **Step 4 — Class-V opcodes.** `PUSH_CONST k` → `S[d] = protoScala::gen::constant(C, blk3_rec, k);`. `PUSH_UNIT`/`PUSH_NULL`/`PUSH_TRUE`/`PUSH_FALSE` → `S[d] = protoScala::gen::unitValue(C);` / `PROTO_NONE` / `PROTO_TRUE` / `PROTO_FALSE`. `POP` → `depth--` and nothing emitted. `DUP` → `S[d] = S[d-1];`. `PUSH_LOCAL n` → `S[d] = S[n];`. `STORE_LOCAL n` → `S[n] = S[d-1];`. `EXTEND` is consumed by the decoder and emits nothing. `RETURN` → `return F.finish(S[d-1]);`.
- [ ] **Step 5 — Jumps, and the safepoint obligation.** `JUMP t` → `goto L<t>;`. `JUMP_IF_FALSE t` → `if (!protoScala::gen::truthy(C, S[d-1])) goto L<t>;`, with `depth--` recorded. `JUMP_BACK t` → **`protoScala::gen::safepoint(C); goto L<t>;`**. The safepoint is not optional: `ProtoContext::safepoint()` is the only place a context's young chain reaches the collector, an unsubmitted chain is live by construction, and protoST reclaimed exactly **0** cells for its entire history while passing 833 tests (P4 rule 1). A generated loop without it is that bug, in machine-written code. Task 12's mutation M6 deletes the safepoint and asserts the GC-pressure fixture reds.
- [ ] **Step 6 — Arithmetic, comparison and `CONCAT`.** One `gen::` call each. `CONCAT n` gathers `n` operands from the stack into `&S[d-n]` and emits `S[d-n] = protoScala::gen::concat(C, &S[d-n], n);` — the array is a run of traced slots, not a C++ array of pointers, which is what keeps it P1-safe.
- [ ] **Step 7 — Cells, lazies and by-name.** `MAKE_CELL n` → `S[n] = protoScala::gen::makeCell(C);`. `PUSH_CELL n` → `S[d] = protoScala::gen::cellGet(C, S[n]);`. `STORE_CELL n` → `S[n] = protoScala::gen::cellSet(C, S[n], S[d-1]);` — note the reassignment: `cellSet` returns the cell, because a cell may be immutable and `setAttribute` then yields a new object, which the interpreter also has to write back.
- [ ] **Step 8 — `#line` on every statement boundary.** Emit `#line <mod.lineAt(pc)> "<abs source path>"` whenever `lineAt(pc)` differs from the previous instruction's. The absolute path is used so a debugger finds the file from any working directory; `protoscalac --emit-cpp` records the path it was given, and the specification says so.
- [ ] **Step 9 — Exhaustiveness, enforced by the compiler.** `emitInstr` is a `switch (op)` with **no `default`**, compiled with `-Wswitch -Werror=switch`. A new opcode added to `Op` then fails `protoscalac`'s build instead of being silently skipped. Every arm that is not implemented in the first cut calls `refuse(...)` rather than falling through.
- [ ] **Step 10 — Unit-test the emitter's arithmetic on the smallest possible program.** `tests/unit/test_cpp_emitter.cpp`: compile `val x = 1 + 2` in-process, run `CppEmitter::emit` to a `std::ostringstream`, and assert the output contains exactly one `gen::add(` call, one `gen::constant(C, ` per literal, and no occurrence of the substring `const proto::ProtoObject* t` (the emitter has no temporary mechanism, and a test that says so is what keeps it that way).

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler && \
P=/home/gamarino/Documentos/proyectos/protoScala && \
$P/build_release/protoscalac $P/tests/conformance/00-binary/hello.scala --build-so && \
grep -c 'safepoint\|#line' hello.cpp && ls -l module.so
```
produces `module.so`, and `grep -n 'const proto::ProtoObject\* [a-z_]* =' hello.cpp` prints nothing outside the function signatures — no generated C++ local holds a value.

---

## Task 7: Code emission — calls, sends, closures and classes

**Files:** `src/compiler/CppEmitter.cpp` (modify), `src/runtime/GeneratedSupport.cpp` (modify), `tests/conformance/27-transpiler/` (**create**, 12 fixtures).

**Interfaces:** no new signature; this task fills the `gen::` calls declared in Task 2.

- [ ] **Step 1 — `CALL n`.** The callee is at `S[d-n-1]` and the arguments are the `n` slots above it, contiguous, which is exactly what `gen::call` wants:
  ```cpp
  S[d-n-1] = protoScala::gen::call(C, S[d-n-1], &S[d-n], n);
  ```
  `CALL_SPREAD n` passes the trailing list as the extra argument. `CALL_KW k` reads the `KwSendSite` constant at `k` for the positional count and the keyword names, and passes the keyword values as a second contiguous run.
- [ ] **Step 2 — `SEND`, `SEND_APPLY`, `SEND_KW`, `SEND_SUPER`.** The receiver is the base of the run, so `base = &S[d-argc-1]` and the emitted call is `gen::send(C, blk3_rec, siteIdx, &S[d-argc-1], argc)`. `gen::send` resolves the site name through `BlockRec::symbols[siteIdx]` and the D5 fallback name through the `ConstRec::key` entry, then calls `ops::sendNamed`, which is the interpreter's own `dispatch`. **The emitter never re-implements the D5 private-member retry**; it passes the fallback and lets one implementation decide.
- [ ] **Step 3 — `MAKE_FN b`.** The block's captures are the `n = blockAt(b)->captureCount()` slots below the top:
  ```cpp
  S[d-n] = protoScala::gen::makeFn(C, blk3_rec, /*blockIndex*/b, &S[d-n], n);
  ```
  `gen::makeFn` builds the function object with the right `functionArity[...]` prototype and stores the captures in `__captures__`, and stores the **`proto::ProtoMethod`** — `blk3_rec.blocks[b]` — where the interpreter stores `__code__`. This is the line that delivers the phase: a transpiled closure is a native method object, and `ExecutionEngine::callMember` already knows how to call one, so an interpreted caller and a foreign caller take the same path.
- [ ] **Step 4 — `MAKE_CLASS`, `NEW`, `NEW_SPREAD`, `INVOKE_INIT`, `STORE_FIELD`, `SET_FIELD`.** `MAKE_CLASS s` reads the `ClassSpec` at `s`, whose parents are the `parentCount` slots and whose members are the `memberKeys.size()` slots above them, all contiguous. It emits `S[base] = protoScala::gen::makeClass(C, blk3_rec, s, &S[base]);` and `gen::makeClass` forwards to `ops::makeClassFrom`, which is `ExecutionEngine::makeClass` moved. The linearization is already in the `ClassSpec` the transpile-time `Linearizer` produced, so no linearization runs in generated code and none can differ.
- [ ] **Step 5 — Pattern-matching opcodes.** `TEST_TYPE t` → `S[d-1] = protoScala::gen::testType(C, S[d-1], t) ? PROTO_TRUE : PROTO_FALSE;`. `TEST_PROTO k` → `gen::testProto(C, blk3_rec, k, S[d-1])`. `UNAPPLY_FIELDS n` writes its results into the run starting at `S[d-1]` and the emitter advances `depth` by the `Names` constant's size, which it knows at emit time. `UNCONS` → `protoScala::gen::uncons(C, S[d-1], &S[d-1], &S[d]);`. `MATCH_ERROR` → `protoScala::gen::matchError(C, S[d-1]);` (`[[noreturn]]`, so the emitter stops emitting the arm). `CAST_FAIL k` likewise.
- [ ] **Step 6 — `MAKE_TUPLE n`.** `S[d-n] = protoScala::gen::makeTuple(C, &S[d-n], n);`. It builds a `TupleN` **case-class instance**, not a `proto::ProtoTuple`: DESIGN §4.6's prohibition is absolute, and a generated module that interned a transient tuple node would leak perennially with no reclamation metric able to see it.
- [ ] **Step 7 — Twelve fixtures under `tests/conformance/27-transpiler/`.** These are ordinary conformance fixtures with `// EXPECT:` first lines and they run on the **interpreter** like every other fixture — their purpose is to name, in the suite, the twelve shapes this task emits, so that a regression has a fixture with a topic name rather than only a differential diff. One each for: a plain call, a spread call, a keyword call, a send, an applied send, a super send, a closure with two captures, a class with a trait parent, a case class with `copy`, a pattern match with an unapply, a `TupleN` literal, and a `null`-receiver send. Each is also in the differential run of Task 11, so each is checked twice.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler && \
P=/home/gamarino/Documentos/proyectos/protoScala && \
for f in $P/tests/conformance/27-transpiler/*.scala; do \
  $P/build_release/protoscalac "$f" --build-so >/dev/null || { echo "FAIL $f"; exit 1; }; \
done; echo ALL-TRANSPILED
```
prints `ALL-TRANSPILED`, and `ctest --test-dir build_release -R 'conformance/27-transpiler' --output-on-failure < /dev/null` passes 12/12 on the interpreter.

---

## Task 8: Code emission — exceptions, faithfully

**Files:** `src/compiler/CppEmitter.cpp` (modify), `src/runtime/GeneratedSupport.cpp` (modify), `tests/unit/test_exceptions.cpp` (modify).

**Interfaces:** no new signature.

The interpreter's shape is load-bearing and the generated code must reproduce it, not approximate it. `ExecutionEngine::runFrame` enters a handler body **by `continue`, outside the C++ catch block**, and the recorded reason is that two things depend on it: the handler body must run with no live C++ handler so it can suspend cooperatively like any other code, and **the frame must be able to catch a second exception** — one raised by its own handler body, by the `RETHROW` a non-matching cascade emits, or by a `finally`. Entering the handler from inside the catch abandons the loop and the frame's handler table is never consulted again: measured, that turns **11 fixtures red**.

- [ ] **Step 1 — The retry loop.** Wrap the whole block body, not each `try` region:
  ```cpp
      std::size_t resumePc = 0;
      for (;;) {
        try {
          switch (resumePc) {
            case 0:  goto L0;
            case 34: goto L34;   // one case per handlerPc
            default: throw std::logic_error("blk3: unreachable resume pc");
          }
        L0:
          pc = 0; /* ... */
        L34:
          pc = 34; /* handler body */
        } catch (protoScala::ScalaThrow& t) {
          // A0-2 / E1: re-root the in-flight value in this frame's own traced
          // slots BEFORE anything can allocate. returnValue is the slot the
          // interpreter's frames use for exactly this, and pendingSlot() is
          // this frame's.
          C->returnValue = t.value;
          S[F.pendingSlot()] = t.value;
          const protoScala::gen::HandlerRec* h = protoScala::gen::handlerFor(blk3_rec, pc);
          if (!h) throw;
          S[h->slot] = S[F.pendingSlot()];
          resumePc = h->handlerPc;
          continue;
        } catch (const protoScala::ScalaError& e) {
          const protoScala::gen::HandlerRec* h = protoScala::gen::handlerFor(blk3_rec, pc);
          if (!h) throw;
          S[F.pendingSlot()] = protoScala::gen::materialise(C, e.className().c_str(),
                                                           e.message().c_str());
          S[h->slot] = S[F.pendingSlot()];
          resumePc = h->handlerPc;
          continue;
        } catch (const std::runtime_error& e) {
          // The protoCore-error bridge, the same one runLoop has: a protoCore
          // std::runtime_error becomes a RuntimeException. ScalaError is caught
          // above because it IS a std::runtime_error and re-translating it would
          // replace a precise class name with RuntimeException.
          const protoScala::gen::HandlerRec* h = protoScala::gen::handlerFor(blk3_rec, pc);
          if (!h) throw;
          S[F.pendingSlot()] = protoScala::gen::materialise(C, "RuntimeException", e.what());
          S[h->slot] = S[F.pendingSlot()];
          resumePc = h->handlerPc;
          continue;
        }
      }
  ```
  Five properties, each deliberate. **The `switch` is inside the `try`**, because C++ forbids jumping *into* a try block and permits jumping within one — so every label and the dispatch that reaches it live in the same `try`, and the handler body is therefore re-protected by the frame's own table exactly as the interpreter's `continue` re-protects it. **`ScalaThrow::value` is re-rooted into `returnValue` and `pendingSlot()` as the first two statements of the catch**, because A0-2 / escalation E1 fixed that protocol: the only window in which the value is unrooted is between one frame's context being destroyed and the next frame's catch, and nothing in that window allocates. **`materialise`'s result goes into `pendingSlot()` before `S[h->slot]`**, because `materialise` allocates and a `ProtoObject*` may not be held in a C++ local across an allocation (P1, P4 rule 3); the double write is not redundant, it is the rule. **The catch order is `ScalaThrow`, `ScalaError`, `std::runtime_error`** — `ScalaError` derives from `std::runtime_error`, so an arm in the other order would rewrite every precise class name to `RuntimeException`; `ScalaThrow` deliberately derives from `std::exception` and **not** from `std::runtime_error` for the same reason, which `tests/unit/test_exceptions.cpp` already pins with a `static_assert`. **`std::logic_error` is absent by construction, not by omission**: it does not derive from `std::runtime_error`, so it propagates through all three arms untouched, which is what makes D74 hold on the generated path. `FutureYield` likewise propagates, since it is not a `std::exception` at all.
- [ ] **Step 2 — `pc` tracking.** Emit `pc = <n>;` before every instruction that can throw — every `gen::` call, every send, every arithmetic op — and nowhere else. The handler search then reads the same word index `runLoop`'s own catch reports, so `handlerFor` finds the same entry. A `pc` assignment before a class-V opcode is dead and is not emitted.
- [ ] **Step 3 — `THROW` and `RETHROW`.** `THROW` → `protoScala::gen::throwValue(C, S[d-1]);` which checks the `@Throwable` marker and raises `"throw expects a Throwable, got <type>"` otherwise, from the one implementation. `RETHROW n` → `protoScala::gen::rethrow(C, S[n]);`. Both are `[[noreturn]]`.
- [ ] **Step 4 — `finally`.** A `HandlerKind::Finally` entry's body is emitted like a catch body and ends with the `RETHROW` the compiler already emitted. Nothing special is needed, because `finally` is already a handler in the table — which is the payoff of consuming bytecode rather than AST.
- [ ] **Step 5 — Stack depth on handler entry.** `enterHandler` resets the operand stack to the `try`'s entry depth: `sp = slots + stackBase + h.stackDepth`. The emitter knows `h->stackDepth` at emit time, so it sets its own `depth` to `stackBase + h->stackDepth` when it begins emitting the handler body — a compile-time assignment, not a run-time one. Assert it: if the emitter's tracked depth at `handlerPc` disagrees with `stackBase + h->stackDepth`, throw `std::logic_error` naming the block and the pc. A depth disagreement is a generator bug that would otherwise surface as a wrong value.
- [ ] **Step 6 — D74 on the generated path.** Add two cases to `tests/unit/test_exceptions.cpp`: (a) a generated frame that raises `std::logic_error` is **not** caught by a Scala `catch { case e: Throwable => }` and the `std::logic_error` reaches the caller; (b) a generated frame whose handler body itself throws is caught by the same frame's next matching entry — the second-exception property, which is the one that was worth 11 fixtures.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && cmake --build build_release -j4 && \
ctest --test-dir build_release -R 'unit.*exceptions' --output-on-failure < /dev/null 2>&1 | tail -3
```
passes with the two new cases, and
```bash
cd /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler && \
P=/home/gamarino/Documentos/proyectos/protoScala && \
$P/build_release/protoscalac $P/tests/conformance/20-exceptions/finally-runs-when-catch-does-not-match.scala --build-so && \
grep -c 'catch (protoScala::ScalaThrow' *.cpp
```
prints `1` — one retry loop per block, not one per `try`.

---

## Task 9: `proto_module_init`, identity, exports and `@main`

**Files:** `src/compiler/CppEmitter.cpp` (modify), `src/runtime/GeneratedSupport.cpp` (modify), `src/compiler/CppTables.cpp` (modify — `emitExports`).

**Interfaces:**

```cpp
// emitted by CppEmitter into every generated file
extern "C" void* proto_module_init();
extern "C" const char* proto_module_version_v1();   // "" when none (D4)
extern "C" const char* proto_module_language_v1();  // "protoScala"
extern "C" const void* proto_module_exports_v1();   // &<unit>_exports, or nullptr in script mode
extern "C" int  proto_module_main(int argc, char** argv);  // script mode only (D8)
```

```cpp
// include/protoScala/GeneratedModule.h — added in this task, with an ABI note
namespace protoScala::gen {
/** The ClassInfo / GlobalBinding / by-name-selector tables a compiled module exports,
 *  so an `import` of it keeps EARLY TYPE BINDING instead of degrading to foreign. */
struct ExportsRec {
    std::uint32_t abiVersion;             // kGeneratedModuleABI
    const char* moduleName;
    const char* moduleKey;
    const char* moduleTypeKey;
    const struct BindingRec* bindings; std::size_t bindingCount;
    const struct TypeRec* types;       std::size_t typeCount;
    const struct SelectorRec* selectors; std::size_t selectorCount;
};
}
```

- [ ] **Step 1 — `proto_module_init`.** Emit:
  ```cpp
  extern "C" void* proto_module_init() {
      proto::ProtoContext* ctx = protoScala::gen::currentContext();
      protoScala::gen::linkModule(ctx, kAllBlocks, kAllBlockCount);
      return const_cast<void*>(static_cast<const void*>(
          protoScala::gen::runModuleBody(ctx, &blk0, "util.Strings", "")));
  }
  ```
  `gen::currentContext()` resolves the calling context from `activeCallContext()` and throws `std::logic_error` when there is none, naming the function — a module initialised outside a protoScala call context is a host defect, and D74 keeps it uncatchable. `runModuleBody` is D6 site 1.
- [ ] **Step 2 — `emitExports`.** Walk the `GlobalTable` the transpile produced and emit `BindingRec`, `TypeRec` (the `ClassInfo` fields: name, key, kind, flags, linearization, fields, constructor keys, aux arities, primary mask, members) and `SelectorRec` (the by-name selector index) tables — the same record shapes `support/PreludeImage.h` already defines for the prelude, reused by name where the layout is identical and renamed into `gen::` where it must be installed rather than internal. **This is the step that makes a compiled module a first-class import target**: without it, `import util.Strings` of a `.so` would bind late like a foreign module and `case Point(x, y) =>` would not compile, because a name bound at run time carries no `ClassInfo` — which is exactly what `ModuleLoader.h`'s leading comment records as the reason the dialect binds imports early.
- [ ] **Step 3 — Script mode.** When `opts_.asScript`, emit
  ```cpp
  extern "C" int proto_module_main(int argc, char** argv) {
      proto::ProtoContext* ctx = protoScala::gen::currentContext();
      return protoScala::gen::runMain(ctx, "hello", /*takesArgs*/false, argc, argv);
      //                                  ^ CompiledUnit::mainKey, not mainName
  }
  ```
  using `CompiledUnit::mainName`, `mainKey` and `mainTakesArgs`. `gen::runMain` looks the global up, calls it through `ExecutionEngine::callTopLevel`, translates an uncaught Scala exception into the same stderr shape `Session::runScript` produces, and returns 0 or 1. **One implementation of the uncaught-exception report**, so the differential harness compares identical text.
- [ ] **Step 4 — Imports at load time.** Each `import` in the unit compiled to a `PUSH_GLOBAL` of a session global the transpile-time loader filled. The generated module must fill the same global at load time, so `proto_module_init` emits, before the body, one line per import:
  ```cpp
  protoScala::gen::importModule(ctx, /*providerSpec*/"", "util.Strings", "/abs/dir/of/source");
  ```
  `gen::importModule` is the interpreter's own D90 path: resolve, load, force the module object singleton, bind the module global. The `importerDir` is baked in as the absolute directory of the transpiled source, because that is what the interpreter used and a module resolved differently would be a different module. The specification records that a module moved after transpiling resolves its imports from its original directory, and that `PROTOSCALA_PATH` still applies.
- [ ] **Step 5 — Unit test the exports round-trip.** Extend `tests/unit/test_cpp_tables.cpp`: transpile a two-class module to C++, compile it in the test's own scratch directory with the emitted `Makefile`, `dlopen` it, read `proto_module_exports_v1()`, and assert the `TypeRec` count, the first type's key and its linearization match the `ModuleExports` the in-process `Session` produces for the same file. Byte-for-byte on the keys, because a key that differs by one byte is the 6-versus-7-byte interning bug again.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler && \
P=/home/gamarino/Documentos/proyectos/protoScala && \
$P/build_release/protoscalac $P/tests/conformance/24-modules/_modules/util/Strings.scala \
    --as-module --module-name util.Strings --build-so && \
nm -D --defined-only module.so | grep -E 'proto_module_(init|version_v1|exports_v1|main)'
```
lists `proto_module_init`, `proto_module_version_v1` and `proto_module_exports_v1` and **does not** list `proto_module_main`; and the same command on a fixture with an `@main` does list it.

---

## Task 10: `CompiledModuleProvider` and the module driver

**Files:**
- `src/umd/CompiledModuleProvider.h` / `.cpp` (**create**).
- `src/repl/Session.cpp` (modify) — register the provider, add `runModule`.
- `src/main.cpp` (modify) — `--run-module`.
- `tests/unit/test_compiled_provider.cpp` (**create**), `tests/cli/run-module.sh` (**create**).
- `tests/umd/foreign-caller.cpp` (**create**) — the cross-runtime-call demonstration.

**Interfaces:**

```cpp
// src/umd/CompiledModuleProvider.h
namespace protoScala {

/** protoCore UMD provider for compiled modules. Alias "compiled",
 *  GUID "protoScala-compiled-v1" (D4). Loads any conforming .so, generated or
 *  hand-written: only proto_module_init is required. */
class CompiledModuleProvider : public proto::ModuleProvider {
public:
    explicit CompiledModuleProvider(std::vector<std::string> basePaths);
    ~CompiledModuleProvider() override;
    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                      proto::ProtoContext* ctx) override;
    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }
private:
    std::vector<std::string> basePaths_;
    std::string guid_, alias_;
    std::mutex mutex_;
    std::map<std::string, void*> loadedHandles_;
};

void installCompiledProvider(proto::ProtoContext* ctx, std::vector<std::string> basePaths);
}
```

```cpp
// src/repl/Session.h — added
/** Loads a compiled module and runs it as a program: proto_module_init, then
 *  proto_module_main when present. Returns the process exit code. */
int runModule(const std::string& soPath, const std::vector<std::string>& args);
```

- [ ] **Step 1 — `tryLoad`, and the shape INTEROP §6 prescribes.** Replace `.` with `/` in `logicalPath`, look for `<name>.so` under each base path, `dlopen(RTLD_NOW | RTLD_GLOBAL)`, `dlsym("proto_module_init")`. Keep one `dlopen` reference per logical path for the provider's lifetime and `dlclose` a duplicate of the same handle, so code an existing module object may still run is never unloaded. On a miss return `PROTO_NONE`, never `nullptr` — `PROTO_NONE` is the protoCore convention and comparing against the wrong sentinel dereferences `321UL` (P4 rule 6).

  **The provider carries its own state and uses `ctx` only to allocate the result in the caller's context.** INTEROP §6 records this as a rule learnt the hard way: `ScalaModuleProvider` resolves its host through the space-keyed `moduleHostForSpace`, and *"therefore still answers only protoScala's own callers"* — Track Y measured a provider reporting a module that was there as absent, and protoST fixed it in the provider with **no protoCore change**, because *"a `ModuleProvider` is an object with its own state"*. `CompiledModuleProvider` is written that way from the first line: its base paths and handle map are members, and it never looks anything up by `ctx->space`. Note also that **protoCore ships no `dlopen` provider at all** — `FileSystemProvider` is its only one — so this is the family's second compiled provider after protoPython's, and the first in protoScala.
- [ ] **Step 2 — Refuse a script (D8).** If `dlsym("proto_module_main")` is non-null, `dlclose` and raise `ScalaError("ImportError", "a module may not define an @main method")`. The wording matches `tests/conformance/24-modules/module-with-a-main.scala`'s `// EXPECT-ERROR`, so one fixture covers both paths.
- [ ] **Step 3 — Identity and rooting.** Build
  ```cpp
  const char* v = "";
  if (auto* f = reinterpret_cast<const char*(*)()>(dlsym(handle, "proto_module_version_v1"))) v = f();
  const proto::ModuleIdentity id = (*v == '\0')
      ? proto::ModuleIdentity::unversioned(guid_, logicalPath)
      : proto::ModuleIdentity(guid_, logicalPath, v);
  if (const proto::ProtoObject* cached = proto::ProtoSpace::findModule(id)) return cached;
  ```
  then run the initializer, then **`return ctx->space->registerModule(id, mod);`** and nothing else. `registerModule` is **publish-or-adopt**: it probes `sharedModuleCacheGet(id)`, adopts and roots an existing entry if one appeared meanwhile, otherwise inserts and roots the new one — so calling `addModuleRoot` as well would root the module twice in an append-only table that never removes an entry. **The version accessor is optional**, so a hand-written module that defines only `proto_module_init` gets the empty version — P3's permanent, first-class "declares no version" value, reserved so that a future manifest cannot re-alias it, and never a wildcard. Add a fixture proving a compiled `util.Strings` and a source `util.Strings` are two modules (different GUIDs, per §D4) and that loading both leaves two module roots: `proto::ProtoSpace::moduleRootCount()` increases by exactly 2.
- [ ] **Step 4 — Register the provider.** `installCompiledProvider` uses `std::call_once` and prepends `provider:compiled` to the space's resolution chain **after** `provider:scala`, so a `.scala` beside a `.so` still wins and the change is additive for every existing fixture. Base paths: `PROTOSCALA_MODULE_PATH` (`:`-separated) first, then `<prefix>/<libdir>/protoscala/modules`, created by `install(DIRECTORY DESTINATION ...)` in Task 14 so the path `--version` prints exists.
- [ ] **Step 5 — `--run-module`.** `protoscala --run-module <path.so> [args...]` calls `Session::runModule`, which `dlopen`s the file, calls `proto_module_init` under the session's root context, then `proto_module_main(argc, argv)` when present, and returns its code. Without `proto_module_main` it returns 0 and prints nothing — a module has no output of its own. `tests/cli/run-module.sh` checks: a missing file exits 1 with a message naming the path; a `.so` with neither symbol exits 1 with `"not a protoScala module: proto_module_init not found"`; the transpiled `hello.scala` prints `Hello, protoScala!` and exits 0.
- [ ] **Step 6 — The cross-runtime-call demonstration, which is the phase's claim made testable.** `tests/umd/foreign-caller.cpp` is a GoogleTest case that includes **`protoCore.h` only** — no protoScala header, no `protoScala::` name anywhere in the file, enforced by a `grep` in the test's own CMake comment and by the fact that it links `protoCore` alone plus the `.so`. It: creates a `ProtoSpace`; obtains the module object by `dlopen` + `proto_module_init` through a tiny host shim that is *linked separately*; reads the exported function object's attribute; calls `obj->asMethod(ctx)` and invokes the resulting `proto::ProtoMethod` with a `ProtoList` of one `SmallInteger`; and asserts the result. It then repeats the same call against the **interpreted** module and asserts the two results are equal. Record in the test's comment, verbatim: *"This is the capability Phase 7 adds. The equivalent call against a protoST method is impossible, because a protoST method is `__bc_ptr__` plus protoST's engine rather than a `proto::ProtoMethod`; the same was true of a protoScala compiled function before this phase."* Record equally plainly that the caller shares the module's `ProtoSpace`, and that the cross-space case remains undemonstrated (R5).

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && cmake --build build_release -j4 && \
ctest --test-dir build_release -R 'compiled_provider|foreign_caller|cli/run-module' \
      --output-on-failure < /dev/null 2>&1 | tail -4
```
passes all three, and `grep -c protoScala tests/umd/foreign-caller.cpp` prints `0` outside the comment block.

---

## Task 11: The differential conformance harness

**This task is the phase's strongest asset and it is a first-class deliverable, not a verification afterthought.** protoScala's suite (1263 cases when this was written, 1344 on 2026-09-25 — measure it) includes **859** files under `tests/conformance/**/*.scala` are fixtures (about 18 are `_`-prefixed helpers, leaving roughly **841** registered: 693 `// EXPECT:`, 142 `// EXPECT-ERROR`, 6 `// XFAIL`). If the transpiler is correct, **every such fixture must produce the same observable output transpiled as interpreted.** That is enormous coverage for free, and for a code generator it is the only practical way to earn trust.

**Files:**
- `tests/conformance/run-transpiled.sh` (**create**).
- `tests/transpile-exclude.txt` (**create**) — the exclusion list, with a reason per line.
- `tests/CMakeLists.txt` (modify) — register one `transpiled/<fixture>` case per non-excluded fixture.

**Interfaces:**

```bash
# tests/conformance/run-transpiled.sh
#   Usage: run-transpiled.sh <protoscala> <protoscalac> <file.scala> <exclude-file> <scratch-dir>
# Exit 0 = the transpiled run agreed with the fixture's directive.
```

```text
# tests/transpile-exclude.txt — "<relative fixture path>  <reason-code>  <reason>"
13-actors/await-one-worker.scala            D103  await cannot suspend a transpiled frame
06-recursion/stack-overflow.scala           D104  native frame size differs; the depth is not comparable
```

- [ ] **Step 1 — The runner, reusing the existing directive parser byte for byte.** Copy `tests/conformance/run.sh`'s `case "$first_line" in` block **verbatim** — the same five directive forms, the same `${first_line#// EXPECT: }` extraction — so the two harnesses cannot disagree about what a fixture asks for. Then replace the single interpreter run with four stages, whose outputs are concatenated in order into one `stdout_file` and one `stderr_file`:
  1. **transpile** — `protoscalac "$FILE" --build-so` in a private scratch directory, `stderr` captured;
  2. **compile** — `make` (run by `--build-so`), `stderr` captured;
  3. **run** — `protoscala --run-module module.so [args]`, from the same working directory the interpreted run would use (`PROTOSCALA_RUN_CWD` honoured, `PROTOSCALA_PATH` and `PROTOSCALA_TEST_TMP` passed through), under `timeout 90s`;
  4. **verdict** — the existing `matches_expect` / `matches_expect_error` functions, unchanged.
- [ ] **Step 2 — The guard that stops the harness becoming a test that cannot fail.** Three rules, and each one exists because its absence is a way for the harness to pass while proving nothing:
  - **A C++ compile failure is always a harness FAIL**, never a pass. Without this, an `EXPECT-ERROR` fixture whose expected substring happened to appear in a g++ diagnostic would go green on a broken generator.
  - **A transpile refusal is a FAIL unless the fixture is on the exclusion list**, and then it is a pass with the reason printed.
  - **A fixture on the exclusion list that transpiles, compiles and runs successfully is a FAIL**, with the message `"<file> is excluded as <reason-code> but now works — remove it from tests/transpile-exclude.txt"`. This is the bidirectional check that keeps the list from rotting, and it is the same discipline the `XFAIL` directive already uses ("FAIL: XFAIL passed unexpectedly — remove the XFAIL marker").
- [ ] **Step 3 — Register the cases.** In `tests/CMakeLists.txt`, reuse the existing `PROTOSCALA_CONFORMANCE_FILES` glob and the `_`-prefix helper rule, and add a second `add_test` per fixture named `transpiled/${conf_file}`, passing the exclusion file and a per-fixture scratch directory under `${CMAKE_CURRENT_BINARY_DIR}/transpiled/`. Copy the existing per-directory `ENVIRONMENT` blocks (`26-file-io` → `PROTOSCALA_TEST_TMP`, `24-modules` → `PROTOSCALA_PATH`, `tutorial/16-*` → `PROTOSCALA_RUN_CWD`, `tutorial/00-readme-a-module` → its own `PROTOSCALA_PATH`) so a transpiled fixture runs in the same environment as its interpreted twin. Give the transpiled cases `TIMEOUT 300` — each one runs a C++ compiler.
- [ ] **Step 4 — Write the exclusion list, small and enumerated.** Expected content, **46 entries**, every one of them named:

  | Group | Count | Reason code | Why |
  |---|---|---|---|
  | fixtures that use `await` | **38** | **D103** | cooperative suspension snapshots a bytecode frame (`__mod__`, `__ip__`, `__fbase__`, `__fslots__`); a transpiled frame has no `ip` and cannot be rebuilt. `nativeReentryDepth()` already refuses to suspend above depth 1 (D43). `protoscalac` **refuses** these at transpile time (§D5), so the exclusion is checked in both directions by Step 2 |
  | `06-recursion/stack-overflow.scala`, `20-exceptions/native-stack-overflow-is-catchable.scala` | **2** | **D104** | the generated frame's native size differs from the interpreter's, so the depth at which `StackOverflowError` fires is not comparable. The *class* is still raised; only the depth differs, and neither fixture asserts a depth |
  | the six `// XFAIL` fixtures | **6** | **X** | an `XFAIL` asserts *failure*, and the transpiled path's failure has a different cause: `07-classes/this-{escapes,identity}-in-constructor{,-braces}.scala` pin an unimplemented constructor semantics, and `23-named-arguments/foreign-python-{keyword,open-encoding}.scala` pin the absent `py` provider. Comparing two different failures proves nothing |

  **Coverage: 841 − 46 = 795 fixtures run differentially**, which is **94.5 %** of the registered conformance suite.

  Three exclusions the maintainer might expect and which are **not** needed, stated because an unexplained absence looks like an oversight:
  - **No fixture is excluded for needing the REPL.** The REPL is exercised by `tests/cli/repl-*.sh`, not by conformance fixtures; there are **zero** REPL-dependent `.scala` fixtures. §D5 keeps the REPL on the interpreter and no conformance coverage is lost.
  - **No fixture is excluded for asserting an interpreter-specific error prefix.** All 142 `EXPECT-ERROR` texts are substrings of messages produced by the *shared* implementation: a compile error comes from `protoscalac` running the same `Compiler` (`"Reassignment to val x"`, `"needs to be abstract, since def area is not defined"`, `"must be defined at the top level"`) and a runtime error comes from the same prelude and the same `ops::` bodies (`"MatchError: 5 (of class Int)"`, `"ClassCastException: String cannot be cast to Int"`, `"NoSuchElementException: key not found: 9"`). The runner matches with `grep -F` over the concatenated stderr and stdout, so which *stage* produced the text does not matter — which is why §D5 could avoid classifying fixtures by error kind at all.
  - **No `24-modules` or `25-interop` fixture is excluded.** They are the highest-risk group (transpile-time import resolution per §D7 plus load-time re-import per Task 9 Step 4) and Step 7 verifies them explicitly, but risk is a reason to look, not to exclude.
- [ ] **Step 5 — Make the harness self-report.** Each case prints one line on success — `transpiled OK: <file> (transpile <ms>, compile <ms>, run <ms>)` — and the CMake summary target `transpiled-summary` prints the totals: fixtures attempted, passed, excluded by reason code, and total C++ compile time. A run whose numbers are all zero is a broken harness that looks like a clean suite, which is the failure this project has caught five times (memory: silent bench failures fooled the harness; benchmarks must self-report).
- [ ] **Step 6 — First full run, and triage.** `ctest --test-dir build_release -R '^transpiled/' --output-on-failure < /dev/null -j4`. Record the first-run result in `.agent_scratch/phase7-transpiler/differential-run-1.md` **before fixing anything**: the count, and one line per failure with the fixture, the stage that failed and the diff. Then fix the generator, not the fixture and not the list. A fixture that cannot be made to pass joins the exclusion list **only** with a reason code, a one-sentence justification and the maintainer's agreement — and the list's total is reported in Task 17's STATUS entry, so it cannot grow quietly.
- [ ] **Step 7 — Verify the two highest-risk groups by hand.** For all 30 `24-modules` and 19 `25-interop` fixtures, additionally confirm: the transpiled module's imports resolved from the *baked-in* `importerDir` and not from the harness's working directory (check by running the same `.so` from a different directory and asserting the same output); and a transpiled module that imports another transpiled module works (transpile `_modules/util/Strings.scala` to `util/Strings.so`, put it on `PROTOSCALA_MODULE_PATH`, transpile the importer, and run). Record both in `differential-run-1.md`.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && \
ctest --test-dir build_release -R '^transpiled/' --output-on-failure < /dev/null -j4 2>&1 | tail -3 && \
wc -l tests/transpile-exclude.txt
```
reports **795 tests passed, 0 tests failed** and an exclusion file of **46** content lines (plus its header comment), each with a reason code.

---

## Task 12: The mutation matrix

**Every differential case must be shown to fail under a named mutation of the generator.** A code-generator test suite that has never been red is a suite whose coverage is unknown.

**Files:** `tests/mutations/` (**create**) — one patch file per mutation; `.agent_scratch/phase7-transpiler/mutations.md` (created); `docs/PROTOSCALAC_SPECIFICATION.md` (Task 16 quotes the table).

**Interfaces:** each mutation is a `git apply`-able patch against `src/compiler/CppEmitter.cpp` or `src/runtime/GeneratedSupport.cpp`, plus a declared **expected red set**.

- [ ] **Step 1 — Define the twelve mutations, each with the construct it corrupts and the fixtures that must red.**

  | id | Mutation | Expected red (minimum) |
  |---|---|---|
  | **M1** | `gen::add` emits `gen::sub` | every arithmetic fixture; `02-expressions/*`, `15-integers/*` |
  | **M2** | `SEND`'s D5 fallback name is passed as `nullptr` | `07-classes/class-private-member*.scala` |
  | **M3** | `MAKE_FN` drops the last capture | `05-functions/*closure*`, `12-for-comprehensions/*` |
  | **M4** | `MAKE_CLASS` emits the parents in reverse | `07-classes/*linearization*`, `22-super-and-extensions/*` |
  | **M5** | the handler-entry stack depth uses `0` instead of `h->stackDepth` | `20-exceptions/*finally*`, `20-exceptions/catch-does-not-match-propagates.scala` |
  | **M6** | `JUMP_BACK` omits `gen::safepoint(C)` | `cli/gc-pressure` under `PROTOCORE_HEAP_LIMIT_CELLS=20000`; **P4 rule 1** |
  | **M7** | `catch (const std::logic_error&)` removed from `ForeignBoundary.h` | the D74 unit case of Task 8 Step 6 |
  | **M8** | `materialise`'s result is held in a C++ local instead of `pendingSlot()` | the ASan/GC-pressure run of Step 3; **P4 rule 3** |
  | **M9** | `emitConsts` writes `fromUTF8String` instead of `createSymbol` for a `Symbol` | any fixture whose attribute name exceeds 6 ASCII bytes; **P4 rule 4** (short names match by accident, which is the trap) |
  | **M10** | `UNAPPLY_FIELDS` returns one fewer field | `11-pattern-matching/*unapply*`, `08-case-classes/*` |
  | **M11** | `proto_module_version_v1` returns `"0.0.0"` instead of `""` | the Task 10 Step 3 aliasing fixture; **P3 §D11** |
  | **M12** | `emitExports` omits the `TypeRec` table | `24-modules/*` type imports; the Task 9 Step 5 round-trip |

- [ ] **Step 2 — Run each mutation and record the actual red set.** For each: `git apply tests/mutations/Mn.patch`, rebuild `protoscalac`, run the transpiled suite, record the failing case list, `git apply -R`. Write the table into `mutations.md` with **expected** and **actual** columns. A mutation whose actual red set is empty is a **hole in the harness** and the task is not done until a case is added that covers it — that is the point of the exercise, not a formality.
- [ ] **Step 3 — Two mutations need a sanitiser to show.** M6 and M8 are GC-window bugs and a green functional suite does not see them. Run the transpiled `cli/gc-pressure` fixture under `PROTOCORE_HEAP_LIMIT_CELLS=20000` and under AddressSanitizer (`-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fsanitize=address`) in a separate build directory, and record that each mutation is red there and green without it. **Assert reclamation proportionally, never `> 0`:** the fixture self-reports the cells it allocated and the assertion is `reclaimed >= 0.5 * allocated`. Track Y found a forced cycle reclaiming 4–7 cells instead of 205,120 while a `> 0` assertion passed.
- [ ] **Step 4 — Publish the matrix.** The table goes into `docs/PROTOSCALAC_SPECIFICATION.md` §6, so a future change to the emitter has a list of the twelve things the harness is known to catch and, by omission, an honest statement of what it is not known to catch.

**Done when:** `cat /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/mutations.md` shows twelve rows with a non-empty **actual** red set each, M6 and M8 rows citing the sanitiser run, and no row whose actual set is empty.

---

## Task 13: What the first cut refuses, and how

**Files:** `src/compiler/CppEmitter.cpp` (modify — `check()`), `src/compiler/TranspilerMain.cpp` (modify — `--report-purity`), `tests/cli/transpiler-refusals.sh` (**create**), `scripts/` (no new script — protoCore's checker is invoked).

**Interfaces:**

```cpp
// CppEmitter::check(), already declared in Task 6
std::vector<Refusal> check(const CompiledUnit& unit) const;

// TranspilerMain.cpp
/** --report-purity: prints what this module needs and why, per §D2's four conditions. */
static int reportPurity(const CompiledUnit& unit, const GlobalTable& globals);
```

- [ ] **Step 1 — `check()` walks first and emits nothing.** It visits every block and every instruction, collects a `Refusal{message, line}` per unsupported construct, and `main` prints them all as `<file>:<line>: error: <message>` and exits 1 without opening the output file. A partially written `.cpp` that a later `make` compiles into something is worse than no output.
- [ ] **Step 2 — The refusals of the first cut, exhaustively.** `await` inside any block: `"await is not supported in a transpiled module (D103): cooperative suspension snapshots a bytecode frame"`, detected as a `SEND`/`SEND_APPLY` site whose `ConstRec::sval` is `await` — an over-approximation that also refuses a user method named `await`, which is the safe direction and is recorded in the specification. Any opcode with no `emitInstr` arm: `"opcode <n> is not supported by protoscalac"`. `UnitMode::Repl`: `"protoscalac compiles files, not REPL input (D105)"`. `--as-module` on a unit with an `@main`: `desugarModule`'s own D91 message, unchanged.
- [ ] **Step 3 — `--report-purity` (§D2).** Print one line per reason the module needs protoScala, keyed to §D2's four conditions, and a verdict:
  ```text
  util/Strings.scala needs protoScala:
    ADD at Strings.scala:7          protoScala numeric semantics (Int = Long = BigInt, D1)
    PUSH_GLOBAL println             a prelude global
    MAKE_CLASS Point                a linearized class
  verdict: not protoCore-pure. Loading this module adds a protoScala runtime and
  therefore one ProtoSpace term to the process sizing rule (protoCore MemoryModel.md).
  ```
  Two fixtures: one ineligible (any real module) and one **eligible** — a unit whose opcodes are a subset of §D2's list, e.g. `def pick(a: Boolean, b: Int, c: Int): Int = if a then b else c` with no `println` — which must report `verdict: protoCore-pure (emission under --pure is not offered; see D2)`. The eligible fixture exists so that the classification itself has a passing case, not only failing ones.
- [ ] **Step 4 — Run protoCore's static checker over the *generated* output.** Generated code is an embedder and P4's rules apply to it. The repository already carries the ratchet for its own sources as `conformance-allow.txt` (one entry today, `blocking_join_unbracketed src/runtime/StackGuard.cpp:145`), whose format is `<check-id> <path>:<line> <sha1-12-of-the-line-text> :: <justification>` and whose command is
  ```bash
  python3 ../protoCore/scripts/conformance/check_static.py --repo .
  ```
  (exit 0 clean, 1 an unjustified finding, 2 a stale entry). Generate ten representative fixtures into `.agent_scratch/phase7-transpiler/generated/`, run the checker over that directory with a second ratchet file `tests/transpiled-allow.txt` in the **same format**, and expect it clean for `local_across_alloc`, `symbol_key_source`, `attr_sentinel` and `critsec_across_block`. **Any hit is a generator bug, not an allowlist entry** — DECISIONS-LOG records the standing rule that justifying a defect would make the ratchet a laundering mechanism. Record the result; if the checker is absent from the installed protoCore, record that and run the four patterns its documentation specifies instead.
- [ ] **Step 5 — The CLI test.** `tests/cli/transpiler-refusals.sh`: `await` fixture exits 1 with the D103 message and the correct line number, and **no** `.cpp` is written; `--as-module` on an `@main` fixture exits 1 with the D91 message; `--report-purity` on the ineligible fixture exits 0 and prints `verdict: not protoCore-pure`; on the eligible fixture exits 0 and prints `verdict: protoCore-pure`.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && \
ctest --test-dir build_release -R 'cli/transpiler-refusals' --output-on-failure < /dev/null && \
ls /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/*.cpp 2>&1 | wc -l
```
passes, and no `.cpp` was left behind by the refused fixture.

---

## Task 14: Packaging and installation

**Files:** `CMakeLists.txt` (modify), `docs/INSTALLATION.md` (modify), `tests/cli/package.sh` (modify).

**Interfaces:** none new.

- [ ] **Step 1 — Install what a module author needs.** `protoscalac` to `${CMAKE_INSTALL_BINDIR}`, `libprotoScala.so*` to `${CMAKE_INSTALL_LIBDIR}`, `include/protoScala/GeneratedModule.h` to `${CMAKE_INSTALL_INCLUDEDIR}/protoScala`, the CMake package to `${CMAKE_INSTALL_LIBDIR}/cmake/protoScala`, and an empty `${CMAKE_INSTALL_LIBDIR}/protoscala/modules` for compiled modules, beside the existing `providers` directory. Set `INSTALL_RPATH` on `protoscalac` to `$ORIGIN/../${CMAKE_INSTALL_LIBDIR}` (`@executable_path/..` on Apple), as `protoscala` already has.
- [ ] **Step 2 — Keep `protoscala-precompile` uninstalled.** It is a build tool and shipping it would ship a binary no user runs; the existing comment says so and stays.
- [ ] **Step 3 — CPack.** Add the new files to the `protoScala` component. `CPACK_DEBIAN_PACKAGE_DEPENDS` keeps its `protocore (>= …), protocore (<< …)` relation unchanged; `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` stays **off** for the reason already recorded (no distribution owns `libprotoCore.so.3`, and `dpkg-shlibdeps` would take the whole `.deb` down). Add `g++` and `make` to `CPACK_DEBIAN_PACKAGE_RECOMMENDS`, not `DEPENDS`: `protoscalac --emit-cpp` needs neither, and only `--build-so` does.
- [ ] **Step 4 — Extend `tests/cli/package.sh`** to assert the package contains `bin/protoscalac`, `lib/libprotoScala.so.1`, `include/protoScala/GeneratedModule.h` and `lib/cmake/protoScala/protoScalaConfig.cmake`.
- [ ] **Step 5 — Prove the installed path end to end, in a scratch prefix.** Never a system prefix:
  ```bash
  S=/home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler
  cd /home/gamarino/Documentos/proyectos/protoScala
  cmake -B "$S/build-inst" -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$S/prefix"
  cmake --build "$S/build-inst" -j4 && cmake --install "$S/build-inst"
  cd "$S" && env -u LD_LIBRARY_PATH "$S/prefix/bin/protoscalac" \
      /home/gamarino/Documentos/proyectos/protoScala/tests/conformance/00-binary/hello.scala --build-so
  env -u LD_LIBRARY_PATH "$S/prefix/bin/protoscala" --run-module ./module.so
  ldd ./module.so | grep -E 'protoScala|protoCore'
  ```
  The program must print `Hello, protoScala!` **without** `LD_LIBRARY_PATH`, which is what the generated `-Wl,-rpath` entries are for, and `ldd` must resolve both `libprotoScala.so.1` and `libprotoCore.so.3` from inside the prefix. Record the `ldd` output: it is §D2's honest statement, printed by a tool rather than asserted by a document.
- [ ] **Step 6 — `docs/INSTALLATION.md`.** Add a section: what `protoscalac` needs at build time (`g++`, `make`), the three environment overrides, the `PROTOSCALA_MODULE_PATH` search order, and the rename-to-`<module>.so` rule the generated `Makefile`'s fixed `module.so` target implies.

**Done when:** the Step 5 command block prints `Hello, protoScala!` and an `ldd` listing naming both libraries inside `$S/prefix`, and `ctest --test-dir build_release -R cli/package --output-on-failure < /dev/null` passes.

---

## Task 15: Measurement — what moved, and what did not

**Files:** `benchmarks/transpiled/` (**create**), `benchmarks/RESULTS.md` (modify), `.agent_scratch/phase7-transpiler/measurements.md` (created).

**Interfaces:** none new. Each benchmark is an existing `benchmarks/*.scala` run twice: interpreted and transpiled.

- [ ] **Step 1 — Start-up, whose criterion is no regression.** Measure script and REPL start-up three rounds interleaved, exactly as Phase 6 did, and compare against Task 1 Step 5's recorded 23.73 / 23.89 ms. **The acceptance criterion is no regression beyond measurement noise**, not an improvement: Task 2 changes the executable from statically linked runtime to `libprotoScala.so`, which adds one `dlopen`-time relocation pass, and the phase's stated position is that the remaining cold-start headroom is the **4.0 %** that running the compiled prelude costs. A regression above 3 % is a finding to fix before the phase closes.
- [ ] **Step 2 — Transpiled module load time, measured against the thing it replaces.** For `hello.scala` and for a 200-line module, compare: interpreter (parse + desugar + compile + link + run) versus transpiled (`dlopen` + `linkModule` + run). Report both absolute numbers. Expect the transpiled path to win on the front end and to pay `linkModule`, which is the same interning work `linkSymbols` does — so the honest headline is *"the front end is gone; interning is not"*.
- [ ] **Step 3 — Throughput, with the expectation written down first.** Run `fib`, `tak`, `sum_loop`, `list_ops`, `map_build`, `attr_lookup` and `object_tree` interpreted and transpiled. **The written expectation is a modest win from removing the dispatch `switch` and no change at all on send-bound and allocation-bound workloads**, because the generated code calls the same `ops::` bodies through the same dynamic paths. Record the table. If a benchmark wins by a large factor, **explain it before reporting it** — the likely explanations are that the benchmark was dispatch-bound or that the generated code accidentally skipped a check, and the second is a bug. If a benchmark loses, that is also a finding: one long function per block can defeat register allocation, which is what Step 4's `-O2` choice anticipates.
- [ ] **Step 4 — The cost side, which a performance-flavoured table must carry.** Record, per benchmark: `protoscalac` time, `g++` time at `-O2` and at `-O3`, the generated `.cpp` line count, and the stripped `module.so` size. A generated module is one long function per block, and `-O3`'s inlining makes compile time superlinear in block size; the table is what justifies `-O2` in the emitted `Makefile` (Task 4 Step 3) or overturns it.
- [ ] **Step 5 — Every benchmark self-reports and the runner verifies.** Each transpiled benchmark prints the computed result and the total work done, and the runner asserts the **value**, never the exit code. protoPython's sprint-9 "wins" were crashes that the harness read as successes (memory: silent bench failures fooled the harness), and `memory_pressure` is excluded from the set by standing decision because protoCore's GC defers collection by design (memory: memory_pressure benchmark not meaningful).
- [ ] **Step 6 — Write the section, with the framing repeated at the point of maximum temptation.** `benchmarks/RESULTS.md` gains a Phase 7 section that opens with, verbatim: *"The transpiler is not a performance feature. The generated C++ calls the runtime dynamically, so it removes front-end cost and the dispatch loop's `switch`, and it removes neither dynamic dispatch nor the cost of a send. protoScala's positioning is an agile, interoperable, easily integrable and very simple Scala, not a fast one."*

**Done when:** `cat /home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/measurements.md` shows the start-up comparison against 23.73 / 23.89 ms with a verdict line, the load-time table, the seven-benchmark throughput table with a written expectation and an explanation per outlier, and the compile-cost table; and `git diff --stat benchmarks/RESULTS.md` shows the new section.

---

## Task 16: `docs/PROTOSCALAC_SPECIFICATION.md`

**Files:** `docs/PROTOSCALAC_SPECIFICATION.md` (**create**), `README.md`, `docs/INTEROP.md`, `docs/INSTALLATION.md`, `docs/DESIGN.md` (modify — one cross-reference each).

**Interfaces:** none.

- [ ] **Step 1 — Location.** `docs/PROTOSCALAC_SPECIFICATION.md`, mirroring `protoPython/docs/PROTOPYC_SPECIFICATION.md` (§D10). Linked from `README.md`, from `docs/INTEROP.md` §3 (the UMD surface), from `docs/INSTALLATION.md` (what to install to produce a module) and from `docs/DESIGN.md` §3.1 (the pipeline now has a second back end).
- [ ] **Step 2 — Contents, section by section.** Following `PROTOPYC_SPECIFICATION.md`'s order, with three protoScala-specific additions:
  1. **Command line** — the option table of Task 4 Step 1, the file-versus-directory behaviour, the generated `Makefile`'s flags (`-O2 -fPIC -std=c++20`, `-lprotoScala -lprotoCore`, `module.so`, `-Wl,-rpath`), the build-tree versus installation resolution and the three `PROTOSCALAC_*` overrides, the whitespace refusal, the error format `file:line:column: message`, and the exit-status list.
  2. **Loading generated modules** — `proto_module_init`, `CompiledModuleProvider`'s alias `compiled` and GUID `protoScala-compiled-v1`, the `PROTOSCALA_MODULE_PATH` search order, the `dlopen(RTLD_NOW | RTLD_GLOBAL)` contract, the rename-to-`<module>.so` rule, and the `proto_module_main` refusal (D8/D91).
  3. **Module identity and version** *(new)* — P3's `providerGUID \x1F logicalPath \x1F version` key, the **empty string** as the permanent unversioned value and why `"0.0.0"`, `"unversioned"` and `"latest"` are all unsafe reservations, `--module-version`, the absence of a manifest as deliberate, and D106's consequence that one `.so` under two providers is two modules.
  4. **Debugging** — `#line` directives point at the `.scala` file, so g++, GDB and LLDB report Scala source lines; `--emit-cpp` is the inspection path and `--disassemble` is not available for a compiled module.
  5. **Generated code** — values are `const proto::ProtoObject*`; the frame's slot layout and the reserved `pendingSlot()`; **the P1 rule that no generated C++ local holds a value**; the operand stack as traced automatic locals; `JUMP_BACK`'s `safepoint()` obligation; symbols from `createSymbol`; the retry loop and why the `switch` is inside the `try`; the two `translateForeignException` sites and the six clauses in order, including `catch (const std::logic_error&) { throw; }` before the `std::exception` arm and the sentence *"without it, D74 retires silently"*.
  6. **The differential harness** *(new)* — 795 of 841 fixtures, the three anti-rot guards, and the twelve-row mutation matrix from Task 12 Step 4.
  7. **What a transpiled module needs, and why** *(new)* — §D2's four eligibility conditions, `--report-purity`, and the sizing-rule consequence with a pointer to `protoCore/docs/MemoryModel.md`.
  8. **Not implemented** — this section is **mandatory**, and the reason is recorded in it: `PROTOPYC_SPECIFICATION.md` §5 exists precisely to retract an earlier specification that described features `protopyc` did not have. It lists, from §D5: `await` in transpiled code (D103); the REPL (D105); a `--pure` emission mode (§D2); static type inference beyond nothing — the transpiler performs **no** type inference, because the bytecode it consumes has none; a `py::`-style C++ abstraction layer; and C++ namespaces mirroring the module hierarchy.
- [ ] **Step 3 — Two things the specification must say that no other document will.** First: *"the transpiler consumes bytecode, so its coverage is decided per opcode and the list in §8 is exhaustive rather than indicative."* Second: *"there is one implementation of every opcode, in `src/runtime/OpcodeOps.h`, called by both the interpreter and generated code. A second implementation inside the emitter is a defect regardless of whether it is correct."*

**Done when:** `wc -l docs/PROTOSCALAC_SPECIFICATION.md` shows a file with all eight sections present (`grep -c '^## ' ` prints 8), and `grep -rn 'PROTOSCALAC_SPECIFICATION' README.md docs/INTEROP.md docs/INSTALLATION.md docs/DESIGN.md` shows one reference in each.

---

## Task 17: Documentation, deviations, tutorial, release

**Files:** `docs/STATUS.md`, `docs/ROADMAP.md`, `docs/LANGUAGE.md`, `docs/INTEROP.md`, `docs/DESIGN.md`, `docs/TUTORIAL.md`, `docs/tutorial/17-*.md` (**create**), `tests/conformance/tutorial/17-*.scala` (**create**), `CHANGELOG.md`, `README.md`, `docs/DECISIONS-LOG.md`, `CMakeLists.txt` (version).

**Interfaces:** none.

- [ ] **Step 1 — ROADMAP.** Add **Phase 7 — The C++ transpiler ✅ (0.7.0)** with the Goal and a **Done when** that is this plan's acceptance set: `protoscalac` emits C++ that builds to a `.so`; `CompiledModuleProvider` loads it; the differential harness runs 795 fixtures green; the twelve mutations each red a non-empty set; the cross-runtime-call test in `tests/umd/foreign-caller.cpp` passes while naming no protoScala symbol; start-up shows no regression against 23.73 / 23.89 ms; and `PROTOSCALAC_SPECIFICATION.md` exists. Add, in the same voice the other phases use, **"What it deliberately did not do"**: `await` in transpiled code, the REPL, `--pure` emission, and a cross-*space* call.
- [ ] **Step 2 — STATUS.md deviations, starting at the floor Task 1 Step 3 recorded.**
  - **D103** — `await` is refused inside a transpiled module. Cooperative suspension snapshots a bytecode frame (`__mod__`, `__ip__`, `__fbase__`, `__fslots__`) and a transpiled frame has no `ip`; `nativeReentryDepth()` already refuses to suspend above depth 1 (D43). The detection is by send name, so a user method named `await` is refused too — the safe direction.
  - **D104** — a transpiled program's `StackOverflowError` fires at a different recursion depth than the interpreter's, because the native frame size differs. The class and the message are unchanged; only the depth is.
  - **D105** — `protoscalac` compiles files, not REPL input; there is no transpiled REPL and no `res0` echo.
  - **D106** — one `.so` loaded through two runtimes' compiled providers is **two modules** in one process, because P3's identity is provider **GUID** + path + version. Two module objects, two top-level runs.
  - **D107** — a transpiled module's version comes from `protoscalac --module-version`, not from the source: protoScala has no module manifest and this phase did not invent one. Without the option the version is the empty string, which P3 fixed as the permanent "declares no version" value.
  Add to STATUS's open items: the exclusion list's size (46) with its three reason groups, and the fact that a cross-**space** call remains undemonstrated (R5).
- [ ] **Step 3 — Tutorial chapter 17, dual audience, one fixture per runnable snippet.** `docs/TUTORIAL.md`'s chapter table is the live list, so add the chapter there first and take its number from it. For the **Scala** audience: a module is a file, `protoscalac` turns it into a `.so`, and the `.so` is the same artefact a C++ or Python author produces — so a Scala library can be consumed by a Python program without either side knowing. For the **Python/JavaScript** audience: this is the shape of a native extension (`.so`, `dlopen`, one init symbol) with one difference worth naming — there is no C API to learn, because the values are protoCore cells on both sides and cross the boundary without copying. Extend chapter 2 (the bridge) with the three-languages-one-artefact point and chapter 3 (departures) with D103–D107. **Every runnable snippet gets a fixture in `tests/conformance/tutorial/`**, and each fixture also runs in the differential harness, so the chapter's code is checked twice.
- [ ] **Step 4 — LANGUAGE.md, INTEROP.md, DESIGN.md.** LANGUAGE: a short "compiled modules" section keyed to D103–D107. INTEROP: §3 gains the `compiled` provider and its GUID; §7 gains the note that a transpiled function is a `proto::ProtoMethod` and is therefore callable by any runtime sharing the space, with the cross-space limit stated. DESIGN §3.1: the pipeline diagram gains a second back end from `BytecodeModule`, and §3.6 gains the sentence that the interpreter and the generated code share `OpcodeOps.h`.
- [ ] **Step 5 — Version, CHANGELOG, README.** `project(protoScala VERSION 0.7.0 ...)`; `PROTOSCALA_ABI_SOVERSION` is introduced at `1`. CHANGELOG: an `0.7.0` entry naming `protoscalac`, `libprotoScala.so`, `CompiledModuleProvider`, the differential harness, and the four framing points in one line each. README: a "producing a UMD module" paragraph stating that C++, Python and Scala all yield the same artefact and that the producing language is an implementation detail of the module.
- [ ] **Step 6 — Final gate.** From clean:
  ```bash
  cd /home/gamarino/Documentos/proyectos/protoScala
  rm -rf build_release && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release
  cmake --build build_release -j4
  ctest --test-dir build_release --output-on-failure < /dev/null 2>&1 | tail -3
  PROTOSCALA_ACTOR_WORKERS=1  ctest --test-dir build_release < /dev/null 2>&1 | tail -2
  PROTOSCALA_ACTOR_WORKERS=16 ctest --test-dir build_release < /dev/null 2>&1 | tail -2
  PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release -E 'actors-stress' < /dev/null 2>&1 | tail -2
  ```
  All four configurations green, as every phase since Phase 3 has required. Commit by path, on `feature/transpiler-phase7`, never pushed.

**Done when:**
```bash
cd /home/gamarino/Documentos/proyectos/protoScala && \
./build_release/protoscala --version && \
ctest --test-dir build_release --output-on-failure < /dev/null 2>&1 | tail -3 && \
grep -c 'D10[3-7]' docs/STATUS.md
```
prints `0.7.0`, a green total of **the Step 2 baseline + 795 + 12 + the new unit and CLI cases** with 0 failures, and at least `5`.

---

## Appendix A — The four framing points, in one place

Repeated here because a reader who skips to a task must still meet them, and because each is a way to get this phase wrong.

1. **Not a performance feature.** The generated C++ calls the runtime dynamically. It removes the front end and the dispatch `switch`; it removes neither dynamic dispatch nor the cost of a send, and arithmetic is not faster. protoScala is an agile, interoperable, easily integrable and very simple Scala — not a fast one.
2. **Cold start is already solved.** Phase 6's prelude image took the 82 % that parse + desugar + compile represent, and the budget is met at 23.73 ms. The remaining headroom is the **4.0 %** that running the compiled prelude costs. Task 15's criterion is no regression.
3. **What it unlocks is cross-runtime calls.** Track Y proved values cross without copying and proved a foreign runtime cannot *call* a bytecode method. A transpiled function is a `proto::ProtoMethod`, so it can be called by any runtime sharing the space. Task 10 Step 6 demonstrates it from a caller that names no protoScala symbol. The cross-*space* case is not demonstrated and is R5's.
4. **It does not gate libtorch.** A hand-written C++ UMD module wrapping an external library needs only Phase 6's `dlopen` provider loading. Task 10's provider loads such a module unchanged, and nothing else in this phase is a prerequisite for it.

## Appendix B — Open questions left for the maintainer

| # | Question | Where |
|---|---|---|
| 1 | Consume bytecode (recommended) or AST? | §D1 |
| 2 | Accept that protoCore-pure emission is not offered, and that `--report-purity` replaces it? | §D2 |
| 3 | Publish `libprotoScala.so` with `SOVERSION 1` and one installed header? | §D3 |
| 4 | Install `protoscalac`? | §D3, closing note |
| 5 | Module version from `--module-version`, defaulting to `""`? | §D4 |
| 6 | Refuse `await` in the first cut? | §D5, §D9 |
| 7 | Boundary catch template at two `gen::` sites, never in emitter output? | §D6 |
| 8 | `protoscalac` owns a `Session`, so imports behave as D90 says? | §D7 |
| 9 | A transpiled script exports `proto_module_main` and the provider refuses it? | §D8 |
| 10 | Specification at `docs/PROTOSCALAC_SPECIFICATION.md`? | §D10 |
| 11 | Is the 46-fixture exclusion list acceptable, and its three reason groups? | Task 11 Step 4 |
| 12 | Is the `ops::` extraction's 3 %-cycles back-out rule the right gate? | Task 3 Step 5 |
