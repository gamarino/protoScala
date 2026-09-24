# protoScala Phase 3 — Fast Paths, Collections, Prelude and String Interpolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a usable standard library (ROADMAP "Phase 3"): SmallInteger fast paths whose `LargeInteger` promotion is pinned by boundary fixtures at ±2^53; the full `List` surface; `Vector`, `Range`, `Either`, an extended `Try` and an extended `Option`; `Map` and `Set` built on protoCore's `ProtoMap` (protoCore 2.1.0) through the hashed-collection helper; string interpolation (`s"…"`, `f"…"`, `raw"…"`) through lexer, parser, desugarer, compiler and runtime; benchmark suite v1 (`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`) self-reporting and recorded in `benchmarks/RESULTS.md`; tutorial chapters 8 (Collections) and 10 (Strings and interpolation) with a conformance fixture per runnable snippet. Release **0.4.0** (Phase 5 took 0.3.0 out of order; ROADMAP's "each completed phase bumps the minor version" therefore gives Phase 3 → 0.4.0 and Phase 4 → 0.5.0).

**Architecture:** Nothing in the pipeline changes shape. The frontend gains one thing: `InterpString` stops carrying raw hole *text* and starts carrying parsed sub-expressions, produced by a nested `Parser` run over each hole's source (`Parser::parseInterpolation`). `Desugar` lowers `s"…"` and `raw"…"` to a new `CONCAT` opcode over already-evaluated pieces, lowers `f"…"` to a call of the native global `__fmt(format, args…)`, and rejects any other interpolator until extension methods arrive (Phase 4). The runtime gains four new prototypes pinned in root-context slots — `vectorProto`, `rangeProto`, `mapProto`, `setProto` — each an ordinary protoCore object holding its payload in one attribute (`__vec__`: a `ProtoList`; `__start__`/`__end__`/`__step__`/`__inclusive__`: `SmallInteger`s; `__map__`: a `ProtoMap`). `Map` and `Set` never touch `ProtoMap::setAt` directly: every read and write goes through protoCore's `proto::hashedPut` / `hashedGet` / `hashedRemove` / `hashedForEach` with one process-wide `proto::KeySemantics` whose three callbacks are protoScala's own `scalaHash` and `valuesEqual` (`src/runtime/Values.h`), so Scala's cooperative numeric equality (`1 == 1L == 1.0`) decides membership. `Either`, the extended `Try` and the extended `Option` are written in protoScala in `lib/prelude.scala`. No protoCore change is needed and none is made.

**Tech Stack:** C++20, CMake ≥ 3.20, protoCore ≥ 2.1.0 (`find_package(protoCore 2.1 CONFIG)`, sibling-tree fallback `../protoCore/build_release`), GoogleTest 1.14, libreadline; Python 3 for `benchmarks/run_benchmarks.py`; Scala 3.9.0 at `/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0` (`bin/scalac` plus `java -cp "$SCALA_HOME/lib/*:out"`, **never** `bin/scala`) as the reference for expected outputs; OpenJDK 21 for that reference only.

**Spec:** `docs/DESIGN.md` §1 (targets), §1.1 (P1–P6), §3.4 (desugaring table), §3.5 (opcodes), §3.6 (SmallInteger fast paths), §4.1 (values), §4.6 (tuples are case classes, never `ProtoTuple`), §5.1 (universal `apply`), §6 (collections), §6.1 (`Map`/`Set` on `ProtoMap`), §10 (testing), §11 (R2, R8); `docs/platform/PROTOMAP-SPEC.md` §2.1, §4; `docs/ROADMAP.md` "Phase 3" and the Documentation track; `docs/LANGUAGE.md` §2 and §4.

---

## Global Constraints

Every Phase 1, Phase 2 and Phase 5 constraint still holds (`plans/2026-09-22-phase-1-core-language.md`, `plans/2026-09-22-phase-2-object-model.md` and `plans/2026-09-23-phase-5-actors.md`, "Global Constraints"); repeated here with the Phase 3 additions.

**Workspace safety.**

- **Nothing is written outside `/home/gamarino/Documentos/proyectos`.** No `/tmp`, no `$HOME`, no `cmake --install` outside the workspace. Tests create temporaries in the CTest working directory; ad-hoc scratch goes to `../.agent_scratch/phase3/`.
- The canonical build directory is `build_release/`. After any protoCore change, **delete it first** (`rm -rf build_release`, path checked): a stale binary against a new protoCore ABI crashes on the first `ProtoSpace`.
- Commits use the repository's configured git identity (never `-c user.*`, never `--author`) and end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`. Never force-push, never rewrite published history, never create the `v0.4.0` tag — the maintainer tags.
- All code, comments, messages and documentation in professional English. User-facing messages never mention internal phase names.

**Binding principles (DESIGN §1.1 P1–P6).**

- **P1 — values live in protoCore structures.** Every `Map`, `Set`, `Vector` and `Range` payload is an attribute of a protoCore object; no `std::vector<const ProtoObject*>` survives an allocation. The one place this bites in this phase is sorting (Task 8): `sorted`/`sortBy`/`sortWith` must copy the elements into an automatic-local slot array of a dedicated `ProtoContext`, sort the *indices* there, and rebuild the list — never into a `std::vector<const ProtoObject*>` that outlives an allocating comparator.
- **P2 — one `ProtoContext` per invocation**, chained through `previous`. Every native that re-enters the VM (a comparator, a `map` function, a `KeySemantics` callback) opens its own child context.
- **P3 — a missing capability extends protoCore as a new type.** Phase 3 needs nothing new: `ProtoMap` and the hashed-collection helper shipped in protoCore 2.0.0/2.1.0. **If an implementer concludes a protoCore change is required, that is a maintainer decision** — write it into Task 0 as a new `A0-n` with options and a recommendation, and stop; never patch protoCore from this plan.
- **P4 — every deviation from Scala 3 is documented** with a stable id in `docs/STATUS.md`. Phase 3 uses **D54–D70**, of which **D57, D60 and D64 end up unused** (A0-5's `Char` divergence, A0-8's `Range`-equality divergence and A0-12's `Try` divergence were each removed by a ruling or by by-name landing; Task 14 Step 2 records why rather than renumbering). The highest id in use is **D53**, claimed by the by-name-parameters work that landed on `main` on 2026-09-23 (commits `ce172f9`, `ae6ab5b`, `bca0352`). **Re-check anyway before writing a single id:** run `grep -oE 'D[0-9]+' docs/STATUS.md | tr -d D | sort -n | tail -1`; if the highest id in use is not D53, shift this whole block up so it starts one past it, keeping the relative order, and record the shift in `docs/DECISIONS-LOG.md`.
- **P5 — purity over performance.** A performance fix that fragments protoCore's conceptual model is rejected. Any claimed speed-up is backed by `perf stat -r 3` cycles on a quiet host, never by taste.
- **P6 — no mutable-heap GC premises.** No new stop-the-world work, no write barrier, no card marking. Blocking waits (none are added in this phase) would run inside `ProtoContext::UnmanagedScope`.

**`ProtoTuple` prohibition (DESIGN §4.6, R2).** **Never map transient data to `ProtoTuple`.** protoCore interns every `ProtoTuple` node and interned tuples are perennial (`core/ProtoTuple.cpp:214`, `core/ProtoSpace.cpp:449`), so each transient `(a, b)` would stay alive for the life of the process. Scala tuples are **case classes** `Tuple2`..`Tuple22` (Phase 2). In this phase that rule applies to: the `k -> v` pairs `Map.toList` yields (case-class `Tuple2` instances), the `(hash, entry)` slots inside a `ProtoMap` (a flat `ProtoList` `[k0, v0, k1, v1, …]`, which is what protoCore's helper already builds — PROTOMAP-SPEC §4), the pairs `zip` and `zipWithIndex` produce, the `(matching, rest)` result of `partition`, and the `(front, back)` result of `splitAt`. Argument packs, varargs and captures stay `ProtoList`s.

**`PROTO_NONE`.** `PROTO_NONE` is Scala `null` *and* "attribute missing". Presence is probed with `hasOwnAttribute`/`hasAttribute`, or by the `nullptr` a raw accessor returns, never by comparing a lookup result with `PROTO_NONE` alone. `ProtoMap::getAt` and `proto::hashedGet` return `nullptr` for an absent key precisely so a stored `PROTO_NONE` stays distinguishable — Task 9 depends on that.

**Interned symbols.** Symbols are per `ProtoSpace`. **Never cache them in function-local `static`s.** Every new attribute key and method name this phase needs goes into `RuntimeLayout` (filled in `Runtime::Runtime`) and is read from there on the hot paths. protoST measured 51 % of CPU inside `SymbolTable::intern` when two symbols per dispatch were interned instead of cached; `ExecutionEngine.cpp`'s `NEG` and `NOT` cases still do exactly that today and Task 1 Step 3 fixes them.

**Tutorial (the Documentation track, every phase).** Phase 3 writes `docs/tutorial/08-collections.md` and `docs/tutorial/10-strings-and-interpolation.md`, and extends `docs/tutorial/02-for-the-python-or-javascript-developer.md` (the Python/JavaScript bridge: `dict`/`Map`, `set`/`Set`, `list`/`List` vs `Vector`, f-strings vs `s"…"`, `range()` vs `Range`) and `docs/tutorial/03-for-the-scala-developer.md` (the departures catalogue, keyed to the new D-ids). The tutorial is **dual-audience**: traditional Scala programmers get an honest catalogue of departures; developers coming from Python or JavaScript get Scala from first principles with a bridge back to what they know. **Every runnable snippet in a chapter is, verbatim, the body of a conformance fixture** `tests/conformance/tutorial/NN-<topic>-<name>.scala` whose `// EXPECT:` line is the output printed under the snippet, so the text cannot drift from the implementation.

**Conformance fixtures.** `tests/conformance/NN-topic/*.scala`; the **first line** is the directive and nothing else is parsed:
`// EXPECT: <last non-empty stdout line>`, `// EXPECT-ERROR[: <substring of stderr or stdout>]`, `// XFAIL: …`, `// XFAIL-ERROR[: …]`. One CTest case per file (`tests/CMakeLists.txt` globs `conformance/*.scala`); files whose name starts with `_` are helpers and are not run on their own. **Every fixture whose syntax differs between braces and indentation exists in both variants** (`-braces` / `-indent` suffixes). Where a behaviour should match scalac, the step says to verify it against `/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0`:

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase3/out ../.agent_scratch/phase3/probe.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase3/out" probe
```

Never `bin/scala`: it is a script runner whose top-level wrapping differs from a compiled `@main`, which has produced false divergences before.

**Benchmarks self-report and the runner verifies.** Every benchmark prints the work it computed on its last line and `benchmarks/run_benchmarks.py` compares it with the `// EXPECT:` directive of the same file **before** computing any rate; a wrong result, a non-zero exit or a timeout marks the cell FAILED and a FAILED cell never enters a median or a geometric mean. Exit code alone never counts as success (DESIGN §10; the protoPython sprint-9 lesson).

**Build and test only with:**

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
cmake -B build_release -S . && cmake --build build_release
ctest --test-dir build_release --output-on-failure
```

Every target builds with `-Wall -Wextra -Wpedantic` and **no warnings**. The full suite must also pass under `PROTOCORE_HEAP_LIMIT_CELLS=20000` (unfiltered), which is this project's deterministic GC-pressure sweep.

---

## Preflight (before Task 0)

- [ ] **Step 0: Reconcile with the work in flight on `main`**

A **by-name-parameters implementation landed on `main` on 2026-09-23** (commits `ce172f9`, `ae6ab5b`, `bca0352`), after this plan's first draft and outside every phase's scope — ROADMAP records it under Phase 4 as "Landed early (2026-09-23), with the D47 ruling". It takes opcode **38** (`FORCE_THUNK`, `src/compiler/Opcodes.h:59`), adds `builtinByNameSignatures()` (`src/runtime/Primitives.h:41-45`) and `GlobalTable::byNameMaskOf` / `byNameSelectorMask` (`src/compiler/GlobalTable.h:41-100`), and claims **D53** — a by-name parameter is honoured only where the compiler resolves the call site to the declaration. The same run also **overturned D45** (an actor handler may now return a bare `newState`, commit `ce172f9`), which every actor fixture this phase touches must be read against.

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
git log --oneline | head -3
git status --short
grep -n "FORCE_THUNK\|byName" src/compiler/Opcodes.h src/runtime/Primitives.h | head
grep -oE 'D[0-9]+' docs/STATUS.md | tr -d D | sort -n | tail -1
```

Record three things before Task 0 and carry them through the phase:

1. **The lowest free opcode in the `38..63` band** — 39 at the time of writing, which is what Task 3 assumes for `CONCAT`. Confirm it rather than trusting the number.
2. **The highest deviation id in use** — D53 at the time of writing. If it is not D53, shift Phase 3's D54–D70 block up so it starts one past it.
3. **That by-name parameters are still in the tree** (`grep -c 'FORCE_THUNK' src/compiler/Opcodes.h` returns 1). They are, as of `bca0352`, so **A0-12 resolves to the by-name form**: `Try` declares its `apply` argument by-name, `Try { risky() }` works, and **no deviation id is recorded for it** — Task 14 Step 2 leaves D64 unused and says why. Only if the work were reverted would A0-12's function-only form and D64 apply.

If `git status --short` is not clean, **stop and ask the maintainer** whether to branch from the committed state or wait for the merge.

- [ ] **Step 1: Rebuild from clean and confirm the baseline is green**

```bash
cd /home/gamarino/Documentos/proyectos/protoScala
git status --short && git log --oneline | head -3
rm -rf build_release
cmake -B build_release -S . && cmake --build build_release 2>&1 | tail -20
ctest --test-dir build_release 2>&1 | tail -5
```

Expected: a clean `git status`; `100% tests passed`, 694 tests on 2026-09-23 (274 unit, 388 conformance, 22 CLI, 10 benchmark smoke). If the tree is not clean, **stop and ask the maintainer**. Record the test count — Task 14 quotes the before/after numbers.

- [ ] **Step 2: Confirm protoCore carries `ProtoMap` and the hashed-collection helper**

```bash
grep -n "VERSION" /home/gamarino/Documentos/proyectos/protoCore/CMakeLists.txt | head -2
grep -c "newMap\|hashedPut\|hashedGet\|hashedRemove\|hashedForEach\|KeySemantics" \
     /home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h
git -C ../protoCore log --oneline | head -1
```

Expected: `VERSION 2.1.0`; a count of at least 6; one commit line (paste it into the Task 14 CHANGELOG entry: "built against protoCore `<hash>`"). If `hashedPut` is missing, **stop**: Task 9 assumes protoCore's helper and its absence is a Task 0 `A0-6` re-decision, not something to work around.

- [ ] **Step 3: Confirm the Phase 3 surface is still absent (the failing starting point)**

```bash
mkdir -p ../.agent_scratch/phase3
printf 'val m = Map(1 -> "a")\nprintln(m(1))\n' > ../.agent_scratch/phase3/probe-map.scala
build_release/protoscala ../.agent_scratch/phase3/probe-map.scala; echo "rc=$?"
printf 'val x = 2\nprintln(s"x is $x")\n' > ../.agent_scratch/phase3/probe-interp.scala
build_release/protoscala ../.agent_scratch/phase3/probe-interp.scala; echo "rc=$?"
printf 'println((0 until 5).toList)\n' > ../.agent_scratch/phase3/probe-range.scala
build_release/protoscala ../.agent_scratch/phase3/probe-range.scala; echo "rc=$?"
```

Expected: `probe-map.scala:1:9: error: Not found: Map`, rc=1; `probe-interp.scala:2:11: error: string interpolation is not implemented yet`, rc=1; a `NoSuchMethodError` naming `until`, rc=1.

- [ ] **Step 4: Confirm the scalac reference works**

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
mkdir -p ../.agent_scratch/phase3/out
printf '@main def probe(): Unit =\n  val m = Map(1 -> "a", 2 -> "b")\n  println(m.toList.sortBy(_._1))\n' \
  > ../.agent_scratch/phase3/probe.scala
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase3/out ../.agent_scratch/phase3/probe.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase3/out" probe
```

Expected: `List((1,a), (2,b))`. If `scalac` fails, every "verify against scalac" step becomes "record that the reference was unavailable and why" — it never fabricates the expected output.

- [ ] **Step 5: Record the benchmark baseline before any engine change**

```bash
python3 benchmarks/run_benchmarks.py --name phase3-baseline --runs 5 --warmup 2
```

Expected: a report in `benchmarks/reports/<date>-phase3-baseline.md` with no FAILED cell. Task 1 changes the dispatch loop; this is the only honest reference for "no regression". Run it on a quiet host (`uptime` load average below 2.0) or record the load with the numbers and say the figure is not claimed.

---

## Task 0: Maintainer decisions

Each decision below is **decided by the agent, pending review** under the maintainer's standing authorisation, and is recorded in `docs/DECISIONS-LOG.md` by Task 14 with that marker. Where a decision creates a user-visible departure from Scala 3, Task 14 records it as a `D<n>` in `docs/STATUS.md` and `docs/LANGUAGE.md` §5. **Phase 3 uses D54–D70**, subject to the re-check in the Global Constraints (D53 is the highest id in use, claimed by the by-name-parameters work that landed on 2026-09-23).

### A0-1 — How `s"…"` and `raw"…"` are lowered

DESIGN §3.4 gives the row `s"a $x b"` → `StringContext("a ", " b").s(x)` "compiled to a fast concat primitive". Those are two different lowerings and the phase must pick one.

| Option | What it costs | What it buys |
|---|---|---|
| **(a) A `CONCAT n` opcode** (recommended) | One opcode in the free `39..63` band, ~25 lines in `runLoop`, ~30 lines in `Desugar`/`Compiler`. `s` and `raw` no longer honour a user-defined `StringContext` | One rope built in one pass with `appendLast`; no `StringContext` instance, no parts `ProtoList`, no argument `ProtoList` per interpolation. An `s"…"` inside a loop allocates only the result |
| (b) Desugar to `StringContext(parts…).s(args…)` exactly as scalac does | Spec-faithful; a user `StringContext` extension works the day extension methods land (Phase 4) | Three allocations plus two sends per interpolation, in the single most common string idiom |
| (c) (a) for `s`/`raw`/`f`, and (b) for any *other* interpolator name | Both code paths | Custom interpolators for free — except that defining one needs `extension (sc: StringContext)`, which is Phase 4 |

**Decision: (a)**, confirmed by the maintainer (2026-09-23), with the unknown-interpolator case rejected at compile time (`A0-3`). `CONCAT n` pops `n` operand-stack values, converts each with `toScalaString` (which returns a `ProtoString` unchanged and calls a user `toString` through the active engine otherwise) and joins them with `ProtoString::appendLast`, which is an O(log n) rope join that copies neither side. Recorded as **D54**: `s"…"` and `raw"…"` are compiled directly and a user-defined `StringContext` is ignored. Reversing to (c) later is additive — the compiler simply stops taking the fast path when the interpolator is unknown.

### A0-2 — How `f"…"` is lowered

Scala's `f` interpolator reads each hole's format specifier from the literal text that immediately follows it (`f"$x%05.2f"`) and defaults to `%s`.

| Option | Consequence |
|---|---|
| **(a) The desugarer builds one printf-style format string and emits `__fmt(fmt, a1, …, an)`** (recommended) | The format string is a compile-time constant in the pool; the native does one pass; a malformed specifier is a **compile** error with the right source position, as scalac gives |
| (b) A `FORMAT` opcode | A second opcode for a cold path |
| (c) Format at run time from the parts list | The specifier's source position is lost, so the diagnostic points at the whole string |

**Decision: (a)**. `__fmt` is a native global (like `__raise`). Supported conversions, decided here and recorded as **D55**: `%s %b %c %d %o %x %X %e %E %f %g %G %%`, with the flags `-`, `+`, ` `, `0`, `,` (grouping, ASCII `,` only) and `width.precision`. `%n` is **not** supported (write `\n`); no locale support; `%d` on a `LargeInteger` prints the full precision (D1). Anything else is a compile error `unsupported format specifier '%…' in an f-interpolator`.

### A0-3 — An interpolator that is neither `s`, `f` nor `raw`

**Decision:** a compile error at the interpolation's source position: `unknown string interpolator 'json': only s, f and raw are available (custom interpolators need extension methods)`. Recorded as **D56**. Phase 4's extension-method task closes it by making `extension (sc: StringContext) def json(args: Any*)` reachable; until then, silently accepting one would mean building a `StringContext` the runtime cannot dispatch on.

### A0-4 — Where `Map`, `Set`, `Vector` and `Range` live

`List` is a raw `ProtoList` with Scala methods installed on protoCore's list prototype (DESIGN §6), so a raw list *is* a Scala `List`. The four new collections cannot follow that pattern: `ProtoMap` has no prototype protoScala owns, and `Vector` must stay distinguishable from `List`.

| Option | Consequence |
|---|---|
| **(a) Runtime prototypes pinned in root slots, payload in one attribute** (recommended) | Exactly what DESIGN §6 prescribes (`Vector`: "an object holding a `ProtoList` in one attribute"); `TEST_PROTO` membership works through the ordinary `@Vector`/`@Map`/`@Set`/`@Range` marker keys of `builtinTypes()`; `Map`/`Set`/`Vector`/`Range` become writable in `builtinGlobalNames()` alongside `List`/`Nil` |
| (b) Prelude classes written in protoScala | `Map` would claim the type key `@Map` that `builtinTypes()` needs, and every operation would cross the VM boundary twice (the Phase 5 `Actor` lesson: a prelude `object Actor` clashes with the builtin type key) |

**Decision: (a)**. Attribute keys: `__vec__` (a `ProtoList`), `__map__` (a `ProtoMap` object word), `__start__`/`__end__`/`__step__`/`__inclusive__` (`SmallInteger`s and `PROTO_TRUE`/`PROTO_FALSE`). `__list__` is **not** reused: it is already the WithFilter's source key.

### A0-5 — Which keys are identity keys for `Map`/`Set` — **settled by the maintainer: DESIGN §6.1 stands**

protoCore's `proto::KeySemantics` asks the language three questions: `isIdentityKey`, `hash`, `equals`. An earlier draft of this plan proposed answering `isIdentityKey` with a constant `false`, on the grounds that misclassifying a key is *silently* wrong. **The maintainer overturned that: the specification is the authority, and the classification is implemented as DESIGN §6.1 defines it.** The concern was accepted as legitimate, so the classification is made **explicit and testable** rather than implicit and hopeful: Step 2 below decides it by an exact pointer comparison, not a heuristic, and Step 1's fixtures are written so that a misclassified key fails a test **loudly** — as a missing entry, a wrong `size`, or a lookup that prints a miss — never quietly.

Restoring §6.1 surfaced two problems inside the section itself, which went to the maintainer as concrete counterexamples rather than as policy. **Both were ruled on 2026-09-23, both toward Scala, and `docs/DESIGN.md` §6.1 was edited accordingly** (Task 14 Step 5 records the edit; the quote below is the edited text, re-synchronised):

- **C1 — `Char` follows Scala, and is special-cased in the code.** The ruling, in the maintainer's words: *"si `Char` es un caso especial, debería procesarse especial en el código de la función. Tratar en lo posible seguir Scala."* A `Char`'s `==` is **not** `eq` — Scala's cooperative equality makes `'a' == 97` true and `'a'.##` equal to `97` — which is precisely why it cannot sit in bullet 1, whose criterion is "objects whose `==` is `eq`". `Char` moves to the value bucket, `scalaIsIdentityKey` carries an explicit `Char` branch with that reason written at it, and `Map[Any, Int]('a' -> 1, 97 -> 1)` has **one** key, as in Scala. There is no divergence, so **no deviation id is recorded** and `map-char-and-int-keys.scala` is a passing fixture rather than an `XFAIL`.
- **C2 — a parameterised `enum` case is a case class, so it is a value key.** The ruling: *"es el usuario que está haciendo la conversión"* — writing `case Leaf(n: Int)` inside an `enum` *is* writing a case class, so it is classified as one. Only a **singleton** enum case (a `case object`) is an identity key, and §6.1's bullet 1 now says *singleton* explicitly so the ambiguity cannot come back. `Map(Tree.Leaf(1) -> "a")(Tree.Leaf(1))` hits, as in Scala, and `map-parameterised-enum-case-keys.scala` is a passing fixture.

**The standing rule these two rulings establish, which applies beyond §6.1: follow Scala unless there is a *platform* reason not to.** A0-8 below is re-opened under it (see Q8a).

**The rule, quoted verbatim from DESIGN §6.1** (this is the specification text, not a paraphrase; implement exactly this):

> - **Identity-equality keys** — objects whose `==` is `eq` (instances of
>   classes with the default `equals`, `object`s, `case object`s, **singleton**
>   enum cases, symbols, booleans): the key is the object itself. It is traced by
>   the GC and recovered directly; the value slot holds `v`. No buckets, no
>   collisions.
> - **Value-equality keys** — numbers, **chars**, strings, case classes
>   (including tuples and **parameterised enum cases, which are case classes**),
>   collections, and classes that override `equals`: the key is the
>   Scala `hashCode` encoded as a `SmallInteger` word (non-pointer, so the GC
>   ignores it and it can never collide with an identity key). The value slot
>   holds an entry `(k, v)` — a two-element `ProtoList`, *never* a `ProtoTuple`
>   (§4.6) — or, on a genuine hash collision, a `ProtoList` of entries compared
>   with `==`.
> - **`Char` is a value-equality key** (maintainer ruling, 2026-09-23). An
>   earlier version of this section listed it among the identity keys. That was
>   wrong on this section's own criterion: a `Char`'s `==` is **not** `eq`,
>   because Scala's cooperative equality makes `'a' == 97` true and `'a'.##`
>   equal to `97`, so `Map('a' -> 1)` and a lookup by `97` must find one key.
>   `Char` is therefore the one kind the classification function special-cases
>   explicitly, rather than pretending it behaves like the other immediates.
> - **A parameterised `enum` case is a case class, so it is a value-equality
>   key** (maintainer ruling, 2026-09-23). Writing `case Leaf(n: Int)` inside an
>   `enum` *is* writing a case class, so it is classified as one and two
>   separately built `Leaf(1)`s are one key, as in Scala. Only a **singleton**
>   enum case (a `case object`) is an identity key. Before this ruling the two
>   bullets above overlapped and disagreed for a parameterised case.
> - `Set` uses the same scheme with the element as the entry.
> - The language supplies equality and hash (Scala's cooperative numeric
>   equality: `1 == 1L == 1.0`, and `##`). protoCore supplies the traced-key
>   structure and, if the platform spec adopts it, a shared hashed-collection
>   helper parameterised by the language's `hash`/`equals`.

**The classification table protoScala implements from it**, term by term, so no implementer has to interpret the prose:

| Key | Class | Why, from §6.1 |
|---|---|---|
| an instance of a class with the **default** `equals` | **identity** | bullet 1, first clause. "Is this an instance at all?" is `isScalaInstance(ctx, L, v)` (`src/runtime/Values.h`, already used by `any_equals`); "does its class override `equals`?" is settled by comparing the resolved `equals` method against `RuntimeLayout::defaultEqualsMethod` — the one object installed on `anyProto` (`Primitives.cpp:869`, `any_equals`) — an **exact pointer comparison**, not a heuristic |
| an `object`, a `case object` | **identity** | bullet 1. A `case object` has identity equality in protoScala (DESIGN §4.5), so this agrees with `valuesEqual` |
| a **singleton** `enum` case | **identity** | bullet 1 ("**singleton** enum cases"); a singleton case is a `case object` (Phase 4 A0-9) |
| `true`, `false` | **identity** | bullet 1. `PROTO_TRUE`/`PROTO_FALSE` are unique words |
| a `Char` | **value** | bullet 2, by the C1 ruling. It is the one kind `scalaIsIdentityKey` special-cases explicitly: a `Char` is a unique embedded word, so identity *looks* right, but its `==` is not `eq` (`'a' == 97`), so it must hash like the `Int` of its code point. `scalaHash` already hashes a `Char` to its code point, so `'a'` and `97` land in the same slot and `valuesEqual` confirms them equal |
| `null` (`PROTO_NONE`), `()` | **identity** | not named in §6.1; both are unique words and `==` on them is word identity, so bullet 1's *criterion* ("objects whose `==` is `eq`") puts them there |
| a function value, a `Future`, an actor, a `WithFilter` | **identity** | not named in §6.1; `valuesEqual` falls back to identity for them, so bullet 1's criterion applies |
| an `Int`, `Long`, `BigInt`, `Double` | **value** | bullet 2 ("numbers"). Also forced: protoCore's helper puts every `SmallInteger` key on the value path regardless of what `isIdentityKey` answers |
| a `String` | **value** | bullet 2 |
| a case-class instance, a `TupleN`, a **parameterised** `enum` case | **value** | bullet 2 ("case classes (including tuples and parameterised enum cases, which are case classes)"). Their `equals` resolves to `product_equals` (`ProductPrimitives.cpp:277`), which is not `defaultEqualsMethod`, so the pointer comparison classifies all three without a special case — a parameterised enum case needs no rule of its own, which is the C2 ruling's point |
| a `List`, `Vector`, `Map`, `Set`, `Range` | **value** | bullet 2 ("collections") |
| a class that overrides `equals` | **value** | bullet 2. Its `equals` resolves to the user's compiled method, again not `defaultEqualsMethod` |

**Why the two slot kinds cannot collide, which is what makes bullet 1 sound.** A hashed slot key is always a `SmallInteger` word (embedded type 0). An identity slot key is either a cell pointer or a non-`SmallInteger` immediate: `PROTO_TRUE`/`PROTO_FALSE`, `PROTO_NONE`, the `()` singleton, or an object cell. (A `Char` is embedded type 2 and would also have been disjoint, but after the C1 ruling it is a value key and never appears as an identity slot key at all.) The two sets are disjoint by encoding, which is exactly the argument PROTOMAP-SPEC §4 makes ("identity keys and hashed keys can never collide … The language's `isIdentityKey` must return `false` for integers"). Step 8's unit test asserts the disjointness directly rather than trusting it.

**How the two rulings are implemented and pinned.** Neither leaves a deviation behind, so both fixtures are ordinary passing fixtures; Task 9 Step 1 carries them and Step 8 carries the unit tests.

| Ruling | Key and operation | Answer, here and in Scala | Where it is pinned |
|---|---|---|---|
| **C1** | `val m = Map[Any, Int]('a' -> 1, 97 -> 1)`; `m.size`, `m(97)` | `1` and `1` — one key | `map-char-and-int-keys.scala`, and `MapSurface.CharAndIntAreOneKey`; the explicit `Char` branch in `scalaIsIdentityKey` (Step 2) |
| **C2** | `val m = Map(Tree.Leaf(1) -> "a")`; `m(Tree.Leaf(1))` | `"a"` — one key | `map-parameterised-enum-case-keys.scala` (needs Phase 4's `enum`; ships `XFAIL: requires enum (Phase 4)` **only** while Phase 4 has not landed, and its `EXPECT` line is already written) |

**Two cases that look like counterexamples and are not**, recorded so nobody re-raises them:

- A class that overrides `equals` but **not** `hashCode` lands in the value bucket, so its lookups hash by `scalaHash`'s identity fallback and two `==` objects hash differently and miss each other. That is exactly Scala's behaviour — the classic `equals`-without-`hashCode` bug — so it is not a divergence. Step 1 pins it with a fixture anyway, because a reader will otherwise mistake it for one.
- A mutable key whose `equals`-relevant field changes after insertion becomes unfindable. Scala behaves identically.

**Decision:** implement DESIGN §6.1 as quoted — which, after the C1 and C2 rulings, agrees with Scala on every key kind. **No deviation id is recorded for A0-5**: the provisional D57 of the earlier draft ("a `Char` key and a numerically equal `Int` key are distinct") is **withdrawn**, because the ruling removed the divergence it described. Task 14 Step 2 leaves **D57 unused** and says why, as it already does for D64.

### A0-6 — Iteration order of `Map` and `Set`

D7 already says the order is unspecified. `ProtoMap` iterates in ascending key-*word* order, so for hashed keys the order is ascending-hash, which is stable within a run and across runs for the same key set (`scalaHash` is deterministic), but bears no relation to insertion order or to Scala's.

**Decision:** keep D7 as it is and add the concrete consequence as **D58**: `Map.toString`, `Set.toString`, `foreach`, `keys`, `values`, `toList` and `mkString` yield ascending-hash order, which is deterministic for a given key set but differs from Scala's. **Every conformance fixture that prints more than one entry sorts first** (`m.toList.sortBy(_._1)`), so the suite pins behaviour and never pins the order. `SortedMap`/`ListMap` stay out of scope (ROADMAP "Later (v0.7+)").

### A0-7 — `Vector`'s representation and `List == Vector`

DESIGN §6 fixes the representation (an object holding a `ProtoList`) and the equality (`List(1,2) == Vector(1,2)` is `true`, Scala `Seq` equality) and says it is "implemented in the `equals` of both".

**Decision:** implement it **once**, in `valuesEqual` (`src/runtime/Values.cpp`), by unwrapping a `Vector` object to its `__vec__` list before the element-wise comparison. `equals` on either prototype then delegates to `valuesEqual`, and the `EQ`/`NE` opcodes — which call `valuesEqual` directly and never dispatch — agree with `==` written as a method. Two implementations would have produced the classic `a == b` ≠ `a.equals(b)` split. Consequence recorded as **D59**: `Vector.hashCode` equals `List.hashCode` for the same elements (required for `Map(List(1) -> 1)(Vector(1))` to work), and both differ from the JVM's (D39, unchanged).

### A0-8 — `Range`: eager or lazy, and whether `Range == List` follows Scala

DESIGN §6: "an object with `start`/`end`/`step` and lazy iteration".

**Decision on representation, confirmed by the maintainer (2026-09-23):** the object stores only the four fields; `foreach`, `map`, `flatMap`, `filter`, `withFilter`, `forall`, `exists`, `sum`, `count` and `find` iterate without materialising, and only `toList`/`toVector`/`toSet`/`mkString`/`reverse` materialise. `Range.apply(i)` is O(1) arithmetic. `length` is O(1) arithmetic.

**Decision on equality: follow Scala.** `(0 until 3) == List(0, 1, 2)` is **`true`**, as scalac answers, and the earlier draft's `false` is withdrawn. This was escalated and then handed back to be **decided on cost**, under the maintainer's standing rule: *"En la medida que sea razonable, least surprise para el programador. Por otro lado, si alguien escribe usando esos detalles está jugando con fuego y no creo que sea tan común."* — match Scala where matching is cheap; document the divergence where matching would cost real machinery and only unusual code could notice. **Here matching is cheap**, so it is matched. The cost, stated:

| What it takes | Size |
|---|---|
| A `SeqView` struct plus `seqViewOf` / `seqElemAt` in `Values.cpp` — one read-only, **allocation-free** view over a `ProtoList` (a `List`), a `Vector`'s `__vec__` list, and a `Range` read arithmetically | ~40 lines, and it **replaces** the `vectorListOf` helper A0-7 was already going to add for `List == Vector`, so it is a generalisation of machinery this phase introduces anyway, not new machinery |
| `valuesEqual`'s list branch uses the view: compare lengths, then element-wise | ~10 lines, replacing the existing two-line unwrap |
| `scalaHash`'s list branch uses the same view, so `Range` hashes like the `List` of its elements | ~8 lines. Required, not optional: without it `Map(List(0,1,2) -> 1)((0 until 3))` would miss and `Set` would hold duplicates — an `==`/`##` split is exactly the defect A0-7 exists to prevent |
| Cost at run time | **No allocation.** `Range.length` and `Range.at(i)` are O(1) arithmetic; the length check short-circuits, so `(0 until 1000000000) == List(1)` answers `false` in O(1). Two `Range`s compare `start`/`step`/`length` in O(1). The `EQ` opcode's `SmallInteger` fast path (Task 1 Step 2) precedes `valuesEqual` entirely, so ordinary integer comparison in a loop is untouched |
| What was **not** done, and why the earlier draft feared it | Materialising the `Range` on every `==`. That would have been O(n) allocation on a hot opcode and is what made the divergence look justified; the index-wise view avoids it completely |

So **D60 is withdrawn** — there is no divergence left to record. `Range.isInstanceOf[List[Int]]` stays `false`, but so it is in Scala (a `Range` is not a `List`), so that is not a divergence either. Task 6 Step 3a implements the view and Task 6 Step 1 carries the fixtures; Task 10 extends `seqViewOf` with the `Vector` branch instead of adding `vectorListOf`.

### A0-9 — Overflow of a `Range`'s bounds

`0 until n` with a `LargeInteger` bound cannot be represented in the `SmallInteger` fields.

**Decision:** `to`, `until` and `by` require `SmallInteger` arguments and raise `IllegalArgumentException: a Range bound must fit a 54-bit integer` otherwise; recorded as **D61**. A `Range` of more than 2^53 elements is not a workload this dialect serves, and the alternative — boxing every bound — costs an allocation on every `Range` method.

### A0-10 — `sorted` without an `Ordering`

Implicits and givens are out of scope (D3), so `xs.sorted` has no `Ordering` to resolve.

| Option | Consequence |
|---|---|
| **(a) `sorted` uses the runtime's own ordering** (recommended) | Works for `Int`, `Double`, `Char`, `String` and mixed numbers, which is what `sorted` is used for; anything else is a loud error |
| (b) No `sorted`, only `sortWith` | Every program must write a comparator for a list of integers |

**Decision: (a)**: `sorted` compares with `proto::ProtoObject::compare`, which orders numbers by exact value across `SmallInteger`/`LargeInteger`/`Double` and strings by content. A pair the runtime cannot order raises `IllegalArgumentException: sorted needs comparable elements; use sortWith`. Recorded as **D62**. `sortBy(f)` sorts by `compare` on `f`'s results; `sortWith(lt)` takes the comparator. All three are **stable** (merge sort), as Scala's are.

### A0-11 — `collect` and partial functions

`xs.collect { case … }` needs a `PartialFunction` with `isDefinedAt`; a `{ case … }` literal in protoScala is a total function that throws `MatchError` on no match (D34).

**Decision, confirmed by the maintainer (2026-09-23):** `collect` is **not** provided in Phase 3, and `xs.collect(…)` fails with `NoSuchMethodError`. Recorded as **D63**, with the follow-up named: a `PartialFunction` needs the compiler to emit a second entry point per `{ case … }` literal, which belongs with Phase 4's pattern-matched `catch` handlers, not here. Writing `xs.filter(p).map(f)` is the documented replacement.

### A0-12 — `Try.apply` and by-name parameters

`Try { risky() }` needs a by-name parameter; protoScala has none (D33) and cannot infer one without static types (D47 already took this decision for `Future.apply`).

**Decision: use by-name, because it landed.** By-name parameters are in the tree as of `bca0352` (Preflight Step 0), so `Try.apply` declares its single argument by-name, `Try { risky() }` works as in Scala, the function form `Try(() => expr)` stays accepted for compatibility with programs written against D47's `Future.apply`, and **no deviation id is recorded** — Task 14 Step 2 leaves D64 unused and says why. **Only** if the by-name work were reverted does the fallback apply: `Try(() => expr)` recorded as **D64** with the same wording as D47, and the native raising `IllegalArgumentException: Try.apply expects a function: write Try(() => expr)` when handed anything else. Note D53's limit either way: a by-name argument is lazy only where the compiler resolves the call site to the declaration, and `Try.apply` is a method of a prelude `object`, which D53 lists as resolvable.

### A0-13 — `Try` keeps carrying `RuntimeError` in this phase

`Failure(error: RuntimeError)` landed in Phase 5 (D44) because there were no exception values yet. Phase 4 replaces `RuntimeError` with a real `Throwable` hierarchy.

**Decision:** Phase 3 **extends** `Try`/`Success`/`Failure` with combinators (`map`, `flatMap`, `filter`, `withFilter`, `foreach`, `recover`, `recoverWith`, `orElse`, `toEither`, `failed`) and **does not re-point `Failure`'s payload**. D44 stays open and Phase 4 closes it in one prelude edit plus the `await` raise path. This is the only ordering coupling between the two phases and Task 5 Step 1 restates it in the prelude comment.

### A0-14 — `Seq` and `Iterable` as traits

LANGUAGE §4 lists `Seq`/`Iterable` "as traits" in the full target surface; ROADMAP's Phase 3 done-when does not mention them, and Phase 2's linearization installs a *flat* parent chain per class, so making `List`, `Vector`, `Range`, `Map` and `Set` share a `Seq` ancestor means editing five runtime prototypes and `builtinTypes()`.

**Decision, ruled 2026-09-23: not in Phase 3, and ROADMAP's done-when governs.** `case xs: Seq[_]` and `x.isInstanceOf[Seq[_]]` are rejected at compile time with `this type cannot be tested at run time: Seq is not a protoScala type`, exactly as an intersection type is today (D37). Recorded as **D65**. The discrepancy is resolved by **correcting the document that over-promised**: `docs/LANGUAGE.md` §4 currently lists `Seq`/`Iterable` "as traits" in the target surface with no phase attached, and Task 14 Step 3 removes that promise — a document that promises what does not exist is worse than an absent feature.

### A0-15 — Opcodes added in this phase

**Decision:** exactly one, `CONCAT = 39`, in the free `39..63` band that `Opcodes.h` reserves for "Phase 1 additions". **38 is already taken:** a by-name-parameters change in flight on `main` added `FORCE_THUNK = 38` (`src/compiler/Opcodes.h:59`). Re-read `Opcodes.h` before adding the opcode and take the lowest free number in the band, not 39 by assumption. Nothing else needs one: every collection operation is an ordinary `SEND` to a native method, and `perf stat -r 3` is the only argument that would ever justify a second (DESIGN §1: protoScala is not a fast Scala). The `96..127` (exceptions) and `128..159` (actors) bands stay reserved and untouched.

---

## Design notes that apply to several tasks

File:line references are to the trees of 2026-09-23.

1. **How a native method is written and installed.** Every native uses the `PRIM` macro of `src/runtime/PrimitiveSupport.h:17-22`, which expands to protoCore's exact `ProtoMethod` signature. Installation is a `MethodEntry` table plus `installAll` (`src/runtime/Primitives.cpp:839-850`). The helpers `layoutOf()`, `arg(ctx, args, i, name, expected)`, `argCount`, `intArg`, `stringArg`, `asStr`, `boolean`, `str` and `wrongType` come from the same header. A new primitive file follows `ProductPrimitives.cpp` / `ActorPrimitives.cpp`: it includes `runtime/PrimitiveSupport.h`, defines its `PRIM`s in an anonymous namespace and exposes one `install…Primitives(ctx, layout)` that `installPrimitives` calls last (`Primitives.cpp:935-936`).

2. **Re-entering the VM from a native.** `activeCallContext()->engine->invoke(ctx, fn, args, argc)` runs a Scala callable from inside a primitive (`ExecutionEngine.h:30-34`, `:46-49`). `args` must be **rooted** — put them in a child `ProtoContext`'s automatic locals first. This is what `List.map` already does and what every new higher-order method (`foldLeft`, `sortWith`, `groupBy`, a `KeySemantics` callback) must do. A native that re-enters the VM raises `nativeReentryDepth()` above 1, so an `await` under it is refused (D43) — that is correct and expected.

3. **Rooting a compare-and-swap-free accumulator.** Building a list in a loop inside a native (`filter`, `map`, `sorted`) must keep the partial result in a slot, not a C++ local, across the allocation that extends it — the Phase 5 `Mailbox::push` bug. The shape:

```cpp
proto::ProtoContext scope(ctx->space, ctx);
scope.resizeAutomaticLocals(2);
const proto::ProtoObject** slot = scope.getAutomaticLocals();
slot[0] = ctx->newList(0, nullptr)->asObject(&scope);     // the accumulator
for (unsigned long i = 0; i < n; ++i) {
    slot[1] = src->getAt(&scope, static_cast<int>(i));     // the element, rooted
    const proto::ProtoObject* r = engine->invoke(&scope, fn, &slot[1], 1);
    slot[1] = r;
    slot[0] = slot[0]->asList(&scope)->appendLast(&scope, slot[1])->asObject(&scope);
}
return slot[0];
```

4. **`ProtoMap` is never touched directly.** Every `Map`/`Set` read and write goes through `proto::hashedGet` / `hashedPut` / `hashedRemove` / `hashedForEach` with the single `proto::KeySemantics` of Task 9 Step 1. protoCore's header is explicit: "A map used with these functions must be read and modified only through them (with one `KeySemantics`): mixing them with raw `setAt` / `removeAt` on the same map is undefined, because a raw `SmallInteger` key would collide with a hash slot."

5. **`getIterator` returns `nullptr` for an empty map.** protoCore documents it; every iteration site must handle it. `hashedForEach` does not have that hazard, so prefer it.

6. **The classification is a pointer comparison, not a heuristic.** `RuntimeLayout::defaultEqualsMethod` is the single `equals` installed on `anyProto` (`Primitives.cpp:869`). A class that does not override `equals` resolves to exactly that object; a case class resolves to `product_equals` (`ProductPrimitives.cpp:277`); a user override resolves to their own compiled method. `scalaIsIdentityKey` therefore decides DESIGN §6.1's "instances of classes with the default `equals`" versus "classes that override `equals`" exactly, which is what makes the classification testable rather than hopeful (A0-5).

7. **`scalaHash` must not be reimplemented.** `scalaHash(ctx, L, v)` (`src/runtime/Values.h`) is already Scala's `##`: numbers by `Statics` (an Int-range integer hashes to itself, a whole `Double` as the integer, so `1.##` == `1L.##` == `1.0.##`), `Char` by code point, `Boolean` 1231/1237, `String` by `String.hashCode`, `()`/`null` 0, `List` by `seqHash`, an instance by its `hashCode` method. That is exactly what `KeySemantics::hash` needs; the cooperative numeric part is what makes `Map(1 -> "a")(1.0)` return `"a"` as Scala does.

8. **`valuesEqual` must not be reimplemented** either: it is already Scala `==` with cooperative numeric equality, string content, element-wise lists and a user `equals`. `KeySemantics::equals` is a two-line wrapper over it.

9. **The `KeySemantics` callbacks run in a `ProtoContext` protoCore hands them and may allocate** (the header says so), so a user `hashCode` re-entering the VM is legal. They must not throw across protoCore's C ABI in a way that leaves a half-built map — they may throw `ScalaError`, which unwinds through protoCore's C++ frames normally (protoCore's mutators allocate inside a `CriticalSection` but the callbacks are documented to run outside it).

10. **`toScalaString` keeps ropes intact.** `toScalaString(ctx, L, v)` returns `v` itself when it is already a string and `makeString(ctx, show(ctx, L, v))` otherwise (`src/runtime/Values.h`). `CONCAT` uses it, so `s"$a$b"` on two strings joins two ropes with no copy — the protoJS "String.prototype rope-flatten anti-pattern" lesson applied here.

11. **Two real defects in `runLoop` that Task 1 fixes.** `case Op::NEG` and `case Op::NOT` (`ExecutionEngine.cpp:761-779`) call `proto::ProtoString::createSymbol(&frame, "unary_-")` **on every execution**. That is the interning anti-pattern the Global Constraints forbid, and `createSymbol` additionally leaks for strings longer than 6 bytes (R4) — `unary_-` is 7 bytes. And `case Op::EQ`/`Op::NE` (`:755-760`) has no `SmallInteger` fast path at all: the most common comparison in a loop goes through the full `valuesEqual`.

12. **Where the `List` methods live today.** `installPrimitives` (`Primitives.cpp:865-937`) builds `constexpr MethodEntry` arrays per prototype. `lists[]` (`:908-913`) currently holds `length size isEmpty nonEmpty apply head foreach mkString :: tail drop map flatMap filter withFilter`; `listCompanion[]` (`:917-918`) holds `apply empty`; `strings[]` (`:900-907`) holds `length isEmpty nonEmpty charAt apply substring toUpperCase toLowerCase trim contains startsWith endsWith indexOf reverse * + concat toInt toDouble`. Tasks 3, 4 and 7 extend these arrays in place; the arrays are `constexpr`, so an addition is one line each.

13. **A `MethodEntry` array may exceed the `installAll` template's deduction comfortably** — it is `template <std::size_t N> void installAll(ctx, target, const MethodEntry (&entries)[N])`, so any size works.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/compiler/Opcodes.h` (modify) | `CONCAT = 39` (38 is `FORCE_THUNK`, added by the by-name change in flight on `main`), its comment and `opName` |
| `src/frontend/AST.h` (modify) | `InterpString` carries `std::vector<NodePtr> args` and `std::vector<std::string> literals` instead of raw `InterpolationPart`s |
| `src/frontend/Parser.h` / `Parser.cpp` (modify) | `parseInterpolation`: a nested parse of each hole's source text |
| `src/frontend/Desugar.cpp` (modify) | `s`/`raw` → `Concat(pieces)`; `f` → `__fmt(fmt, args…)`; any other interpolator → error |
| `src/compiler/Compiler.h` / `Compiler.cpp` (modify) | `compileInterp`; `CONCAT` emission |
| `src/runtime/ExecutionEngine.cpp` (modify) | `CONCAT`; the `EQ`/`NE` `SmallInteger` fast path; `NEG`/`NOT` read their symbols from `RuntimeLayout` |
| `src/runtime/Runtime.h` / `Runtime.cpp` (modify) | `vectorProto`, `rangeProto`, `mapProto`, `setProto`, their companions, the Phase 3 attribute keys and method-name symbols, and `defaultEqualsMethod` (filled by `installPrimitives`, not by the constructor — the method object does not exist until then) |
| `src/runtime/Values.h` / `Values.cpp` (modify) | `isVectorFast`/`isRangeFast`/`isMapFast`/`isSetFast`, `show`, `typeName`, `scalaHash` and `valuesEqual` for the four new kinds; `List == Vector` |
| `src/runtime/Primitives.h` / `Primitives.cpp` (modify) | The extended `List`, `String` and `Int` tables; `installCollectionPrimitives`; `__fmt`; the new builtin globals |
| `src/runtime/CollectionPrimitives.cpp` (new) | `Vector`, `Range`, `Map`, `Set`: every native, plus the `KeySemantics` and the sort |
| `src/support/FormatSpec.h` / `FormatSpec.cpp` (new) | `FormatSpec` and `parseFormatSpec`: the `f`-interpolator specifier parser, free of protoCore so the compiler may include it (Task 4 Step 4) |
| `src/runtime/Format.h` / `Format.cpp` (new) | `formatOne`: the runtime formatter, which needs `RuntimeLayout` and protoCore |
| `src/compiler/ClassInfo.cpp` (modify) | `builtinTypes()` gains `Vector`, `Range`, `Map`, `Set`, `StringContext` |
| `lib/prelude.scala` (modify) | `Either`/`Left`/`Right`; the extended `Option`; the extended `Try`; `__mkLeft`/`__mkRight` constructor hooks |
| `tests/unit/test_values.cpp`, `test_primitives.cpp`, `test_lexer.cpp`, `test_parser.cpp`, `test_desugar.cpp` (modify); `tests/unit/test_collections.cpp`, `test_format.cpp` (new); `tests/unit/CMakeLists.txt` (modify) | Unit tests |
| `tests/conformance/15-integers/`, `16-strings/`, `17-collections/`, `18-maps-and-sets/`, `19-either-try/` (new) | Fixtures |
| `tests/cli/gc-pressure.sh` (modify) | A collections section under a low heap |
| `benchmarks/comparable/list_ops.scala`, `map_build.scala` (new); `benchmarks/comparable/python/list_ops.py`, `map_build.py` (new); `benchmarks/run_benchmarks.py`, `benchmarks/RESULTS.md`, `benchmarks/README.md` (modify) | Benchmark suite v1 |
| `docs/tutorial/08-collections.md`, `docs/tutorial/10-strings-and-interpolation.md` (new); `docs/TUTORIAL.md`, `docs/tutorial/02-*.md`, `03-*.md` (modify); `tests/conformance/tutorial/08-*`, `10-*` (new) | The Documentation track |
| `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `docs/DESIGN.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt` | Status, D54–D70 (D57, D60 and D64 unused), version 0.4.0 |

`CMakeLists.txt` gains `src/runtime/CollectionPrimitives.cpp` and `src/runtime/Format.cpp` to `protoscala_runtime`, and `src/support/FormatSpec.cpp` to `protoscala_support` (which `protoscala_compiler` already links, so `Compiler.cpp` may include `support/FormatSpec.h` without touching protoCore). The library order is unchanged: `frontend ← compiler ← runtime ← repl ← protoscala`.

### Opcodes added in this phase

| # | Name | Stack effect | Notes |
|---|---|---|---|
| 39 | `CONCAT` | `[v1 .. vn] -> [str]` | operand: n ≥ 2; each `vi` is converted with `toScalaString` and joined with `ProtoString::appendLast` (A0-1, D54) |

`Opcodes.h`'s `// 39..63   reserved (Phase 1 additions)` comment becomes `// 40..63   reserved; 39 = CONCAT (Phase 3)`. `docs/STATUS.md`'s opcode table gains the same row (Task 14).

---

### Task 1: SmallInteger fast paths and the ±2^53 promotion boundary

**Files:**
- Modify: `src/runtime/ExecutionEngine.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`
- Test: `tests/conformance/15-integers/*.scala` (new), `tests/unit/test_engine.cpp` (modify)

**Interfaces:**
- `RuntimeLayout` gains, next to `applyName`:
```cpp
    const proto::ProtoString* unaryMinusName = nullptr;  // "unary_-"
    const proto::ProtoString* unaryNotName = nullptr;    // "unary_!"
```
- No public function is added; the opcode semantics of `ADD`, `SUB`, `MUL`, `LT`, `LE`, `GT`, `GE`, `EQ`, `NE`, `NEG`, `NOT` are unchanged in meaning.

- [ ] **Step 1: Write the failing boundary fixtures first**

`tests/conformance/15-integers/small-int-boundary.scala`:
```scala
// EXPECT: 9007199254740992 -9007199254740993 9007199254740991 true true
// 2^53 = 9007199254740992 is one past PROTO_SMALL_INT_MAX, so every line
// below crosses the SmallInteger -> LargeInteger promotion (DESIGN §3.6, D1).
@main def run(): Unit =
  val max = 9007199254740991L        // PROTO_SMALL_INT_MAX
  val min = -9007199254740992L       // PROTO_SMALL_INT_MIN
  val over = max + 1                 // promotes
  val under = min - 1                // promotes
  val back = over - 1                // demotes back into the SmallInteger range
  println(over.toString + " " + under + " " + back + " " + (back == max) + " " + (over > max))
```

`tests/conformance/15-integers/small-int-multiply-boundary.scala`:
```scala
// EXPECT: 81129638414606681695789005144064 true 9007199254740991
@main def run(): Unit =
  val max = 9007199254740991L
  val big = (max + 1) * (max + 1)    // 2^106, far outside 64 bits
  val square = big / (max + 1)
  println(big.toString + " " + (big > 0) + " " + (square - 1))
```

`tests/conformance/15-integers/promotion-is-invisible.scala`:
```scala
// EXPECT: true true true true
// A promoted value behaves exactly like a small one: ==, hashCode consistency,
// ordering and toString all agree across the boundary.
@main def run(): Unit =
  val a = 9007199254740991L + 1
  val b = 4503599627370496L * 2
  println((a == b).toString + " " + (a.## == b.##) + " " + (a <= b) + " " + (a.toString == b.toString))
```

`tests/conformance/15-integers/negate-boundary.scala`:
```scala
// EXPECT: 9007199254740992 -9007199254740991 true
// -PROTO_SMALL_INT_MIN does not fit a SmallInteger and must promote.
@main def run(): Unit =
  val min = -9007199254740992L
  val neg = -min
  println(neg.toString + " " + (-(min + 1)) + " " + (neg == -min))
```

`tests/conformance/15-integers/factorial-crosses-the-boundary.scala`:
```scala
// EXPECT: 2432902008176640000 51090942171709440000
@main def run(): Unit =
  def fact(n: Int): Int = if n <= 1 then 1 else n * fact(n - 1)
  println(fact(20).toString + " " + fact(21))
```

Also write the brace variants `small-int-boundary-braces.scala`, `negate-boundary-braces.scala` and `factorial-crosses-the-boundary-braces.scala` (the other two have no indentation-sensitive syntax beyond `@main`, so a `-braces` twin would be the same file).

Run `ctest --test-dir build_release -R '15-integers'` after `cmake --build build_release`. Expected at this point: `promotion-is-invisible` and `negate-boundary` pass (the promotion already works), and nothing crashes. This step establishes the *coverage* the ROADMAP asks for; Steps 2–4 are the defects the coverage exposes.

- [ ] **Step 2: Give `EQ` / `NE` a `SmallInteger` fast path**

In `src/runtime/ExecutionEngine.cpp`, replace the body of `case Op::EQ: case Op::NE:` (currently at `:755-760`) with:

```cpp
                case Op::EQ: case Op::NE: {
                    const proto::ProtoObject* a = sp[-2];
                    const proto::ProtoObject* b = sp[-1];
                    // Two SmallIntegers are equal exactly when their words are:
                    // the encoding is injective over [-(2^53), 2^53 - 1]. This
                    // is the most frequent comparison in a loop and it must not
                    // reach valuesEqual, which re-tests every tag.
                    const bool eq = (proto::isSmallInt(a) && proto::isSmallInt(b))
                                        ? (a == b)
                                        : valuesEqual(&frame, L, a, b);
                    sp[-2] = (eq == (op == Op::EQ)) ? PROTO_TRUE : PROTO_FALSE;
                    --sp;
                    continue;
                }
```

- [ ] **Step 3: Stop interning `unary_-` and `unary_!` on every execution**

In `src/runtime/Runtime.h`, add the two fields to `RuntimeLayout` as given under **Interfaces**. In `src/runtime/Runtime.cpp`, next to where `applyName` is interned, add:

```cpp
    layout_.unaryMinusName = proto::ProtoString::createSymbol(root, "unary_-");
    layout_.unaryNotName = proto::ProtoString::createSymbol(root, "unary_!");
```

In `src/runtime/ExecutionEngine.cpp`, replace the two `createSymbol` calls:

```cpp
                case Op::NEG: {
                    const proto::ProtoObject* a = sp[-1];
                    if (proto::isSmallInt(a) && proto::asSmallInt(a) != proto::PROTO_SMALL_INT_MIN)
                        sp[-1] = proto::makeSmallInt(-proto::asSmallInt(a));
                    else if (isNumberFast(a))
                        sp[-1] = a->negate(&frame);          // promotes -PROTO_SMALL_INT_MIN
                    else
                        sp[-1] = send(&frame, a, L.unaryMinusName, nullptr, 0);
                    continue;
                }
                case Op::NOT: {
                    const proto::ProtoObject* a = sp[-1];
                    if (a == PROTO_TRUE) sp[-1] = PROTO_FALSE;
                    else if (a == PROTO_FALSE) sp[-1] = PROTO_TRUE;
                    else sp[-1] = send(&frame, a, L.unaryNotName, nullptr, 0);
                    continue;
                }
```

Both symbols are longer than 6 bytes, so this also removes a per-execution `createSymbol` leak (R4).

- [ ] **Step 4: Prove the interning is gone, with a measurement**

```bash
cmake --build build_release
cat > ../.agent_scratch/phase3/neg-loop.scala <<'EOF'
class Box(val v: Int):
  def unary_- : Box = Box0.make(0 - v)
object Box0:
  def make(v: Int): Box = new Box(v)
@main def run(): Unit =
  var i = 0
  var acc = 0
  while i < 200000 do
    val b = new Box(i)
    acc += (-b).v
    i += 1
  println(acc)
EOF
PROTOCORE_HEAP_LIMIT_CELLS=2000000 build_release/protoscala ../.agent_scratch/phase3/neg-loop.scala
perf stat -r 3 build_release/protoscala ../.agent_scratch/phase3/neg-loop.scala 2>&1 | grep -E 'cycles|elapsed'
```

Expected: `-19999900000`, and the run completes under the heap limit (before the fix, 200 000 interned `unary_-` symbols make it grow without bound). Record the cycle count next to the Step 5 baseline.

- [ ] **Step 5: Re-run the benchmark suite and compare against the Preflight baseline**

```bash
python3 benchmarks/run_benchmarks.py --name phase3-task1 --runs 5 --warmup 2
diff <(grep '^|' benchmarks/reports/*phase3-baseline.md) <(grep '^|' benchmarks/reports/*phase3-task1.md) || true
```

Expected: no FAILED cell, and `int_sum_loop`, `sum_loop`, `fib` and `fib30` no worse than the baseline. `sum_loop` runs `i < n` and `acc + i` a million times, so a regression there would mean the `EQ` change hurt the branch layout (the protoPython "micro-opts can hurt icache layout" lesson); if any workload regresses by more than 3 %, back Step 2 out and record why.

**Done when:** `ctest --test-dir build_release -R '15-integers' --output-on-failure` reports 8/8 passing; `grep -c 'createSymbol' src/runtime/ExecutionEngine.cpp` returns `0`; `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release --output-on-failure` is fully green; and `benchmarks/reports/<date>-phase3-task1.md` shows no workload more than 3 % slower than the Preflight baseline.

---

### Task 2: String interpolation, part 1 — the parsed AST

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`
- Test: `tests/unit/test_parser.cpp` (modify)

**Interfaces:**
- `src/frontend/AST.h`: `InterpString` changes shape. Other tasks consume exactly this:
```cpp
// s"a ${x + 1} b": literals = {"a ", " b"}, args = {x + 1}.
// Invariant: literals.size() == args.size() + 1 (empty strings fill the ends).
// `specs[i]` is the f-interpolator format specifier that followed args[i]
// ("%05.2f"), or empty for `%s`; it is always empty for `s` and `raw`.
struct InterpString : Node {
    explicit InterpString(SourcePos p) : Node(NodeKind::InterpString, p) {}
    std::string interpolator;
    std::vector<std::string> literals;
    std::vector<NodePtr> args;
    std::vector<std::string> specs;
};
```
- `src/frontend/Parser.h`, in the private section:
```cpp
    // Turns an InterpolatedString token into a parsed InterpString node: each
    // hole's source text is lexed, laid out and parsed as one expression by a
    // nested Parser whose positions are rebased onto the hole's position.
    NodePtr parseInterpolation(const Token& t);
```

- [ ] **Step 1: Change the AST node**

Replace the existing `InterpString` (`AST.h:58-62`) with the declaration under **Interfaces**. `AST.h` no longer needs `InterpolationPart`, but `Token.h` keeps it — the lexer still produces it and only the parser consumes it.

- [ ] **Step 2: Write the nested parse**

In `src/frontend/Parser.cpp`, add, in the anonymous namespace near `isExtensionStart`:

```cpp
// The f-interpolator's specifier is the leading %… of the literal text that
// follows a hole. Returns its length in `text`, or 0 when the text does not
// start with a specifier. `%%` is an escaped percent, not a specifier.
std::size_t formatSpecLength(const std::string& text) {
    if (text.size() < 2 || text[0] != '%' || text[1] == '%') return 0;
    std::size_t k = 1;
    while (k < text.size() && (text[k] == '-' || text[k] == '+' || text[k] == ' ' ||
                               text[k] == '0' || text[k] == ',' || text[k] == '#')) ++k;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9') ++k;
    if (k < text.size() && text[k] == '.') {
        ++k;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') ++k;
    }
    if (k >= text.size()) return 0;
    const char c = text[k];
    const bool isConversion = c == 's' || c == 'b' || c == 'c' || c == 'd' || c == 'o' ||
                              c == 'x' || c == 'X' || c == 'e' || c == 'E' || c == 'f' ||
                              c == 'g' || c == 'G';
    return isConversion ? k + 1 : 0;
}
```

Then the method itself, next to `parseSimple`:

```cpp
NodePtr Parser::parseInterpolation(const Token& t) {
    auto n = std::make_unique<InterpString>(t.pos);
    n->interpolator = t.text;
    const bool isF = t.text == "f";
    std::string pending;                       // literal text accumulated so far
    bool afterHole = false;
    for (const InterpolationPart& part : t.parts) {
        if (!part.isHole) {
            std::string text = part.text;
            if (isF && afterHole) {
                const std::size_t len = formatSpecLength(text);
                if (len > 0) {
                    n->specs.back() = text.substr(0, len);
                    text = text.substr(len);
                }
            }
            pending += text;
            afterHole = false;
            continue;
        }
        n->literals.push_back(pending);
        pending.clear();
        // `$name` and `${expr}` are both parsed as one expression. The nested
        // Lexer/Layout/Parser is the same pipeline the file went through, so a
        // hole may hold any expression, including another interpolation.
        Lexer lexer(part.text, sourceName_);
        std::vector<Token> toks = applyLayout(lexer.tokenize());
        Parser sub(toks, sourceName_);
        NodePtr e = sub.parseHoleExpression(part.pos);
        n->args.push_back(std::move(e));
        n->specs.emplace_back();
        afterHole = true;
    }
    n->literals.push_back(pending);
    return n;
}
```

and the helper it calls, also in `Parser`:

```cpp
// Parses the whole token stream as one expression and rejects a trailing
// remainder, so `${ 1 2 }` fails at the hole rather than silently dropping `2`.
// Every position in the result is rebased onto `at`, the hole's position in the
// enclosing file, so a diagnostic points at the string, not at column 1.
NodePtr Parser::parseHoleExpression(SourcePos at) {
    while (at(TokenKind::Newline) || at(TokenKind::Indent)) advance();
    NodePtr e = parseExpr();
    while (this->at(TokenKind::Newline) || this->at(TokenKind::Outdent)) advance();
    if (!this->at(TokenKind::EndOfFile))
        throw ParseError("a string interpolation hole holds one expression", at, false);
    rebase(*e, at);
    return e;
}
```

`rebase(Node&, SourcePos)` is a free function in `AST.cpp` that walks the tree and replaces every `pos` with `at` (the hole has one position; sub-positions inside it would be meaningless in the enclosing file's coordinates). Declare it in `AST.h`:

```cpp
// Sets every position in the subtree to `at`. Used for expressions parsed out
// of an interpolation hole, whose own coordinates do not exist in the file.
void rebase(Node& n, SourcePos at);
```

- [ ] **Step 3: Call it from `parseSimple`**

Replace the `case TokenKind::InterpolatedString:` block of `Parser.cpp:533-540` with:

```cpp
        case TokenKind::InterpolatedString:
            base = parseInterpolation(t);
            advance();
            break;
```

- [ ] **Step 4: Unit-test the parse**

In `tests/unit/test_parser.cpp`, add:

```cpp
TEST(Parser, InterpolationSplitsLiteralsAndHoles) {
    const CompilationUnit u = parseSource(R"(val a = s"x=${1 + 2}!")");
    const auto& v = as<ValDef>(*u.stats[0]);
    const auto& s = as<InterpString>(*v.rhs);
    EXPECT_EQ(s.interpolator, "s");
    ASSERT_EQ(s.literals.size(), 2u);
    EXPECT_EQ(s.literals[0], "x=");
    EXPECT_EQ(s.literals[1], "!");
    ASSERT_EQ(s.args.size(), 1u);
    EXPECT_EQ(s.args[0]->kind, NodeKind::Infix);
    ASSERT_EQ(s.specs.size(), 1u);
    EXPECT_TRUE(s.specs[0].empty());
}

TEST(Parser, FInterpolatorTakesTheSpecifierOffTheFollowingLiteral) {
    const CompilationUnit u = parseSource(R"(val a = f"pi=$pi%.2f!")");
    const auto& s = as<InterpString>(*as<ValDef>(*u.stats[0]).rhs);
    ASSERT_EQ(s.specs.size(), 1u);
    EXPECT_EQ(s.specs[0], "%.2f");
    ASSERT_EQ(s.literals.size(), 2u);
    EXPECT_EQ(s.literals[0], "pi=");
    EXPECT_EQ(s.literals[1], "!");
}

TEST(Parser, SInterpolatorLeavesAPercentAlone) {
    const CompilationUnit u = parseSource(R"(val a = s"$n%done")");
    const auto& s = as<InterpString>(*as<ValDef>(*u.stats[0]).rhs);
    ASSERT_EQ(s.literals.size(), 2u);
    EXPECT_EQ(s.literals[1], "%done");
    EXPECT_TRUE(s.specs[0].empty());
}

TEST(Parser, AHoleHoldingTwoExpressionsIsRejected) {
    EXPECT_THROW(parseSource(R"(val a = s"${1 2}")"), ParseError);
}
```

**Done when:** `cmake --build build_release && ctest --test-dir build_release -R 'Parser' --output-on-failure` passes, including the four new cases, and `ctest --test-dir build_release --output-on-failure` is still fully green (the compiler still rejects `InterpString` with "string interpolation is not implemented yet", so no conformance fixture changes yet).

---

### Task 3: String interpolation, part 2 — `s`, `raw` and the `CONCAT` opcode

**Files:**
- Modify: `src/compiler/Opcodes.h`, `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `src/frontend/Desugar.cpp`, `src/runtime/ExecutionEngine.cpp`
- Test: `tests/conformance/16-strings/*.scala` (new), `tests/unit/test_bytecode.cpp` (modify)

**Interfaces:**
- `src/compiler/Opcodes.h`: `CONCAT = 39, // [v1 .. vn] -> [str]  operand: n >= 2` inside `enum class Op`, and a `case Op::CONCAT: return "CONCAT";` in `opName` (which lives in `src/compiler/BytecodeModule.cpp`, not `Opcodes.cpp`).
- `src/compiler/Compiler.h`, private: `void compileInterp(const InterpString& n);`

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/16-strings/interpolation-s.scala`:
```scala
// EXPECT: Alice is 30 and next year 31
@main def run(): Unit =
  val name = "Alice"
  val age = 30
  println(s"$name is $age and next year ${age + 1}")
```

`tests/conformance/16-strings/interpolation-s-braces.scala` is the same program in brace syntax.

`tests/conformance/16-strings/interpolation-raw.scala`:
```scala
// EXPECT: a\nb 2
@main def run(): Unit =
  val n = 2
  println(raw"a\nb $n")
```

`tests/conformance/16-strings/interpolation-calls-tostring.scala`:
```scala
// EXPECT: point=Point(1,2) list=List(1, 2) none=None unit=() null=null
case class Point(x: Int, y: Int)
@main def run(): Unit =
  val p = Point(1, 2)
  val xs = List(1, 2)
  val o: Option[Int] = None
  val u: Unit = ()
  val z: String = null
  println(s"point=$p list=$xs none=$o unit=$u null=$z")
```

`tests/conformance/16-strings/interpolation-empty-and-adjacent.scala`:
```scala
// EXPECT: 12 |1| ||
@main def run(): Unit =
  val a = 1
  val b = 2
  println(s"$a$b" + " " + s"|$a|" + " " + s"||")
```

`tests/conformance/16-strings/interpolation-escapes-dollar.scala`:
```scala
// EXPECT: $5 costs $5
@main def run(): Unit =
  val n = 5
  println(s"$$$n costs $$$n")
```

`tests/conformance/16-strings/interpolation-triple-quoted.scala`:
```scala
// EXPECT: line1 x=7 line2
@main def run(): Unit =
  val x = 7
  val s = s"""line1 x=$x line2"""
  println(s)
```

`tests/conformance/16-strings/interpolation-nested.scala`:
```scala
// EXPECT: outer[inner 3]
@main def run(): Unit =
  val n = 3
  println(s"outer[${s"inner $n"}]")
```

`tests/conformance/16-strings/interpolation-unknown.scala`:
```scala
// EXPECT-ERROR: unknown string interpolator 'json'
@main def run(): Unit =
  val n = 1
  println(json"$n")
```

Verify `interpolation-s`, `interpolation-raw`, `interpolation-calls-tostring`, `interpolation-escapes-dollar` and `interpolation-nested` against scalac (the `List(1, 2)` and `Point(1,2)` renderings are Phase 2 behaviour and already match; `None` and `()` are the ones worth checking):

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
cp tests/conformance/16-strings/interpolation-calls-tostring.scala ../.agent_scratch/phase3/ictos.scala
sed -i '1d' ../.agent_scratch/phase3/ictos.scala
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase3/out ../.agent_scratch/phase3/ictos.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase3/out" run
```

- [ ] **Step 2: Add the opcode**

In `src/compiler/Opcodes.h`, inside `enum class Op`, directly after the last entry of the `38..63` band that is already in use (`FORCE_THUNK = 38` at the time of writing):

```cpp
    // Phase 3: string interpolation (DESIGN §3.4, plan A0-1, D54)
    CONCAT = 39,  // [v1 .. vn] -> [str]  operand: n >= 2; each vi is converted
                  // with toScalaString and the pieces are joined with
                  // ProtoString::appendLast (an O(log n) rope join, no copy)
    // 40..63  reserved (Phase 1 additions)
```

and delete the old `// 39..63   reserved (Phase 1 additions)` comment. In `src/compiler/BytecodeModule.cpp`'s `opName`, add `case Op::CONCAT: return "CONCAT";` next to the `FORCE_THUNK` case.

- [ ] **Step 3: Implement the opcode**

In `src/runtime/ExecutionEngine.cpp`, inside the `switch (op)` of `runLoop`, directly after `case Op::NOT:`'s block:

```cpp
                case Op::CONCAT: {
                    const unsigned n = static_cast<unsigned>(operand);
                    if (n < 2)
                        throw std::logic_error("CONCAT of arity " + std::to_string(n) + " in " +
                                               mod.name());
                    const proto::ProtoObject** base = sp - n;
                    // toScalaString returns a string unchanged and calls a user
                    // toString through the active engine otherwise, so a rope
                    // argument is joined, never flattened.
                    const proto::ProtoObject* acc = toScalaString(&frame, L, base[0]);
                    base[0] = acc;                      // rooted: the next call allocates
                    for (unsigned k = 1; k < n; ++k) {
                        const proto::ProtoObject* piece = toScalaString(&frame, L, base[k]);
                        base[k] = piece;                // rooted for the same reason
                        base[0] = reinterpret_cast<const proto::ProtoString*>(base[0])
                                      ->appendLast(&frame,
                                                   reinterpret_cast<const proto::ProtoString*>(piece))
                                      ->asObject(&frame);
                    }
                    sp = base + 1;
                    continue;
                }
```

Writing each converted piece back into its operand-stack slot before the next allocation is not decoration: `toScalaString` allocates for a non-string, and an unrooted `acc` held only in a C++ local is exactly the Phase 5 `Mailbox::push` bug.

- [ ] **Step 4: Desugar `s` and `raw`, reject the rest**

In `src/frontend/Desugar.cpp`, in `Desugarer::expr`, replace the fall-through that currently returns `InterpString` unchanged (the `return n; // literals, identifiers, imports, interpolations` at `:147`) with an explicit case before it:

```cpp
        case NodeKind::InterpString: {
            auto& s = as<InterpString>(*n);
            if (s.interpolator != "s" && s.interpolator != "raw" && s.interpolator != "f")
                throw ParseError("unknown string interpolator '" + s.interpolator +
                                     "': only s, f and raw are available "
                                     "(custom interpolators need extension methods)",
                                 n->pos, false);
            for (NodePtr& a : s.args) a = expr(std::move(a));
            return n;   // the compiler lowers it (s/raw -> CONCAT, f -> __fmt)
        }
```

`raw` needs no separate handling: the lexer already leaves its escapes unprocessed (`Lexer::lexInterpolated`), so the literal parts arrive as written.

- [ ] **Step 5: Compile `s` and `raw`**

In `src/compiler/Compiler.cpp`, replace `case NodeKind::InterpString: throw CompileError(…)` (`:417-418`) with `case NodeKind::InterpString: compileInterp(as<InterpString>(n)); return;`, and add the method:

```cpp
// s"a${x}b" pushes "a", x, "b" and joins them with one CONCAT. Empty literal
// pieces are dropped: s"$a$b" emits two pushes, not four. A string with no hole
// never reaches here (the lexer produces a plain StringLit for "…").
void Compiler::compileInterp(const InterpString& n) {
    if (n.interpolator == "f") { compileFormat(n); return; }   // Task 4
    unsigned pieces = 0;
    for (std::size_t k = 0; k < n.args.size(); ++k) {
        if (!n.literals[k].empty()) {
            emit(Op::PUSH_CONST, fn_->mod->addString(n.literals[k]), n.pos, +1);
            ++pieces;
        }
        compileExpr(*n.args[k]);
        ++pieces;
    }
    if (!n.literals.back().empty()) {
        emit(Op::PUSH_CONST, fn_->mod->addString(n.literals.back()), n.pos, +1);
        ++pieces;
    }
    if (pieces == 0) {                      // s"" : an empty string, no CONCAT
        emit(Op::PUSH_CONST, fn_->mod->addString(""), n.pos, +1);
        return;
    }
    if (pieces == 1) {
        // One piece: it is either a literal (already a String) or one hole,
        // which still needs converting. `"" + v` is what ADD's string branch
        // does, so push an empty literal and let CONCAT do the conversion.
        emit(Op::PUSH_CONST, fn_->mod->addString(""), n.pos, +1);
        ++pieces;
    }
    emit(Op::CONCAT, pieces, n.pos, -static_cast<int>(pieces) + 1);
}
```

Declare `void compileFormat(const InterpString& n);` in `Compiler.h` now and leave it defined in Task 4; to keep this task's build green, add a temporary body in `Compiler.cpp` that throws `CompileError("the f interpolator is not implemented yet", n.pos)` and make `tests/conformance/16-strings/interpolation-f-basic.scala` (Task 4) an `// XFAIL-ERROR: the f interpolator is not implemented yet` fixture that Task 4 converts.

- [ ] **Step 6: Unit-test the emission**

In `tests/unit/test_bytecode.cpp`:

```cpp
TEST(Bytecode, InterpolationEmitsOneConcat) {
    const std::string asm_ = disassembleSource(R"(val a = 1; val s = s"x${a}y")");
    EXPECT_NE(asm_.find("CONCAT 3"), std::string::npos) << asm_;
    EXPECT_EQ(asm_.find("SEND"), std::string::npos) << asm_;   // no StringContext send
}

TEST(Bytecode, AdjacentHolesEmitNoEmptyLiterals) {
    const std::string asm_ = disassembleSource(R"(val a = 1; val b = 2; val s = s"$a$b")");
    EXPECT_NE(asm_.find("CONCAT 2"), std::string::npos) << asm_;
}
```

**Done when:** `ctest --test-dir build_release -R '16-strings' --output-on-failure` passes every fixture of Step 1 except `interpolation-f-basic` (still `XFAIL-ERROR`); `build_release/protoscala --disassemble tests/conformance/16-strings/interpolation-s.scala | grep -c CONCAT` returns `1`; and the full suite is green, also under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 4: String interpolation, part 3 — the `f` interpolator

**Files:**
- New: `src/runtime/Format.h`, `src/runtime/Format.cpp`
- Modify: `src/compiler/Compiler.cpp`, `src/runtime/Primitives.cpp`, `CMakeLists.txt`
- Test: `tests/conformance/16-strings/interpolation-f-*.scala` (new), `tests/unit/test_format.cpp` (new), `tests/unit/CMakeLists.txt` (modify)

**Interfaces:**
- `src/runtime/Format.h`:
```cpp
#pragma once
#include "protoCore.h"
#include <string>
#include <vector>

namespace protoScala {
struct RuntimeLayout;

// One conversion of a compiled f-interpolator format string.
struct FormatSpec {
    std::string literal;   // the text emitted before this conversion
    char conversion = 's'; // s b c d o x X e E f g G
    bool leftAlign = false;
    bool plusSign = false;
    bool spaceSign = false;
    bool zeroPad = false;
    bool grouping = false;
    bool alternate = false;
    int  width = -1;       // -1: none
    int  precision = -1;   // -1: none
};

// Parses "%[flags][width][.precision]conv". Throws std::invalid_argument with a
// user-facing message when the specifier is not one of A0-2's (compile time).
FormatSpec parseFormatSpec(const std::string& spec);

// Renders one value. `L` is needed for show()/typeName() on the error paths.
std::string formatOne(proto::ProtoContext* ctx, const RuntimeLayout& L, const FormatSpec& spec,
                      const proto::ProtoObject* v);
} // namespace protoScala
```
- `Compiler` lowers `f"…"` to `[__fmt][spec0][a0][spec1][a1]…[tail] SEND_APPLY __fmt`, i.e. an ordinary call of the native global `__fmt(s0, a0, s1, a1, …, sn)` whose odd arguments are the already-parsed specifier strings (including their leading literal text) and whose even arguments are the values. `__fmt` is added to `builtinGlobalNames()`.

- [ ] **Step 1: Write the failing fixtures**

Convert `tests/conformance/16-strings/interpolation-f-basic.scala` from the Task 3 `XFAIL-ERROR` stub to:
```scala
// EXPECT: pi=3.14 n=00042 hex=ff pct=50%
@main def run(): Unit =
  val pi = 3.14159
  val n = 42
  val h = 255
  println(f"pi=$pi%.2f n=$n%05d hex=$h%x pct=$n%%")
```

Wait — `$n%%` would consume `%%` as a literal percent after the hole, so `n` renders with `%s`. Write it as the fixture above and confirm against scalac.

`tests/conformance/16-strings/interpolation-f-alignment.scala`:
```scala
// EXPECT: |left      ||     right||+7|| 7|
@main def run(): Unit =
  val s = "left"
  val r = "right"
  val n = 7
  println(f"|$s%-10s||$r%10s||$n%+d||$n% d|")
```

`tests/conformance/16-strings/interpolation-f-defaults.scala`:
```scala
// EXPECT: a=1 b=hello c=true
@main def run(): Unit =
  val a = 1
  val b = "hello"
  val c = true
  println(f"a=$a b=$b c=$c")
```

`tests/conformance/16-strings/interpolation-f-scientific.scala`:
```scala
// EXPECT: 1.235e+03 1.23e+03
@main def run(): Unit =
  val x = 1234.5678
  println(f"$x%.3e $x%.2e")
```

`tests/conformance/16-strings/interpolation-f-bad-spec.scala`:
```scala
// EXPECT-ERROR: unsupported format specifier
@main def run(): Unit =
  val n = 1
  println(f"$n%q")
```

`tests/conformance/16-strings/interpolation-f-type-mismatch.scala`:
```scala
// EXPECT-ERROR: IllegalArgumentException: %d expects an integer, got String
@main def run(): Unit =
  val s = "x"
  println(f"$s%d")
```

Verify the first four against scalac exactly as Task 3 Step 1 does. `%q` is a *compile* error in scalac too, so `interpolation-f-bad-spec` matches; `f"$s%d"` is a *compile* error in scalac (it has static types) and a run-time error here, which is D4 and needs no new id — say so in the fixture's comment.

- [ ] **Step 2: Write `parseFormatSpec`**

`src/runtime/Format.cpp`:

```cpp
#include "runtime/Format.h"
#include "runtime/Errors.h"
#include "runtime/Values.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace protoScala {

FormatSpec parseFormatSpec(const std::string& spec) {
    FormatSpec out;
    if (spec.empty()) return out;                      // no specifier: %s
    if (spec[0] != '%') throw std::invalid_argument("a format specifier starts with '%'");
    std::size_t k = 1;
    for (; k < spec.size(); ++k) {
        if (spec[k] == '-') out.leftAlign = true;
        else if (spec[k] == '+') out.plusSign = true;
        else if (spec[k] == ' ') out.spaceSign = true;
        else if (spec[k] == '0') out.zeroPad = true;
        else if (spec[k] == ',') out.grouping = true;
        else if (spec[k] == '#') out.alternate = true;
        else break;
    }
    std::size_t digits = k;
    while (k < spec.size() && spec[k] >= '0' && spec[k] <= '9') ++k;
    if (k > digits) out.width = std::stoi(spec.substr(digits, k - digits));
    if (k < spec.size() && spec[k] == '.') {
        ++k;
        digits = k;
        while (k < spec.size() && spec[k] >= '0' && spec[k] <= '9') ++k;
        out.precision = k > digits ? std::stoi(spec.substr(digits, k - digits)) : 0;
    }
    if (k + 1 != spec.size())
        throw std::invalid_argument("unsupported format specifier '" + spec + "'");
    out.conversion = spec[k];
    switch (out.conversion) {
        case 's': case 'b': case 'c': case 'd': case 'o': case 'x': case 'X':
        case 'e': case 'E': case 'f': case 'g': case 'G':
            break;
        default:
            throw std::invalid_argument("unsupported format specifier '" + spec + "'");
    }
    return out;
}

} // namespace protoScala
```

- [ ] **Step 3: Write `formatOne`**

Append to `src/runtime/Format.cpp`:

```cpp
namespace {

std::string pad(const std::string& body, const FormatSpec& spec) {
    if (spec.width < 0 || static_cast<int>(body.size()) >= spec.width) return body;
    const std::string fill(static_cast<std::size_t>(spec.width) - body.size(), ' ');
    return spec.leftAlign ? body + fill : fill + body;
}

std::string groupDigits(const std::string& digits) {
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t k = 0; k < n; ++k) {
        if (k > 0 && (n - k) % 3 == 0) out += ',';
        out += digits[k];
    }
    return out;
}

// Zero padding applies inside the sign, as printf does: %+07d of 42 is "+000042".
std::string padNumeric(std::string sign, std::string body, const FormatSpec& spec) {
    if (spec.width >= 0 && spec.zeroPad && !spec.leftAlign) {
        const int room = spec.width - static_cast<int>(sign.size() + body.size());
        if (room > 0) body = std::string(static_cast<std::size_t>(room), '0') + body;
    }
    return pad(sign + body, spec);
}

} // namespace

std::string formatOne(proto::ProtoContext* ctx, const RuntimeLayout& L, const FormatSpec& spec,
                      const proto::ProtoObject* v) {
    switch (spec.conversion) {
        case 's': {
            std::string body = show(ctx, L, v);
            if (spec.precision >= 0 && body.size() > static_cast<std::size_t>(spec.precision))
                body = body.substr(0, static_cast<std::size_t>(spec.precision));
            return pad(body, spec);
        }
        case 'b':
            return pad(v == PROTO_NONE ? "false"
                       : v == PROTO_FALSE ? "false" : "true", spec);
        case 'c': {
            if (!isCharFast(v))
                throw ScalaError("IllegalArgumentException",
                                 "%c expects a Char, got " + typeName(ctx, L, v));
            std::string body;
            appendUtf8(body, charValueFast(v));
            return pad(body, spec);
        }
        case 'd': case 'o': case 'x': case 'X': {
            if (!isIntegerFast(v) && !isCharFast(v))
                throw ScalaError("IllegalArgumentException",
                                 std::string("%") + spec.conversion + " expects an integer, got " +
                                     typeName(ctx, L, v));
            const proto::ProtoObject* n = widenChar(v);
            const int base = spec.conversion == 'd' ? 10 : spec.conversion == 'o' ? 8 : 16;
            // asIntegerString is bignum-safe: it works for LargeInteger too (D1).
            std::string digits = n->asIntegerString(ctx, base)->toStdString(ctx);
            std::string sign;
            if (!digits.empty() && digits[0] == '-') { sign = "-"; digits = digits.substr(1); }
            else if (spec.plusSign) sign = "+";
            else if (spec.spaceSign) sign = " ";
            if (spec.conversion == 'X')
                for (char& c : digits) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (spec.grouping && spec.conversion == 'd') digits = groupDigits(digits);
            return padNumeric(sign, digits, spec);
        }
        case 'e': case 'E': case 'f': case 'g': case 'G': {
            if (!isNumberFast(v))
                throw ScalaError("IllegalArgumentException",
                                 std::string("%") + spec.conversion +
                                     " expects a number, got " + typeName(ctx, L, v));
            const double d = v->asDouble(ctx);
            char fmt[16];
            std::snprintf(fmt, sizeof fmt, "%%.%d%c", spec.precision >= 0 ? spec.precision : 6,
                          spec.conversion);
            char buf[512];
            std::snprintf(buf, sizeof buf, fmt, d);
            std::string body(buf);
            std::string sign;
            if (!body.empty() && body[0] == '-') { sign = "-"; body = body.substr(1); }
            else if (spec.plusSign) sign = "+";
            else if (spec.spaceSign) sign = " ";
            return padNumeric(sign, body, spec);
        }
        default:
            throw ScalaError("IllegalArgumentException",
                             std::string("unsupported format conversion '") + spec.conversion + "'");
    }
}
```

`%g` on a `LargeInteger` goes through `asDouble`, which loses precision — say so in the tutorial and record it as **D66**: `%e`/`%f`/`%g` convert their argument to a `Double` first, so an integer above 2^53 prints rounded; use `%d`.

- [ ] **Step 4: Compile `f"…"` and add the native**

In `src/compiler/Compiler.cpp`, replace the temporary `compileFormat` body with:

```cpp
// f"a$x%05db" becomes __fmt("a", x, "%05d", "b"): the literal before each hole,
// the value, its specifier, and finally the trailing literal. The specifiers are
// validated here so a bad one is a compile error at the right position (A0-2).
void Compiler::compileFormat(const InterpString& n) {
    for (const std::string& spec : n.specs) {
        if (spec.empty()) continue;
        try { (void)parseFormatSpec(spec); }
        catch (const std::invalid_argument& e) { throw CompileError(e.what(), n.pos); }
    }
    const std::size_t argc = 1 + 3 * n.args.size();     // tail + (literal, value, spec) each
    emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(globals_.binding("__fmt")->key), n.pos, +1);
    for (std::size_t k = 0; k < n.args.size(); ++k) {
        emit(Op::PUSH_CONST, fn_->mod->addString(n.literals[k]), n.pos, +1);
        compileExpr(*n.args[k]);
        emit(Op::PUSH_CONST, fn_->mod->addString(n.specs[k]), n.pos, +1);
    }
    emit(Op::PUSH_CONST, fn_->mod->addString(n.literals.back()), n.pos, +1);
    emit(Op::CALL, static_cast<std::uint64_t>(argc), n.pos, -static_cast<int>(argc));
}
```

`Compiler.cpp` gains `#include "runtime/Format.h"`; to keep the compiler free of protoCore (the header comment of `Compiler.h` says "Pure C++: it never touches protoCore"), split `Format.h` so that `FormatSpec` and `parseFormatSpec` live in `src/support/FormatSpec.h` (no protoCore include) and only `formatOne` stays in `src/runtime/Format.h`. Do that split now rather than later.

In `src/runtime/Primitives.cpp`, add the native next to `prim_raise`:

```cpp
// __fmt(lit0, v0, spec0, lit1, v1, spec1, ..., litN): the compiled form of an
// f-interpolator (Compiler::compileFormat). Arity is always 3k + 1.
PRIM(prim_fmt) {
    const unsigned long n = argCount(ctx, args);
    if (n == 0 || n % 3 != 1)
        throw ScalaError("IllegalArgumentException", "__fmt takes 3k + 1 arguments, got " +
                                                         std::to_string(n));
    const RuntimeLayout& L = layoutOf();
    std::string out;
    for (unsigned long k = 0; k + 1 < n; k += 3) {
        out += stringArg(ctx, args->getAt(ctx, static_cast<int>(k)), "__fmt");
        const ProtoObject* v = args->getAt(ctx, static_cast<int>(k + 1));
        const std::string spec = stringArg(ctx, args->getAt(ctx, static_cast<int>(k + 2)), "__fmt");
        out += formatOne(ctx, L, parseFormatSpec(spec), v);
    }
    out += stringArg(ctx, args->getAt(ctx, static_cast<int>(n - 1)), "__fmt");
    return str(ctx, out);
}
```

Add `{"__fmt", &prim_fmt}` to the globals `MethodEntry` array (`Primitives.cpp:867`) and `"__fmt"` to `builtinGlobalNames()` (`:857`).

- [ ] **Step 5: Unit-test the formatter directly**

`tests/unit/test_format.cpp` (new; add it to `tests/unit/CMakeLists.txt`'s `protoscala_unit_tests` source list):

```cpp
#include "support/FormatSpec.h"
#include <gtest/gtest.h>

using namespace protoScala;

TEST(FormatSpec, ParsesFlagsWidthAndPrecision) {
    const FormatSpec s = parseFormatSpec("%-+08,.3f");
    EXPECT_TRUE(s.leftAlign);
    EXPECT_TRUE(s.plusSign);
    EXPECT_TRUE(s.zeroPad);
    EXPECT_TRUE(s.grouping);
    EXPECT_EQ(s.width, 8);
    EXPECT_EQ(s.precision, 3);
    EXPECT_EQ(s.conversion, 'f');
}

TEST(FormatSpec, AnEmptySpecIsPercentS) {
    EXPECT_EQ(parseFormatSpec("").conversion, 's');
}

TEST(FormatSpec, RejectsAnUnknownConversion) {
    EXPECT_THROW(parseFormatSpec("%q"), std::invalid_argument);
    EXPECT_THROW(parseFormatSpec("%5"), std::invalid_argument);
}
```

**Done when:** `ctest --test-dir build_release -R '16-strings|FormatSpec' --output-on-failure` passes all fourteen cases; the four verifiable fixtures produce byte-identical output under `/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0` (`bin/scalac` + `java -cp "$SCALA_HOME/lib/*:out"`); and the full suite is green, also under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 5: The prelude — `Either`, the extended `Option`, the extended `Try`

**Files:**
- Modify: `lib/prelude.scala`, `src/runtime/Runtime.h`, `src/runtime/Primitives.cpp`
- Test: `tests/conformance/19-either-try/*.scala` (new)

**Interfaces:**
- `lib/prelude.scala` gains, exactly:
```scala
sealed abstract class Either[+A, +B]
final case class Left[+A, +B](value: A) extends Either[A, B]
final case class Right[+A, +B](value: B) extends Either[A, B]
def __mkLeft[A, B](v: A): Either[A, B] = Left(v)
def __mkRight[A, B](v: B): Either[A, B] = Right(v)
```
- `RuntimeLayout::PreludeHooks` gains `const proto::ProtoObject* left = nullptr;` and `const proto::ProtoObject* right = nullptr;`, resolved by `bindPreludeHooks` from the global keys of `__mkLeft` and `__mkRight`. Task 9 (`Map.get`) and Task 8 (`toEither`) consume them.

- [ ] **Step 1: Restate the Phase 4 coupling in the prelude**

At the top of the `Try` block in `lib/prelude.scala`, replace the existing comment with:

```scala
// Phase 5 shipped Try/Success/Failure early (D44) because there were no
// exception values yet. Phase 3 only ADDS combinators; it must not re-point
// Failure's payload. Phase 4 replaces RuntimeError with a real Throwable in one
// edit here plus the `await` raise path, and closes D44 then (plan A0-13).
```

- [ ] **Step 2: Extend `Option`**

Add to the `Option` body in `lib/prelude.scala`, after `toList`:

```scala
  def fold[B](ifEmpty: B)(f: A => B): B = if isEmpty then ifEmpty else f(get)
  def toRight[X](left: X): Either[X, A] = if isEmpty then Left(left) else Right(get)
  def toLeft[X](right: X): Either[A, X] = if isEmpty then Right(right) else Left(get)
  def orNull: A = if isEmpty then null else get
  def forall(p: A => Boolean): Boolean = isEmpty || p(get)
  def count(p: A => Boolean): Int = if !isEmpty && p(get) then 1 else 0
  def zip[B](that: Option[B]): Option[(A, B)] =
    if isEmpty || that.isEmpty then None else Some((get, that.get))
  def iterator: List[A] = toList
  def toSeq: List[A] = toList
```

`fold` takes two parameter lists exactly as Scala's does; multiple parameter lists on a `def` have worked since Phase 1.

- [ ] **Step 3: Add `Either`**

Add to `lib/prelude.scala`, after the `Option` block:

```scala
// Phase 3: the disjoint union. `map`/`flatMap`/`foreach`/`withFilter` are
// right-biased, as they are in Scala 2.13 and Scala 3.
sealed abstract class Either[+A, +B]:
  def isLeft: Boolean
  def isRight: Boolean = !isLeft
  def map[C](f: B => C): Either[A, C]
  def flatMap[C](f: B => Either[A, C]): Either[A, C]
  def foreach[U](f: B => U): Unit
  def getOrElse[C >: B](default: C): C
  def fold[C](fa: A => C, fb: B => C): C
  def swap: Either[B, A]
  def toOption: Option[B]
  def toList: List[B]
  def exists(p: B => Boolean): Boolean
  def forall(p: B => Boolean): Boolean
  def contains[C >: B](elem: C): Boolean

final case class Left[+A, +B](value: A) extends Either[A, B]:
  def isLeft: Boolean = true
  def map[C](f: B => C): Either[A, C] = Left(value)
  def flatMap[C](f: B => Either[A, C]): Either[A, C] = Left(value)
  def foreach[U](f: B => U): Unit = ()
  def getOrElse[C >: B](default: C): C = default
  def fold[C](fa: A => C, fb: B => C): C = fa(value)
  def swap: Either[B, A] = Right(value)
  def toOption: Option[B] = None
  def toList: List[B] = Nil
  def exists(p: B => Boolean): Boolean = false
  def forall(p: B => Boolean): Boolean = true
  def contains[C >: B](elem: C): Boolean = false

final case class Right[+A, +B](value: B) extends Either[A, B]:
  def isLeft: Boolean = false
  def map[C](f: B => C): Either[A, C] = Right(f(value))
  def flatMap[C](f: B => Either[A, C]): Either[A, C] = f(value)
  def foreach[U](f: B => U): Unit = f(value)
  def getOrElse[C >: B](default: C): C = value
  def fold[C](fa: A => C, fb: B => C): C = fb(value)
  def swap: Either[B, A] = Left(value)
  def toOption: Option[B] = Some(value)
  def toList: List[B] = value :: Nil
  def exists(p: B => Boolean): Boolean = p(value)
  def forall(p: B => Boolean): Boolean = p(value)
  def contains[C >: B](elem: C): Boolean = value == elem

def __mkLeft[A, B](v: A): Either[A, B] = Left(v)
def __mkRight[A, B](v: B): Either[A, B] = Right(v)
```

There is no `withFilter` on `Either`: Scala's needs a `Left` to fall back to, which requires a static type. Record as **D67**: `for (x <- e if p)` over an `Either` is rejected with `NoSuchMethodError: value withFilter is not a member of Right`; write `e.filterOrElse`-style code by hand.

- [ ] **Step 4: Extend `Try`**

Add to the `Try` body in `lib/prelude.scala`, after `toOption`:

```scala
  def map[B](f: A => B): Try[B]
  def flatMap[B](f: A => Try[B]): Try[B]
  def foreach[U](f: A => U): Unit
  def recover[B >: A](f: RuntimeError => B): Try[B]
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B]
  def orElse[B >: A](alternative: Try[B]): Try[B] = if isSuccess then this else alternative
  def toEither: Either[RuntimeError, A]
```

and to `Success`:

```scala
  def map[B](f: A => B): Try[B] = __tryOf(() => f(value))
  def flatMap[B](f: A => Try[B]): Try[B] = f(value)
  def foreach[U](f: A => U): Unit = f(value)
  def recover[B >: A](f: RuntimeError => B): Try[B] = this
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B] = this
  def toEither: Either[RuntimeError, A] = Right(value)
```

and to `Failure`:

```scala
  def map[B](f: A => B): Try[B] = Failure(error)
  def flatMap[B](f: A => Try[B]): Try[B] = Failure(error)
  def foreach[U](f: A => U): Unit = ()
  def recover[B >: A](f: RuntimeError => B): Try[B] = __tryOf(() => f(error))
  def recoverWith[B >: A](f: RuntimeError => Try[B]): Try[B] = f(error)
  def toEither: Either[RuntimeError, A] = Left(error)
```

`__tryOf` is the native `Try.apply` of Step 5. `Try` has no `filter`/`withFilter` in Phase 3 for the same reason `Either` has none (a failed filter needs an exception value); record it under **D67**.

- [ ] **Step 5: `Try.apply` as a native**

`Try` is a prelude class, so `Try(() => e)` needs a companion `apply`. A prelude `object Try` cannot be written (a companion must be in the same input and `Try` is `sealed abstract`, which is fine — but the body must catch a failure, and there is no `try`/`catch` yet). Implement `__tryOf` as a native in `src/runtime/Primitives.cpp`:

```cpp
// __tryOf(f): runs f() and wraps the result. Until Phase 4 there is no `catch`
// in the language, so the catch lives here: a ScalaError becomes
// Failure(RuntimeError(class, message)) (D44). Phase 4 replaces this with a
// Scala-level try/catch in the prelude and deletes this primitive.
PRIM(prim_try_of) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "Try.apply", 1);
    if (!compiledModuleOf(ctx, L, f) && !f->isMethod(ctx))
        throw ScalaError("IllegalArgumentException",
                         "Try.apply expects a function: write Try(() => expr)");
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    try {
        slot[0] = engine->invoke(&scope, f, nullptr, 0);
        slot[1] = engine->invoke(&scope, L.hooks.success, &slot[0], 1);
    } catch (const ScalaError& e) {
        slot[0] = str(&scope, e.className());
        slot[1] = str(&scope, e.message());
        const ProtoObject* err = engine->invoke(&scope, L.hooks.runtimeError, &slot[0], 2);
        slot[0] = err;
        slot[1] = engine->invoke(&scope, L.hooks.failure, &slot[0], 1);
    }
    scope.returnValue = slot[1];
    return slot[1];
}
```

Add `{"__tryOf", &prim_try_of}` to the globals table and `"__tryOf"` to `builtinGlobalNames()`, then add to `lib/prelude.scala`:

```scala
// Try { expr }: a by-name parameter, since by-name landed on main (bca0352).
// Try(() => expr) stays accepted, for programs written against Future.apply's
// function form (D47). No deviation: D64 is unused (plan A0-12).
object Try:
  def apply[A](f: () => A): Try[A] = __tryOf(f)
```

A prelude `object Try` beside `sealed abstract class Try` is a companion pair in one input, which Phase 2's D25 rule allows.

**If Preflight Step 0 found by-name parameters in the tree**, write this instead and record no deviation:

```scala
// Try { risky() }: the argument is by-name, so the block is re-evaluated inside
// the primitive's catch. The function form Try(() => expr) also works, because a
// zero-argument function passed to a by-name parameter is already a thunk.
object Try:
  def apply[A](body: => A): Try[A] = __tryOf(() => body)
```

and leave `prim_try_of` exactly as written: it still receives a zero-argument function, because the compiler's by-name lowering wraps `body` in one.

- [ ] **Step 6: Fixtures**

`tests/conformance/19-either-try/either-basics.scala`:
```scala
// EXPECT: Right(4) Left(bad) 4 fallback true false
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val l: Either[String, Int] = Left("bad")
  println(r.map(_ * 2).toString + " " + l.map(_ * 2) + " " + r.map(_ * 2).getOrElse(0) + " " +
    l.getOrElse("fallback") + " " + r.isRight + " " + l.isRight)
```

`tests/conformance/19-either-try/either-fold-and-swap.scala`:
```scala
// EXPECT: err:bad ok:2 Left(2) Right(bad)
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val l: Either[String, Int] = Left("bad")
  println(l.fold(s => "err:" + s, n => "ok:" + n) + " " +
    r.fold(s => "err:" + s, n => "ok:" + n) + " " + r.swap + " " + l.swap)
```

`tests/conformance/19-either-try/either-for-comprehension.scala`:
```scala
// EXPECT: Right(5) Left(no)
@main def run(): Unit =
  val a: Either[String, Int] = Right(2)
  val b: Either[String, Int] = Right(3)
  val bad: Either[String, Int] = Left("no")
  val ok = for
    x <- a
    y <- b
  yield x + y
  val ko = for
    x <- bad
    y <- b
  yield x + y
  println(ok.toString + " " + ko)
```

`tests/conformance/19-either-try/try-combinators.scala`:
```scala
// EXPECT: Success(2) true 2 Success(4) Success(-1) 99
@main def run(): Unit =
  val ok = Try(() => 2)
  val ko = Try(() => "x".toInt)
  println(ok.toString + " " + ko.isFailure + " " + ok.get + " " + ok.map(_ * 2) + " " +
    ko.map(_ * 2).recover(e => -1) + " " + ko.getOrElse(99))
```

`tests/conformance/19-either-try/try-to-either.scala`:
```scala
// EXPECT: Right(7) true
@main def run(): Unit =
  val ok = Try(() => 7)
  val ko = Try(() => None.get)
  println(ok.toEither.toString + " " + ko.toEither.isLeft)
```

`tests/conformance/19-either-try/option-fold-and-toright.scala`:
```scala
// EXPECT: 4 0 Right(2) Left(missing) true
@main def run(): Unit =
  val s: Option[Int] = Some(2)
  val n: Option[Int] = None
  println(s.fold(0)(_ * 2).toString + " " + n.fold(0)(_ * 2) + " " + s.toRight("missing") + " " +
    n.toRight("missing") + " " + s.zip(Some("a")).nonEmpty)
```

Add a `-braces` twin for `either-for-comprehension` (its `for` body differs between syntaxes). Verify `either-basics`, `either-fold-and-swap`, `either-for-comprehension` and `option-fold-and-toright` against scalac.

**Done when:** `ctest --test-dir build_release -R '19-either-try' --output-on-failure` passes all seven fixtures; `build_release/protoscala --version` still starts (a broken prelude fails the binary outright); and `benchmarks/cold-start.sh build_release/protoscala 20` is recorded in the Task 14 notes, because the prelude just grew and DESIGN §1's < 25 ms budget is already at the line.

---

### Task 6: `Range`

**Files:**
- New: `src/runtime/CollectionPrimitives.cpp`
- Modify: `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `src/runtime/Values.h`, `src/runtime/Values.cpp`, `src/compiler/ClassInfo.cpp`, `CMakeLists.txt`
- Test: `tests/conformance/17-collections/range-*.scala` (new), `tests/unit/test_collections.cpp` (new)

**Interfaces:**
- `RuntimeLayout` gains:
```cpp
    proto::ProtoObject* rangeProto = nullptr;          // Range (DESIGN §6)
    const proto::ProtoString* rangeStartKey = nullptr;     // "__start__"
    const proto::ProtoString* rangeEndKey = nullptr;       // "__end__"
    const proto::ProtoString* rangeStepKey = nullptr;      // "__step__"
    const proto::ProtoString* rangeInclusiveKey = nullptr; // "__inclusive__"
```
- `src/runtime/Values.h` gains the whole predicate family at once — they share one shape and Task 9's `scalaIsIdentityKey` consumes all four, so declaring them here keeps every later task compiling in order:
```cpp
// Is `v` one of the Phase 3 collection objects? Each tests the one attribute
// that object kind always carries. The `key != nullptr` guard is load-bearing:
// the tasks land in the order Range, Map/Set, Vector, so a predicate may be
// called before its own prototype and key exist, and it must answer false
// rather than probe a null key.
bool isRangeFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isMapFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isSetFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
bool isVectorFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
```
- `src/runtime/Values.h` also gains the sequence view that `valuesEqual` and `scalaHash` share (A0-8). Task 10 extends it with the `Vector` branch; Tasks 9 and 10 consume it, so it is declared here:
```cpp
// A read-only view of a Scala Seq — a List, a Vector, or a Range — used by
// valuesEqual and scalaHash so that List(1,2) == Vector(1,2) == (1 to 2) and the
// three hash alike (DESIGN §6, plan A0-7 and A0-8). It ALLOCATES NOTHING: a
// Range is read arithmetically, never materialised, which is what makes
// cross-kind Seq equality affordable on the EQ opcode's path.
struct SeqView {
    const proto::ProtoList* list = nullptr;  // List, or a Vector's __vec__
    long long start = 0;                     // Range only
    long long step = 1;                      // Range only
    long long size = 0;
    bool isRange = false;
    bool valid = false;                      // false: `v` is not a Seq at all
};
SeqView seqViewOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
// Element `i` of the view; `i` must be in [0, size).
const proto::ProtoObject* seqElemAt(proto::ProtoContext* ctx, const SeqView& s, long long i);
```

Implement the four predicates in `Values.cpp` now, in that exact shape — for example:
```cpp
bool isRangeFast(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    return L.rangeStartKey != nullptr && isObjectCellFast(v) &&
           v->getAttribute(ctx, L.rangeStartKey) != nullptr;
}
```
with `L.mapDataKey` for `isMapFast` and `isSetFast` (distinguished by the object's `__name__`, since both carry `__map__`) and `L.vecDataKey` for `isVectorFast`. Task 9 adds the two keys and Task 10 the third; until then each predicate answers `false`, which is correct.
- `src/runtime/Primitives.h` gains `void installCollectionPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);`, called from `installPrimitives` after `installProductPrimitives`.
- `ClassInfo.cpp`'s `builtinTypes()` gains `{"Range", "@Range", ClassKind::Class, …, .builtin = true, .linearization = {"@Range", kAnyRefKey, kAnyKey}}`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/17-collections/range-basics.scala`:
```scala
// EXPECT: List(0, 1, 2, 3, 4) List(1, 2, 3, 4, 5) 5 5 true false
@main def run(): Unit =
  val u = 0 until 5
  val t = 1 to 5
  println(u.toList.toString + " " + t.toList + " " + u.length + " " + t.length + " " +
    u.contains(4) + " " + u.contains(5))
```

`tests/conformance/17-collections/range-by-step.scala`:
```scala
// EXPECT: List(0, 2, 4, 6, 8) List(10, 7, 4, 1) List()
@main def run(): Unit =
  println((0 until 10 by 2).toList.toString + " " + (10 to 1 by -3).toList + " " +
    (5 until 5).toList)
```

`tests/conformance/17-collections/range-for-comprehension.scala`:
```scala
// EXPECT: 285 List(0, 2, 4, 6, 8)
@main def run(): Unit =
  var sum = 0
  for i <- 0 until 10 do
    sum += i * i
  val evens = for i <- 0 until 10 if i % 2 == 0 yield i
  println(sum.toString + " " + evens)
```

`tests/conformance/17-collections/range-for-comprehension-braces.scala` is the same program in brace syntax.

`tests/conformance/17-collections/range-tostring.scala`:
```scala
// EXPECT: Range 0 until 5 Range 1 to 5 Range 0 until 10 by 2
@main def run(): Unit =
  println((0 until 5).toString + " " + (1 to 5) + " " + (0 until 10 by 2))
```

`tests/conformance/17-collections/range-equals-list-and-vector.scala`:
```scala
// EXPECT: true true true true false
// A0-8: Scala Seq equality across kinds. A Range, a List and a Vector of the same
// elements are ==, and hash alike, so a Map keyed by one is found by another.
// Implemented through the allocation-free SeqView, not by materialising.
@main def run(): Unit =
  println(((0 until 3) == List(0, 1, 2)).toString + " " +
    (List(0, 1, 2) == (0 until 3)) + " " +
    ((0 until 3).## == List(0, 1, 2).##) + " " +
    (Map(List(0, 1, 2) -> "hit").getOrElse(0 until 3, "miss") == "hit") + " " +
    ((0 until 3) == List(0, 1)))
```

`tests/conformance/17-collections/range-equality-is-not-eager.scala`:
```scala
// EXPECT: false true
// The length check short-circuits, so comparing a billion-element Range against a
// one-element List answers in O(1) and allocates nothing. Under a low heap this
// fixture would fail outright if the implementation materialised the Range.
@main def run(): Unit =
  println(((0 until 1000000000) == List(1)).toString + " " +
    ((0 until 1000000000) == (0 until 1000000000)))
```

Register `range-equality-is-not-eager.scala` with `PROTOCORE_HEAP_LIMIT_CELLS=20000` in the unfiltered sweep (it needs no pin — that is the point).

`tests/conformance/17-collections/range-bound-must-be-small.scala`:
```scala
// EXPECT-ERROR: a Range bound must fit a 54-bit integer
@main def run(): Unit =
  val big = 9007199254740991L + 10
  println((0 until big).length)
```

Verify `range-basics`, `range-by-step`, `range-for-comprehension` and `range-tostring` against scalac. (`Range 0 until 5` is scalac 3.9's `toString`; confirm it rather than assuming.)

- [ ] **Step 2: Pin the prototype and the keys**

In `src/runtime/Runtime.h` add the five fields under **Interfaces**. In `src/runtime/Runtime.cpp`, beside the Phase 5 prototypes:

```cpp
    layout_.rangeProto = const_cast<proto::ProtoObject*>(
        layout_.anyRefProto->newChild(root, /*isMutable=*/true));
    pin(layout_.rangeProto);
    layout_.rangeProto->setAttribute(root, layout_.nameKey, makeString(root, "Range"));
    layout_.rangeProto->setAttribute(root, proto::ProtoString::createSymbol(root, "@Range"),
                                     PROTO_TRUE);
    layout_.rangeStartKey = proto::ProtoString::createSymbol(root, "__start__");
    layout_.rangeEndKey = proto::ProtoString::createSymbol(root, "__end__");
    layout_.rangeStepKey = proto::ProtoString::createSymbol(root, "__step__");
    layout_.rangeInclusiveKey = proto::ProtoString::createSymbol(root, "__inclusive__");
```

(`pin` is the existing helper that stores a prototype in a root-context slot; follow the surrounding code's spelling exactly.)

- [ ] **Step 3: Write the `Range` natives**

`src/runtime/CollectionPrimitives.cpp` starts:

```cpp
/*
 * CollectionPrimitives — Vector, Range, Map and Set (DESIGN §6, §6.1).
 *
 * Each is an ordinary protoCore object whose payload is one attribute
 * (plan A0-4): Vector holds a ProtoList under __vec__, Range holds four
 * SmallIntegers, Map and Set hold a ProtoMap under __map__. Map and Set are
 * read and written only through protoCore's hashed-collection helper with the
 * single KeySemantics of scalaKeySemantics() (PROTOMAP-SPEC §4).
 */
#include "runtime/PrimitiveSupport.h"
#include "compiler/GlobalTable.h"

namespace protoScala {
namespace {

using namespace protoScala::prim;

// ---------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------

struct RangeView {
    long long start, end, step;
    bool inclusive;
    // The element count, saturating at 0. O(1): a Range never materialises.
    long long length() const {
        if (step == 0) return 0;
        const long long last = inclusive ? end : (step > 0 ? end - 1 : end + 1);
        if (step > 0 ? last < start : last > start) return 0;
        return (last - start) / step + 1;
    }
    long long at(long long i) const { return start + i * step; }
};

RangeView viewOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* r) {
    RangeView v{};
    v.start = proto::asSmallInt(r->getAttribute(ctx, L.rangeStartKey));
    v.end = proto::asSmallInt(r->getAttribute(ctx, L.rangeEndKey));
    v.step = proto::asSmallInt(r->getAttribute(ctx, L.rangeStepKey));
    v.inclusive = r->getAttribute(ctx, L.rangeInclusiveKey) == PROTO_TRUE;
    return v;
}

long long rangeBound(ProtoContext* ctx, const ProtoObject* v) {
    if (!proto::isSmallInt(v))
        throw ScalaError("IllegalArgumentException",
                         "a Range bound must fit a 54-bit integer");
    return proto::asSmallInt(v);
}

const ProtoObject* newRange(ProtoContext* ctx, const RuntimeLayout& L, long long start,
                            long long end, long long step, bool inclusive) {
    proto::ProtoContext scope(ctx->space, ctx);
    auto* r = const_cast<ProtoObject*>(L.rangeProto->newChild(&scope, /*isMutable=*/false));
    scope.returnValue = r;
    r = const_cast<ProtoObject*>(r->setAttribute(&scope, L.rangeStartKey, proto::makeSmallInt(start)));
    scope.returnValue = r;
    r = const_cast<ProtoObject*>(r->setAttribute(&scope, L.rangeEndKey, proto::makeSmallInt(end)));
    scope.returnValue = r;
    r = const_cast<ProtoObject*>(r->setAttribute(&scope, L.rangeStepKey, proto::makeSmallInt(step)));
    scope.returnValue = r;
    r = const_cast<ProtoObject*>(
        r->setAttribute(&scope, L.rangeInclusiveKey, boolean(inclusive)));
    scope.returnValue = r;
    return r;
}
```

Immutable instances are rebuilt attribute by attribute (D28), so every intermediate object is re-rooted in `scope.returnValue` before the next `setAttribute` allocates.

Then the methods themselves. `to`, `until` and `by` go on `intProto`:

```cpp
PRIM(prim_int_until) {
    const RuntimeLayout& L = layoutOf();
    const long long a = rangeBound(ctx, self);
    const long long b = rangeBound(ctx, arg(ctx, args, 0, "until", 1));
    return newRange(ctx, L, a, b, 1, /*inclusive=*/false);
}

PRIM(prim_int_to) {
    const RuntimeLayout& L = layoutOf();
    const long long a = rangeBound(ctx, self);
    const long long b = rangeBound(ctx, arg(ctx, args, 0, "to", 1));
    return newRange(ctx, L, a, b, 1, /*inclusive=*/true);
}

PRIM(prim_range_by) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const long long s = rangeBound(ctx, arg(ctx, args, 0, "by", 1));
    if (s == 0) throw ScalaError("IllegalArgumentException", "a Range step cannot be zero");
    return newRange(ctx, L, v.start, v.end, s, v.inclusive);
}

PRIM(prim_range_length) {
    return proto::makeSmallInt(viewOf(ctx, layoutOf(), self).length());
}

PRIM(prim_range_apply) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const long long i = intArg(ctx, arg(ctx, args, 0, "apply", 1), "apply");
    if (i < 0 || i >= v.length())
        throw ScalaError("IndexOutOfBoundsException", std::to_string(i));
    return proto::makeSmallInt(v.at(i));
}

PRIM(prim_range_contains) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* x = arg(ctx, args, 0, "contains", 1);
    if (!proto::isSmallInt(x)) return PROTO_FALSE;
    const long long n = proto::asSmallInt(x);
    const long long len = v.length();
    if (len == 0) return PROTO_FALSE;
    const long long delta = n - v.start;
    if (delta % v.step != 0) return PROTO_FALSE;
    const long long i = delta / v.step;
    return boolean(i >= 0 && i < len);
}

PRIM(prim_range_toList) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* out = scope.newList(0, nullptr);
    scope.returnValue = out->asObject(&scope);
    for (long long i = 0; i < n; ++i) {
        out = out->appendLast(&scope, proto::makeSmallInt(v.at(i)));
        scope.returnValue = out->asObject(&scope);
    }
    return out->asObject(&scope);
}

PRIM(prim_range_foreach) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        (void)engine->invoke(&scope, f, slot, 1);
    }
    return L.unit;
}
```

Write `prim_range_map`, `prim_range_flatMap`, `prim_range_filter`, `prim_range_withFilter`, `prim_range_exists`, `prim_range_forall`, `prim_range_find`, `prim_range_sum`, `prim_range_isEmpty`, `prim_range_nonEmpty`, `prim_range_head`, `prim_range_last`, `prim_range_reverse`, `prim_range_toVector`, `prim_range_toSet` and `prim_range_mkString` in the same shape. `map`/`flatMap`/`filter` return a `List` (a `Range` is not closed under them), which matches Scala's `IndexedSeq` result closely enough and is recorded as **D68**: `Range.map` returns a `List`, where Scala returns an `IndexedSeq`; `(0 until 3).map(_ * 2).toString` is `List(0, 2, 4)`, not `Vector(0, 2, 4)`.

`toString`:

```cpp
PRIM(prim_range_toString) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    std::string s = "Range " + std::to_string(v.start) +
                    (v.inclusive ? " to " : " until ") + std::to_string(v.end);
    if (v.step != 1) s += " by " + std::to_string(v.step);
    return str(ctx, s);
}
```

- [ ] **Step 3a: Implement `SeqView` and wire cross-kind `Seq` equality and hashing**

In `src/runtime/Values.cpp`:

```cpp
SeqView seqViewOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    SeqView out;
    if (isListFast(v)) {
        out.list = v->asList(ctx);
        out.size = static_cast<long long>(out.list->getSize(ctx));
        out.valid = true;
        return out;
    }
    // Task 10 adds the Vector branch here, replacing the vectorListOf helper.
    if (isRangeFast(ctx, L, v)) {
        out.start = proto::asSmallInt(v->getAttribute(ctx, L.rangeStartKey));
        out.step = proto::asSmallInt(v->getAttribute(ctx, L.rangeStepKey));
        const long long end = proto::asSmallInt(v->getAttribute(ctx, L.rangeEndKey));
        const bool inclusive = v->getAttribute(ctx, L.rangeInclusiveKey) == PROTO_TRUE;
        const long long last = inclusive ? end : (out.step > 0 ? end - 1 : end + 1);
        out.size = (out.step == 0 || (out.step > 0 ? last < out.start : last > out.start))
                       ? 0
                       : (last - out.start) / out.step + 1;
        out.isRange = true;
        out.valid = true;
        return out;
    }
    return out;   // not a Seq
}

const proto::ProtoObject* seqElemAt(proto::ProtoContext* ctx, const SeqView& s, long long i) {
    // A Range is arithmetic, so nothing is allocated and nothing is built.
    return s.isRange ? proto::makeSmallInt(s.start + i * s.step)
                     : s.list->getAt(ctx, static_cast<int>(i));
}
```

and replace the list branch of `valuesEqual` with the view (this is the branch A0-7 introduced; the `vectorListOf` form it showed is superseded here):

```cpp
    // Scala Seq equality across kinds: List(1,2) == Vector(1,2) == (1 to 2)
    // (DESIGN §6, plan A0-7 and A0-8). Unwrapping here, before any prototype
    // dispatch, is what keeps the EQ opcode -- which never dispatches -- agreeing
    // with `a.equals(b)`.
    const SeqView sa = seqViewOf(ctx, L, a);
    const SeqView sb = seqViewOf(ctx, L, b);
    if (sa.valid && sb.valid) {
        if (sa.size != sb.size) return false;               // O(1) short-circuit
        if (sa.isRange && sb.isRange)                       // O(1) for two Ranges
            return sa.size == 0 || (sa.start == sb.start && sa.step == sb.step);
        for (long long i = 0; i < sa.size; ++i)
            if (!valuesEqual(ctx, L, seqElemAt(ctx, sa, i), seqElemAt(ctx, sb, i)))
                return false;
        return true;
    }
```

and make `scalaHash`'s list branch use the same view, so a `Range`, a `List` and a `Vector` of the same elements hash identically:

```cpp
    // Required, not optional: an ==/## split would make Map(List(0,1,2) -> 1)
    // miss a lookup by (0 until 3) and let Set hold duplicates (plan A0-8).
    const SeqView sv = seqViewOf(ctx, L, v);
    if (sv.valid) {
        std::int32_t h = 0x3c074a61;                 // the Phase 2 seqHash seed
        for (long long i = 0; i < sv.size; ++i)
            h = mixSeqElement(h, scalaHash(ctx, L, seqElemAt(ctx, sv, i)));
        return finalizeSeqHash(h, sv.size);
    }
```

`mixSeqElement`/`finalizeSeqHash` are whatever the existing `List` branch already uses (D39 fixed the algorithm in Phase 2) — **read that code and reuse it verbatim** rather than inventing a second mixer, or `List` hash codes will change and Phase 2's fixtures will fail.

- [ ] **Step 4: Install them and make `Range` a type**

At the end of `CollectionPrimitives.cpp`:

```cpp
} // namespace

void installCollectionPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    static constexpr MethodEntry ranges[] = {
        {"by", &prim_range_by}, {"length", &prim_range_length}, {"size", &prim_range_length},
        {"apply", &prim_range_apply}, {"contains", &prim_range_contains},
        {"toList", &prim_range_toList}, {"toVector", &prim_range_toVector},
        {"toSet", &prim_range_toSet}, {"foreach", &prim_range_foreach},
        {"map", &prim_range_map}, {"flatMap", &prim_range_flatMap},
        {"filter", &prim_range_filter}, {"withFilter", &prim_range_withFilter},
        {"exists", &prim_range_exists}, {"forall", &prim_range_forall},
        {"find", &prim_range_find}, {"sum", &prim_range_sum},
        {"isEmpty", &prim_range_isEmpty}, {"nonEmpty", &prim_range_nonEmpty},
        {"head", &prim_range_head}, {"last", &prim_range_last},
        {"reverse", &prim_range_reverse}, {"mkString", &prim_range_mkString},
        {"toString", &prim_range_toString},
    };
    installAll(ctx, L.rangeProto, ranges);
    static constexpr MethodEntry rangeOps[] = {{"until", &prim_int_until}, {"to", &prim_int_to}};
    installAll(ctx, L.intProto, rangeOps);
}
} // namespace protoScala
```

`installAll` and `MethodEntry` move from the anonymous namespace of `Primitives.cpp` into `PrimitiveSupport.h` so both files share one definition (the header already exists for exactly this reason).

Add `Range` to `builtinTypes()` in `src/compiler/ClassInfo.cpp`, add `isRangeFast` to `Values.cpp` (`isObjectCellFast(v) && v->getAttribute(ctx, L.rangeStartKey) != nullptr`), and teach `typeName` to answer `"Range"` and `show` to call the object's own `toString` (which it already does for any Scala instance — confirm with the `range-tostring` fixture rather than adding a branch).

- [ ] **Step 5: Unit tests**

`tests/unit/test_collections.cpp` (new; add to `tests/unit/CMakeLists.txt`):

```cpp
#include "EvalHarness.h"
#include <gtest/gtest.h>

using namespace protoScala;

TEST(Range, LengthIsConstantTime) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(0 until 1000000).length"), "1000000");
    EXPECT_EQ(h.eval("(0 until 10 by 3).length"), "4");
    EXPECT_EQ(h.eval("(10 to 1 by -3).length"), "4");
    EXPECT_EQ(h.eval("(5 until 5).length"), "0");
    EXPECT_EQ(h.eval("(5 to 5).length"), "1");
}

TEST(Range, IndexingAndContains) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(0 until 10 by 3)(2)"), "6");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(6)"), "true");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(7)"), "false");
    EXPECT_EQ(h.eval("(0 until 10 by 3).contains(12)"), "false");
}

TEST(Range, ABoundOutsideTheSmallIntegerRangeIsRefused) {
    EvalHarness h;
    EXPECT_EQ(h.eval("0 until (9007199254740991L + 10)"),
              "error: IllegalArgumentException: a Range bound must fit a 54-bit integer");
}
```

**Done when:** `ctest --test-dir build_release -R '17-collections/range|Range' --output-on-failure` passes all nine fixtures and three unit cases; `range-basics`, `range-by-step`, `range-for-comprehension`, `range-tostring` and **`range-equals-list-and-vector`** produce output identical to scalac's; `range-equality-is-not-eager` passes inside the unfiltered `PROTOCORE_HEAP_LIMIT_CELLS=20000` sweep with no pin of its own, which is the proof the comparison allocates nothing; and Phase 2's `List` hash fixtures still pass (Step 3a reuses the existing mixer rather than replacing it).

---

### Task 7: The `List` surface, part 1 — accessors and transforms

**Files:**
- Modify: `src/runtime/Primitives.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`
- Test: `tests/conformance/17-collections/list-*.scala` (new), `tests/unit/test_collections.cpp` (modify)

**Interfaces:** no new type. `listProto`'s `MethodEntry` array (`Primitives.cpp:908-913`) grows with, exactly: `last`, `init`, `take`, `takeWhile`, `dropWhile`, `splitAt`, `reverse`, `++`, `:+`, `+:`, `updated`, `contains`, `indexOf`, `exists`, `forall`, `find`, `count`, `partition`, `headOption`, `lastOption`, `zip`, `zipWithIndex`, `distinct`, `flatten`, `toList`, `toVector`, `toSet`, `iterator`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/17-collections/list-accessors.scala`:
```scala
// EXPECT: 1 5 List(1, 2, 3, 4) List(5, 4, 3, 2, 1) Some(1) None
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.head.toString + " " + xs.last + " " + xs.init + " " + xs.reverse + " " +
    xs.headOption + " " + List().headOption)
```

`tests/conformance/17-collections/list-slicing.scala`:
```scala
// EXPECT: List(1, 2) List(3, 4, 5) List(1, 2) List(3, 4, 5) (List(1, 2),List(3, 4, 5))
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.take(2).toString + " " + xs.drop(2) + " " + xs.takeWhile(_ < 3) + " " +
    xs.dropWhile(_ < 3) + " " + xs.splitAt(2))
```

`tests/conformance/17-collections/list-concat-and-update.scala`:
```scala
// EXPECT: List(1, 2, 3, 4) List(1, 2, 3) List(0, 1, 2) List(1, 9, 3)
@main def run(): Unit =
  val xs = List(1, 2)
  println((xs ++ List(3, 4)).toString + " " + (xs :+ 3) + " " + (0 +: xs) + " " +
    List(1, 2, 3).updated(1, 9))
```

`tests/conformance/17-collections/list-predicates.scala`:
```scala
// EXPECT: true false true 2 Some(4) 1 (List(2, 4),List(1, 3, 5))
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.contains(3).toString + " " + xs.forall(_ > 1) + " " + xs.exists(_ > 4) + " " +
    xs.count(_ % 2 == 0) + " " + xs.find(_ > 3) + " " + xs.indexOf(2) + " " +
    xs.partition(_ % 2 == 0))
```

`tests/conformance/17-collections/list-zip-and-distinct.scala`:
```scala
// EXPECT: List((1,a), (2,b)) List((a,0), (b,1)) List(1, 2, 3) List(1, 2, 3, 4)
@main def run(): Unit =
  println(List(1, 2, 3).zip(List("a", "b")).toString + " " +
    List("a", "b").zipWithIndex + " " + List(1, 2, 2, 3, 1).distinct + " " +
    List(List(1, 2), List(3), List(4)).flatten)
```

`tests/conformance/17-collections/list-index-errors.scala`:
```scala
// EXPECT-ERROR: IndexOutOfBoundsException
@main def run(): Unit =
  println(List(1, 2, 3)(5))
```

Add `-braces` twins for any fixture whose body uses indentation-only syntax (all of the above are single-expression `@main` bodies, so write one `-braces` twin of `list-predicates` as the representative pair the Phase 1 directories use). Verify all six against scalac — `zip` truncating to the shorter list and `splitAt` printing `(List(1, 2),List(3, 4, 5))` with no space after the comma are exactly the details worth pinning.

- [ ] **Step 2: Write the natives**

In `src/runtime/Primitives.cpp`, beside the existing list natives. A representative pair, written in full:

```cpp
PRIM(prim_list_splitAt) {
    const RuntimeLayout& L = layoutOf();
    const proto::ProtoList* xs = self->asList(ctx);
    const long long n = intArg(ctx, arg(ctx, args, 0, "splitAt", 1), "splitAt");
    const auto size = static_cast<long long>(xs->getSize(ctx));
    const long long k = n < 0 ? 0 : (n > size ? size : n);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = xs->getSlice(&scope, 0, static_cast<int>(k))->asObject(&scope);
    slot[1] = xs->getSlice(&scope, static_cast<int>(k), static_cast<int>(size))->asObject(&scope);
    // A Scala tuple is a Tuple2 case-class instance, never a ProtoTuple (§4.6).
    return activeCallContext()->engine->construct(&scope, L.tupleCompanion[2], slot, 2);
}

PRIM(prim_list_zipWithIndex) {
    const RuntimeLayout& L = layoutOf();
    const proto::ProtoList* xs = self->asList(ctx);
    const auto n = static_cast<long long>(xs->getSize(ctx));
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(3);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList(0, nullptr)->asObject(&scope);       // the accumulator, rooted
    for (long long i = 0; i < n; ++i) {
        slot[1] = xs->getAt(&scope, static_cast<int>(i));
        slot[2] = proto::makeSmallInt(i);
        const ProtoObject* pair = engine->construct(&scope, L.tupleCompanion[2], slot + 1, 2);
        slot[1] = pair;
        slot[0] = slot[0]->asList(&scope)->appendLast(&scope, slot[1])->asObject(&scope);
    }
    return slot[0];
}
```

`construct(ctx, cls, args, argc)` is `ExecutionEngine`'s existing "new cls(args) through the primary constructor" entry point (`ExecutionEngine.h:63-65`), which `Product.copy` already uses; `L.tupleCompanion[2]` is the `Tuple2` companion pinned by `Runtime`. Write `last`, `init`, `take`, `takeWhile`, `dropWhile`, `reverse`, `++`, `:+`, `+:`, `updated`, `contains`, `indexOf`, `exists`, `forall`, `find`, `count`, `partition`, `headOption`, `lastOption`, `zip`, `distinct`, `flatten`, `toList`, `iterator` in the same shape. `headOption`/`lastOption`/`find` build their `Option` through `L.hooks.someCompanion` / `L.hooks.noneValue`, exactly as the Phase 5 natives do. `toVector` and `toSet` are written in Tasks 10 and 9 and are added to the array then.

- [ ] **Step 3: Extend the installation table**

Replace `lists[]` in `Primitives.cpp:908-913` with the full array; keep the existing entries in place and append the new ones so the diff reads as an addition.

- [ ] **Step 4: Unit tests**

Add to `tests/unit/test_collections.cpp`:

```cpp
TEST(ListSurface, SliceOperationsClampRatherThanThrow) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).take(99)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("List(1, 2, 3).take(-1)"), "List()");
    EXPECT_EQ(h.eval("List(1, 2, 3).drop(99)"), "List()");
    EXPECT_EQ(h.eval("List[Int]().splitAt(2)"), "(List(),List())");
}

TEST(ListSurface, ZipTruncatesToTheShorterSide) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).zip(List(\"a\"))"), "List((1,a))");
    EXPECT_EQ(h.eval("List[Int]().zip(List(1))"), "List()");
}

TEST(ListSurface, IndexingOutOfRangeThrows) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3)(5)"), "error: IndexOutOfBoundsException: 5");
}
```

**Done when:** `ctest --test-dir build_release -R '17-collections/list|ListSurface' --output-on-failure` passes all seven fixtures and three unit cases; each of the six scalac-verifiable fixtures produces byte-identical output under `tools/scala3-3.9.0`; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 8: The `List` surface, part 2 — folds, sorting, grouping, conversions

**Files:**
- Modify: `src/runtime/Primitives.cpp`
- Test: `tests/conformance/17-collections/list-fold-*.scala`, `list-sort-*.scala`, `list-group-*.scala` (new), `tests/unit/test_collections.cpp` (modify)

**Interfaces:** `listProto`'s array grows with `foldLeft`, `foldRight`, `reduce`, `reduceLeft`, `reduceRight`, `sum`, `product`, `min`, `max`, `minBy`, `maxBy`, `sorted`, `sortBy`, `sortWith`, `groupBy`, `mkString` (0, 1 and 3 arguments), `toMap`. `toMap` and `groupBy` are wired to `Map` in Task 9 and are added to the array there; write everything else here.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/17-collections/list-folds.scala`:
```scala
// EXPECT: 15 15 120 -3 1 5 15
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.foldLeft(0)(_ + _).toString + " " + xs.sum + " " + xs.product + " " +
    xs.foldLeft(0)(_ - _) + " " + xs.min + " " + xs.max + " " + xs.reduce(_ + _))
```

Check `xs.foldLeft(0)(_ - _)`: `((((0-1)-2)-3)-4)-5 = -15`, not `-3`. Write the fixture with `-15` and confirm against scalac before committing the `// EXPECT:` line — **do not guess an arithmetic result; run scalac.**

`tests/conformance/17-collections/list-foldright.scala`:
```scala
// EXPECT: 3 List(1, 2, 3)
@main def run(): Unit =
  val xs = List(1, 2, 3)
  println(xs.foldRight(0)((x, acc) => acc + 1).toString + " " +
    xs.foldRight(List[Int]())((x, acc) => x :: acc))
```

`tests/conformance/17-collections/list-sorting.scala`:
```scala
// EXPECT: List(1, 2, 3, 5) List(5, 3, 2, 1) List(a, bb, ccc) List(ccc, bb, a)
@main def run(): Unit =
  val xs = List(3, 1, 5, 2)
  val ws = List("bb", "ccc", "a")
  println(xs.sorted.toString + " " + xs.sortWith(_ > _) + " " + ws.sortBy(_.length) + " " +
    ws.sortWith(_.length > _.length))
```

`tests/conformance/17-collections/list-sorting-is-stable.scala`:
```scala
// EXPECT: List((1,a), (1,b), (2,c))
case class P(k: Int, s: String)
@main def run(): Unit =
  val xs = List(P(1, "a"), P(2, "c"), P(1, "b"))
  println(xs.sortBy(_.k).map(p => (p.k, p.s)))
```

`tests/conformance/17-collections/list-mkstring.scala`:
```scala
// EXPECT: 123 1-2-3 [1, 2, 3]
@main def run(): Unit =
  val xs = List(1, 2, 3)
  println(xs.mkString + " " + xs.mkString("-") + " " + xs.mkString("[", ", ", "]"))
```

`tests/conformance/17-collections/list-sorted-needs-comparable.scala`:
```scala
// EXPECT-ERROR: sorted needs comparable elements; use sortWith
class Opaque(val n: Int)
@main def run(): Unit =
  println(List(new Opaque(1), new Opaque(2)).sorted)
```

Verify the first five against scalac.

- [ ] **Step 2: Write the folds**

```cpp
PRIM(prim_list_foldLeft) {
    const proto::ProtoList* xs = self->asList(ctx);
    // foldLeft(z)(f): two parameter lists, so the VM calls this native with the
    // seed and then applies the result to f. Phase 1's currying compiles that
    // into two sends, so this native sees exactly one argument here and the
    // function in the second call; the simplest correct shape is a single
    // native taking both, which is what `xs.foldLeft(z)(f)` desugars to once
    // SEND_APPLY has applied the first result. Take (z, f) in one call.
    const ProtoObject* z = arg(ctx, args, 0, "foldLeft", 2);
    const ProtoObject* f = args->getAt(ctx, 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const auto n = static_cast<long long>(xs->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = z;
    for (long long i = 0; i < n; ++i) {
        slot[1] = xs->getAt(&scope, static_cast<int>(i));
        slot[0] = engine->invoke(&scope, f, slot, 2);
    }
    return slot[0];
}
```

`foldLeft` in Scala has two parameter lists (`foldLeft(z)(op)`), which a native cannot express: a native receives one argument list. Resolve it in the prelude instead — a two-parameter-list Scala method that delegates to a one-list native:

```scala
// In lib/prelude.scala, after the Try block:
// Scala's foldLeft/foldRight take two parameter lists; a native takes one, so
// the curried surface lives here and the work stays native.
extension_placeholder_not_used
```

That does not work either, because extensions are Phase 4 and `List` is a builtin prototype, not a prelude class. **Decision, recorded here and carried into Task 0 as A0-16:** `foldLeft`, `foldRight`, `fold` and `Option.fold` take **one** parameter list in protoScala — `xs.foldLeft(0, _ + _)` — and the two-list Scala spelling `xs.foldLeft(0)(_ + _)` also works because Phase 2's `SEND_APPLY` applies the result of the first list to the second when the member's value is a function (D10). Implement the native so that:

- called with two arguments it folds directly;
- called with one argument it returns a one-argument closure object (`foldPartialProto`, the same one-shot applier pattern `Actor.spawn` uses) that folds when applied.

Write it exactly that way:

```cpp
// xs.foldLeft(z, f) folds directly; xs.foldLeft(z)(f) gets a one-shot applier
// whose `apply` finishes the fold (the Actor.spawn currying pattern).
PRIM(prim_list_foldLeft) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long n = argCount(ctx, args);
    if (n == 1) return makeFoldPartial(ctx, L, self, args->getAt(ctx, 0), /*left=*/true);
    if (n != 2) wrongArgCount("foldLeft", "1 or 2", n);
    return foldLeftImpl(ctx, L, self->asList(ctx), args->getAt(ctx, 0), args->getAt(ctx, 1));
}
```

with `makeFoldPartial` building an object whose prototype is a new `RuntimeLayout::foldPartialProto` carrying `__list__`-free keys `__fold_src__`, `__fold_seed__` and `__fold_left__`, and whose `apply` native calls `foldLeftImpl`/`foldRightImpl`. Add the three keys and the prototype to `RuntimeLayout` in this step.

- [ ] **Step 3: Write the sort**

```cpp
// Stable merge sort over a slot array of a dedicated context: the comparator may
// re-enter the VM and allocate, so no element may live in a std::vector across
// it (P1). Indices are plain unsigned and never hold objects.
const ProtoObject* sortList(ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoList* xs,
                            const ProtoObject* keyFn, const ProtoObject* lessFn) {
    const auto n = static_cast<unsigned>(xs->getSize(ctx));
    if (n < 2) return xs->asObject(ctx);
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(3u * n + 2u);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const ProtoObject** elems = slot;            // [0, n)      the elements
    const ProtoObject** keys = slot + n;         // [n, 2n)     keyFn's results, or the elements
    const ProtoObject** scratch = slot + 2u * n; // [2n, 3n)    the merge buffer
    for (unsigned i = 0; i < n; ++i) elems[i] = xs->getAt(&scope, static_cast<int>(i));
    for (unsigned i = 0; i < n; ++i) {
        keys[i] = keyFn ? engine->invoke(&scope, keyFn, &elems[i], 1) : elems[i];
    }
    std::vector<unsigned> idx(n), buf(n);        // indices only: no ProtoObject* in std::
    for (unsigned i = 0; i < n; ++i) idx[i] = i;
    auto before = [&](unsigned a, unsigned b) -> bool {
        if (lessFn) {
            const ProtoObject* pair[2] = {elems[a], elems[b]};
            slot[3u * n] = pair[0];
            slot[3u * n + 1] = pair[1];
            return engine->invoke(&scope, lessFn, slot + 3u * n, 2) == PROTO_TRUE;
        }
        try {
            return keys[a]->compare(&scope, keys[b]) < 0;
        } catch (const std::runtime_error&) {
            throw ScalaError("IllegalArgumentException",
                             "sorted needs comparable elements; use sortWith");
        }
    };
    for (unsigned width = 1; width < n; width *= 2) {
        for (unsigned lo = 0; lo < n; lo += 2 * width) {
            const unsigned mid = lo + width < n ? lo + width : n;
            const unsigned hi = lo + 2 * width < n ? lo + 2 * width : n;
            unsigned i = lo, j = mid, k = lo;
            while (i < mid && j < hi) buf[k++] = before(idx[j], idx[i]) ? idx[j++] : idx[i++];
            while (i < mid) buf[k++] = idx[i++];
            while (j < hi) buf[k++] = idx[j++];
            for (unsigned t = lo; t < hi; ++t) idx[t] = buf[t];
        }
    }
    for (unsigned i = 0; i < n; ++i) scratch[i] = elems[idx[i]];
    return scope.newList(n, scratch)->asObject(&scope);
}
```

`before(idx[j], idx[i])` — strictly-less on the *right* element — is what makes the merge stable: an equal pair keeps the left run's order, as Scala's `sorted` does. `sorted` calls `sortList(ctx, L, xs, nullptr, nullptr)`; `sortBy(f)` calls `sortList(ctx, L, xs, f, nullptr)`; `sortWith(lt)` calls `sortList(ctx, L, xs, nullptr, lt)`.

- [ ] **Step 4: `mkString` with 0, 1 and 3 arguments**

```cpp
PRIM(prim_list_mkString) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long n = argCount(ctx, args);
    std::string start, sep, end;
    if (n == 1) sep = stringArg(ctx, args->getAt(ctx, 0), "mkString");
    else if (n == 3) {
        start = stringArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = stringArg(ctx, args->getAt(ctx, 1), "mkString");
        end = stringArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (n != 0) wrongArgCount("mkString", "0, 1 or 3", n);
    const proto::ProtoList* xs = self->asList(ctx);
    const auto count = static_cast<long long>(xs->getSize(ctx));
    std::string out = start;
    for (long long i = 0; i < count; ++i) {
        if (i > 0) out += sep;
        out += show(ctx, L, xs->getAt(ctx, static_cast<int>(i)));
    }
    return str(ctx, out + end);
}
```

This replaces the existing `mkString`; keep its name in the table.

- [ ] **Step 5: Unit tests**

```cpp
TEST(ListSurface, FoldWorksCurriedAndUncurried) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3).foldLeft(0)(_ + _)"), "6");
    EXPECT_EQ(h.eval("List(1, 2, 3).foldLeft(0, (a: Int, b: Int) => a + b)"), "6");
    EXPECT_EQ(h.eval("List(1, 2, 3).foldRight(0)((x, acc) => x - acc)"), "2");
    EXPECT_EQ(h.eval("List[Int]().foldLeft(7)(_ + _)"), "7");
}

TEST(ListSurface, SortIsStable) {
    EvalHarness h;
    h.eval("case class P(k: Int, s: String)");
    EXPECT_EQ(h.eval("List(P(1, \"a\"), P(2, \"c\"), P(1, \"b\")).sortBy(_.k).map(_.s)"),
              "List(a, b, c)");
}

TEST(ListSurface, SortedRefusesIncomparableElements) {
    EvalHarness h;
    h.eval("class Opaque(val n: Int)");
    EXPECT_EQ(h.eval("List(new Opaque(1), new Opaque(2)).sorted"),
              "error: IllegalArgumentException: sorted needs comparable elements; use sortWith");
}
```

**Done when:** `ctest --test-dir build_release -R '17-collections/list|ListSurface' --output-on-failure` passes every fixture and unit case of Tasks 7 and 8; the five scalac-verifiable fixtures match byte for byte; and `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release` is green — the sort allocates `3n + 2` slots in one context, which under the low ceiling is the check that it does not leak per element.

---

### Task 9: `Map` and `Set` on `ProtoMap`

**Files:**
- Modify: `src/runtime/CollectionPrimitives.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Values.h`, `src/runtime/Values.cpp`, `src/runtime/Primitives.cpp`, `src/compiler/ClassInfo.cpp`
- Test: `tests/conformance/18-maps-and-sets/*.scala` (new), `tests/unit/test_collections.cpp` (modify), `tests/cli/gc-pressure.sh` (modify)

**Interfaces:**
- `RuntimeLayout` gains:
```cpp
    proto::ProtoObject* mapProto = nullptr;        // Map (DESIGN §6.1)
    proto::ProtoObject* setProto = nullptr;        // Set
    proto::ProtoObject* mapCompanion = nullptr;    // the value of the global `Map`
    proto::ProtoObject* setCompanion = nullptr;    // the value of the global `Set`
    const proto::ProtoString* mapDataKey = nullptr;  // "__map__": the ProtoMap
```
- `src/runtime/CollectionPrimitives.cpp` exposes, inside its anonymous namespace, the single semantics object every `Map`/`Set` operation passes to protoCore:
```cpp
// protoScala's key semantics for protoCore's hashed-collection helper
// (PROTOMAP-SPEC §4). isIdentityKey implements DESIGN §6.1's identity/value
// classification as specified (plan A0-5), with Char and parameterised enum cases
// on the value path per the maintainer's C1 and C2 rulings of 2026-09-23. hash
// and equals are protoScala's own scalaHash/valuesEqual, so a Map can never
// disagree with ==.
const proto::KeySemantics& scalaKeySemantics();
```
- `src/runtime/Values.h` already declares `isMapFast` and `isSetFast` (Task 6 Step 1 declared the whole family); this task interns `mapDataKey`, so they start answering `true`. `isSetFast` distinguishes a `Set` from a `Map` by the object's `__name__`, because both carry `__map__`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/18-maps-and-sets/map-basics.scala`:
```scala
// EXPECT: a 3 true false Some(a) None
@main def run(): Unit =
  val m = Map(1 -> "a", 2 -> "b", 3 -> "c")
  println(m(1) + " " + m.size + " " + m.contains(2) + " " + m.contains(9) + " " +
    m.get(1) + " " + m.get(9))
```

`tests/conformance/18-maps-and-sets/map-updates.scala`:
```scala
// EXPECT: 2 1 z a
@main def run(): Unit =
  val m = Map(1 -> "a")
  val m2 = m + (2 -> "b")
  val m3 = m2 - 2
  val m4 = m.updated(1, "z")
  println(m2.size.toString + " " + m3.size + " " + m4(1) + " " + m(1))
```

`tests/conformance/18-maps-and-sets/map-cooperative-numeric-keys.scala`:
```scala
// EXPECT: a a a 1
// Scala's cooperative numeric equality makes 1, 1L and 1.0 the same key.
@main def run(): Unit =
  val m = Map(1 -> "a")
  println(m(1) + " " + m(1L) + " " + m(1.0) + " " + (m + (1.0 -> "a")).size)
```

`tests/conformance/18-maps-and-sets/map-case-class-keys.scala`:
```scala
// EXPECT: hit hit 1
case class K(a: Int, b: String)
@main def run(): Unit =
  val m = Map(K(1, "x") -> "hit")
  println(m(K(1, "x")) + " " + m.getOrElse(K(1, "x"), "miss") + " " +
    (m + (K(1, "x") -> "hit")).size)
```

`tests/conformance/18-maps-and-sets/map-missing-key.scala`:
```scala
// EXPECT-ERROR: NoSuchElementException: key not found: 9
@main def run(): Unit =
  println(Map(1 -> "a")(9))
```

`tests/conformance/18-maps-and-sets/map-iteration-is-sorted-in-fixtures.scala`:
```scala
// EXPECT: List((1,a), (2,b), (3,c)) List(1, 2, 3) List(a, b, c)
// Map iteration order is unspecified (D7, D58), so a fixture always sorts.
@main def run(): Unit =
  val m = Map(3 -> "c", 1 -> "a", 2 -> "b")
  println(m.toList.sortBy(_._1).toString + " " + m.keys.toList.sorted + " " +
    m.values.toList.sorted)
```

`tests/conformance/18-maps-and-sets/map-combinators.scala`:
```scala
// EXPECT: List((1,2), (2,4)) List((2,b)) 2 true
@main def run(): Unit =
  val m = Map(1 -> 1, 2 -> 2)
  val s = Map(1 -> "a", 2 -> "b")
  println(m.map((k, v) => (k, v * 2)).toList.sortBy(_._1).toString + " " +
    s.filter((k, v) => k == 2).toList + " " + m.size + " " + m.forall((k, v) => v > 0))
```

`tests/conformance/18-maps-and-sets/set-basics.scala`:
```scala
// EXPECT: 3 true false List(1, 2, 3) 3
@main def run(): Unit =
  val s = Set(1, 2, 3, 2, 1)
  println(s.size.toString + " " + s.contains(2) + " " + s(9) + " " + s.toList.sorted + " " +
    (s + 2).size)
```

`tests/conformance/18-maps-and-sets/set-algebra.scala`:
```scala
// EXPECT: List(1, 2, 3, 4) List(2, 3) List(1) true false
@main def run(): Unit =
  val a = Set(1, 2, 3)
  val b = Set(2, 3, 4)
  println((a union b).toList.sorted.toString + " " + (a intersect b).toList.sorted + " " +
    (a diff b).toList.sorted + " " + Set(1, 2).subsetOf(a) + " " + b.subsetOf(a))
```

`tests/conformance/18-maps-and-sets/list-to-map-and-groupby.scala`:
```scala
// EXPECT: List((1,a), (2,b)) List((0,List(2, 4)), (1,List(1, 3)))
@main def run(): Unit =
  val pairs = List((1, "a"), (2, "b"))
  val xs = List(1, 2, 3, 4)
  println(pairs.toMap.toList.sortBy(_._1).toString + " " +
    xs.groupBy(_ % 2).toList.sortBy(_._1))
```

`tests/conformance/18-maps-and-sets/map-identity-keys-are-distinct.scala`:
```scala
// EXPECT: 2 miss a
// DESIGN §6.1 bullet 1: a class with the DEFAULT equals is an identity key, so
// two structurally identical instances are two different keys. If the
// classification wrongly sent this key down the value path, `size` would print
// 1 and the third field would print `a` instead of `miss` -- a LOUD failure, not
// a quiet one (plan A0-5).
class Plain(val n: Int)
@main def run(): Unit =
  val k1 = new Plain(1)
  val k2 = new Plain(1)
  val m = Map(k1 -> "a", k2 -> "b")
  println(m.size.toString + " " + m.getOrElse(new Plain(1), "miss") + " " + m(k1))
```

`tests/conformance/18-maps-and-sets/map-overriding-equals-switches-the-classification.scala`:
```scala
// EXPECT: 2 miss 1 hit
// The same program, twice, differing only by `override def equals`/`hashCode`.
// Overriding moves the key from bullet 1 to bullet 2, so the second half must
// find a freshly built equal key where the first half must not. A classification
// that ignored the override would print `1 hit` twice; one that ignored the
// default would print `2 miss` twice. Either way a number changes.
class Plain(val n: Int)
class Structural(val n: Int):
  override def equals(o: Any): Boolean =
    o.isInstanceOf[Structural] && o.asInstanceOf[Structural].n == n
  override def hashCode: Int = n
@main def run(): Unit =
  val p = Map(new Plain(1) -> "x", new Plain(1) -> "y")
  val s = Map(new Structural(1) -> "x", new Structural(1) -> "y")
  println(p.size.toString + " " + p.getOrElse(new Plain(1), "miss") + " " +
    s.size + " " + (if s.getOrElse(new Structural(1), "miss") == "miss" then "miss" else "hit"))
```

`tests/conformance/18-maps-and-sets/map-classification-round-trip.scala`:
```scala
// EXPECT: 9 9 ok
// One key of every kind DESIGN §6.1 classifies, inserted and then read back. A
// key whose classification is wrong vanishes from the map, so `size` and the
// read-back count disagree and the last field prints BAD. This is the fixture
// that turns a silent misclassification into a visible failure.
class Plain(val n: Int)
case class Pair(a: Int, b: String)
object Marker
@main def run(): Unit =
  val plain = new Plain(1)
  val keys: List[Any] =
    List(plain, Marker, true, 'a', 7, "s", Pair(1, "x"), (1, 2), List(1, 2))
  var m = Map[Any, Int]()
  var i = 0
  while i < keys.length do
    m = m + (keys(i) -> i)
    i += 1
  var found = 0
  var j = 0
  while j < keys.length do
    if m.getOrElse(keys(j), -1) == j then found += 1
    j += 1
  println(m.size.toString + " " + found + " " +
    (if m.size == keys.length && found == keys.length then "ok" else "BAD"))
```

(The nine kinds are: a plain class instance and an `object` on the identity path; `true`, a `Char`, an `Int`, a `String`, a case class, a tuple and a `List` on the value path. `'a'` and `7` are distinct keys here because `'a'`'s code point is 97, not 7 — the fixture deliberately avoids the C1 collision so that a collision *would* be a failure.)

`tests/conformance/18-maps-and-sets/map-value-keys-are-structural.scala`:
```scala
// EXPECT: a b c d
// DESIGN §6.1 bullet 2: a freshly built, structurally equal key must hit for
// every value-equality kind. A kind wrongly classified as identity prints its
// fallback word instead.
case class Pair(a: Int, b: String)
@main def run(): Unit =
  val m = Map[Any, String](Pair(1, "x") -> "a", (1, 2) -> "b", List(1, 2) -> "c", "s" -> "d")
  println(m.getOrElse(Pair(1, "x"), "MISS") + " " + m.getOrElse((1, 2), "MISS") + " " +
    m.getOrElse(List(1, 2), "MISS") + " " + m.getOrElse("s", "MISS"))
```

`tests/conformance/18-maps-and-sets/map-equals-without-hashcode-misses.scala`:
```scala
// EXPECT: 2 miss
// NOT a divergence: a class that overrides equals but not hashCode is a value
// key that hashes by identity, so two == instances land in different slots and
// never find each other. Scala behaves identically (the classic bug). Recorded
// here so the behaviour is pinned rather than rediscovered as a defect.
class Broken(val n: Int):
  override def equals(o: Any): Boolean =
    o.isInstanceOf[Broken] && o.asInstanceOf[Broken].n == n
@main def run(): Unit =
  val m = Map(new Broken(1) -> "x", new Broken(1) -> "y")
  println(m.size.toString + " " + m.getOrElse(new Broken(1), "miss"))
```

`tests/conformance/18-maps-and-sets/map-enum-case-keys.scala`:
```scala
// EXPECT: warm 2
// A singleton enum case is a case object, so DESIGN §6.1 bullet 1 makes it an
// identity key and `Color.Red` is the same key every time (A0-5 C2, reading (i)).
// Requires Phase 4's `enum`; if Phase 4 has not landed, ship this as
// `// XFAIL: requires enum (Phase 4)` and convert it when Phase 4 does.
enum Color:
  case Red, Blue
@main def run(): Unit =
  val m = Map(Color.Red -> "warm", Color.Blue -> "cool")
  println(m(Color.Red) + " " + m.size)
```

`tests/conformance/18-maps-and-sets/map-parameterised-enum-case-keys.scala`:
```scala
// EXPECT: a 1
// A0-5 ruling C2 (2026-09-23): a parameterised enum case IS a case class -- "es
// el usuario que esta haciendo la conversion" -- so it is a value key and two
// separately built Leaf(1)s are one key, which is Scala's answer. Under the old
// literal reading of "enum cases" as identity keys this would print `miss 1`.
// This fixture needs Phase 4's `enum`: while Phase 4 has not landed, ship it with
// `// XFAIL: requires enum (Phase 4)` as its first line instead and convert it
// when Phase 4 does -- the EXPECT line above is already the right one.
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
@main def run(): Unit =
  val m = Map(Tree.Leaf(1) -> "a")
  println(m.getOrElse(Tree.Leaf(1), "miss") + " " + m.size)
```

`tests/conformance/18-maps-and-sets/map-char-and-int-keys.scala`:
```scala
// EXPECT: 1 1
// A0-5 ruling C1 (2026-09-23): a Char follows Scala. Its `==` is not `eq` --
// 'a' == 97 is true and 'a'.## is 97 -- so it is a value key and 'a' and 97 are
// ONE key. Under the old reading ('a' an identity key, 97 a value key) this
// would print `2 1`. Verify against tools/scala3-3.9.0 before committing.
@main def run(): Unit =
  val m = Map[Any, Int]('a' -> 1, 97 -> 1)
  println(m.size.toString + " " + m.getOrElse(97, -1))
```

`tests/conformance/18-maps-and-sets/map-char-key-round-trip.scala`:
```scala
// EXPECT: 1 1 1 true
// The C1 ruling in both directions: inserted as a Char, found as an Int and as a
// Char; inserted as an Int, found as a Char. A Char left on the identity path
// would make one of these print -1 or false.
@main def run(): Unit =
  val byChar = Map[Any, Int]('a' -> 1)
  val byInt = Map[Any, Int](97 -> 1)
  println(byChar.getOrElse('a', -1).toString + " " + byChar.getOrElse(97, -1) + " " +
    byInt.getOrElse('a', -1) + " " + (Set[Any]('a', 97).size == 1))
```

`tests/conformance/18-maps-and-sets/set-classification-mirrors-map.scala`:
```scala
// EXPECT: 2 1 true false
// §6.1's third bullet: Set uses the same scheme with the element as the entry,
// so a Plain (identity) element duplicates and a Pair (value) element does not.
class Plain(val n: Int)
case class Pair(a: Int, b: String)
@main def run(): Unit =
  val p = Set(new Plain(1), new Plain(1))
  val q = Set(Pair(1, "x"), Pair(1, "x"))
  println(p.size.toString + " " + q.size + " " + q.contains(Pair(1, "x")) + " " +
    p.contains(new Plain(1)))
```

Add `-braces` twins for `map-case-class-keys` and `list-to-map-and-groupby`. Verify `map-basics`, `map-updates`, `map-cooperative-numeric-keys`, `map-case-class-keys`, `map-iteration-is-sorted-in-fixtures`, `set-basics`, `set-algebra`, `list-to-map-and-groupby`, `map-identity-keys-are-distinct`, `map-overriding-equals-switches-the-classification`, `map-value-keys-are-structural`, `map-equals-without-hashcode-misses` and `set-classification-mirrors-map` against scalac — **the classification fixtures especially**, because scalac is the only independent check that §6.1's reading matches Scala's behaviour. `map-missing-key` pins scalac's message text exactly (`key not found: 9`). `map-char-and-int-keys`, `map-char-key-round-trip` and `map-parameterised-enum-case-keys` carry the two A0-5 rulings; **verify all three against scalac**, because they exist precisely to prove protoScala now answers what Scala answers, and a plan-written expectation would defeat their purpose.

- [ ] **Step 2: Write the key semantics**

In `src/runtime/CollectionPrimitives.cpp`:

```cpp
// ---------------------------------------------------------------------------
// Map and Set
// ---------------------------------------------------------------------------

// The helper's callbacks get a bare ProtoContext, so the layout comes from the
// thread's active call context (which a Map operation always runs inside).

// DESIGN §6.1, bullet 1: "objects whose == is eq (instances of classes with the
// default equals, objects, case objects, enum cases, symbols, booleans, chars)".
// Bullet 2: "numbers, strings, case classes (including tuples), collections, and
// classes that override equals".
//
// The one judgement call — "does this class override equals?" — is decided by an
// EXACT POINTER COMPARISON against the single method object installed on
// anyProto (Primitives.cpp:869, any_equals), pinned in the layout as
// defaultEqualsMethod. A case class resolves to product_equals
// (ProductPrimitives.cpp:277) and a user override to their own compiled method,
// so both fall out of the same comparison with no special case and no heuristic.
// Getting this wrong is silently wrong, which is why Step 1's fixtures are built
// to fail loudly on a misclassification (plan A0-5).
bool scalaIsIdentityKey(ProtoContext* ctx, const ProtoObject* key) {
    const RuntimeLayout& L = *activeCallContext()->layout;

    // --- bullet 2 first: every value-equality kind, so a later fallthrough
    //     can default to identity without swallowing one of them.
    if (isNumberFast(key)) return false;                       // numbers
    if (proto::ProtoObject::isStringTagFast(key)) return false; // strings
    if (isListFast(key)) return false;                          // collections
    if (isVectorFast(ctx, L, key) || isMapFast(ctx, L, key) ||
        isSetFast(ctx, L, key) || isRangeFast(ctx, L, key))
        return false;                                           // collections

    // --- Char is the one special case (maintainer ruling C1, 2026-09-23:
    //     "si Char es un caso especial, deberia procesarse especial en el codigo
    //     de la funcion. Tratar en lo posible seguir Scala"). A Char is a unique
    //     embedded word, so identity LOOKS right -- but a Char's == is not eq:
    //     Scala's cooperative equality makes 'a' == 97 true and 'a'.## == 97, so
    //     Map('a' -> 1) must be found by a lookup of 97. Sending it down the
    //     value path does that for free, because scalaHash already hashes a Char
    //     to its code point and valuesEqual already widens a Char to it.
    if (isCharFast(key)) return false;

    // --- bullet 1: unique-word immediates. `==` on each of these IS word
    //     identity, which is bullet 1's own criterion.
    if (key == PROTO_TRUE || key == PROTO_FALSE) return true;   // booleans
    if (key == PROTO_NONE || key == L.unit) return true;        // null, ()

    // --- an instance: identity exactly when its equals is the default one.
    //     A case class (product_equals), a tuple (likewise) and a class with
    //     `override def equals` all resolve to something else, so they are
    //     value keys, which is bullet 2.
    if (isScalaInstance(ctx, L, key)) {
        const ProtoObject* eq = key->getAttribute(ctx, L.equalsName);
        return eq == nullptr || eq == L.defaultEqualsMethod;
    }

    // Function values, lazy holders, actors, futures, WithFilter: valuesEqual
    // falls back to identity for all of them, so bullet 1's criterion applies.
    return true;
}

unsigned long scalaKeyHash(ProtoContext* ctx, const ProtoObject* key) {
    // Called only for value-equality keys. scalaHash is Scala's ##: cooperative
    // across Int/Long/Double, String by content, case classes structurally.
    return static_cast<unsigned long>(
        static_cast<long long>(scalaHash(ctx, *activeCallContext()->layout, key)));
}

bool scalaKeyEquals(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b) {
    // Called only inside a collision bucket, on value-equality keys.
    return valuesEqual(ctx, *activeCallContext()->layout, a, b);
}

const proto::KeySemantics& scalaKeySemantics() {
    static const proto::KeySemantics semantics{&scalaIsIdentityKey, &scalaKeyHash, &scalaKeyEquals};
    return semantics;
}
```

`RuntimeLayout` gains the pinned method this depends on. In `src/runtime/Runtime.h`, next to `equalsName`:

```cpp
    // The one `equals` installed on anyProto (Primitives.cpp:869). A class that
    // does not override `equals` resolves to exactly this object, which is how
    // scalaIsIdentityKey decides DESIGN §6.1's classification without a
    // heuristic. Filled by installPrimitives, not by Runtime::Runtime, because
    // the method object does not exist until then.
    const proto::ProtoObject* defaultEqualsMethod = nullptr;
```

and in `installPrimitives`, immediately after `installAll(ctx, L.anyProto, any)`:

```cpp
    const_cast<RuntimeLayout&>(L).defaultEqualsMethod =
        L.anyProto->getOwnAttributeDirect(ctx, L.equalsName);
    if (!L.defaultEqualsMethod)
        throw std::logic_error("installPrimitives: anyProto has no equals");
```

A `static const` of a POD struct of three function pointers holds no `ProtoObject*`, so it is not the forbidden symbol cache.

- [ ] **Step 3: Write the `Map` natives**

```cpp
const proto::ProtoMap* mapDataOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* m) {
    const ProtoObject* d = m->getAttribute(ctx, L.mapDataKey);
    if (!d) throw ScalaError("ClassCastException", "not a Map");
    return d->asMap(ctx);
}

const ProtoObject* wrapMap(ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoMap* data,
                           bool isSet) {
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* payload = data->asObject(&scope);
    scope.returnValue = payload;
    auto* o = const_cast<ProtoObject*>(
        (isSet ? L.setProto : L.mapProto)->newChild(&scope, /*isMutable=*/false));
    scope.returnValue = o;
    o = const_cast<ProtoObject*>(o->setAttribute(&scope, L.mapDataKey, payload));
    scope.returnValue = o;
    return o;
}

PRIM(prim_map_apply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "apply", 1);
    const ProtoObject* v = proto::hashedGet(ctx, mapDataOf(ctx, L, self), scalaKeySemantics(), k);
    if (!v)   // nullptr, never PROTO_NONE: a stored null stays distinguishable
        throw ScalaError("NoSuchElementException", "key not found: " + show(ctx, L, k));
    return v;
}

PRIM(prim_map_get) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "get", 1);
    const ProtoObject* v = proto::hashedGet(ctx, mapDataOf(ctx, L, self), scalaKeySemantics(), k);
    if (!v) return L.hooks.noneValue;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = v;
    return activeCallContext()->engine->invoke(&scope, L.hooks.someCompanion, slot, 1);
}

PRIM(prim_map_plus) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* pair = arg(ctx, args, 0, "+", 1);
    // The argument is a Tuple2 case-class instance (§4.6), so read _1 and _2.
    const ProtoObject* k = pair->getAttribute(ctx, L.tupleFieldKey[1]);
    const ProtoObject* v = pair->getAttribute(ctx, L.tupleFieldKey[2]);
    if (!k || !v)
        throw ScalaError("IllegalArgumentException", "Map.+ expects a (key, value) pair, got " +
                                                         typeName(ctx, L, pair));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.returnValue = self;
    const proto::ProtoMap* out =
        proto::hashedPut(&scope, mapDataOf(&scope, L, self), scalaKeySemantics(), k, v);
    return wrapMap(&scope, L, out, /*isSet=*/false);
}
```

Write `updated(k, v)`, `-`/`removed`, `++`, `getOrElse`, `contains`, `isDefinedAt`, `size`, `isEmpty`, `nonEmpty`, `keys`, `keySet`, `values`, `toList`, `toSeq`, `toMap`, `foreach`, `map`, `flatMap`, `filter`, `withFilter`, `foldLeft`, `exists`, `forall`, `find`, `count`, `head`, `mkString`, `equals`, `hashCode` and `toString` in the same shape. Iteration uses `proto::hashedForEach` with a `void*` self pointing at a struct holding a `ProtoContext*` and a slot index — never a `std::vector<const ProtoObject*>`:

```cpp
struct Collector {
    proto::ProtoContext* scope;
    const ProtoObject** acc;    // one slot, re-rooted after every append
    const RuntimeLayout* L;
    bool pairs;                  // true: build Tuple2(k, v); false: just the key
};

void collectEntry(ProtoContext* ctx, void* self, const ProtoObject* k, const ProtoObject* v) {
    auto* c = static_cast<Collector*>(self);
    const ProtoObject* item = k;
    if (c->pairs) {
        const ProtoObject* kv[2] = {k, v};
        item = activeCallContext()->engine->construct(ctx, c->L->tupleCompanion[2], kv, 2);
    }
    c->acc[0] = c->acc[0]->asList(ctx)->appendLast(ctx, item)->asObject(ctx);
}
```

The two-element `kv` array lives only across `construct`, which roots its arguments itself (it is the same contract `Product.copy` relies on).

`toString` renders `Map(1 -> a, 2 -> b)` and `Set(1, 2)` using `show` on each key and value.

- [ ] **Step 4: Write the `Set` natives and the companions**

`Set` reuses `mapDataOf`/`wrapMap` with the element as both key and value (`hashedPut(ctx, data, sem, e, e)`), which keeps one code path and lets `hashedForEach` yield the element directly. Write `apply` (= `contains`), `contains`, `+`, `-`, `++`, `--`, `union`/`|`, `intersect`/`&`, `diff`/`&~`, `subsetOf`, `size`, `isEmpty`, `nonEmpty`, `toList`, `toSeq`, `toSet`, `foreach`, `map`, `flatMap`, `filter`, `withFilter`, `exists`, `forall`, `find`, `count`, `mkString`, `equals`, `hashCode`, `toString`.

The companions are mutable objects in the globals, exactly as `List`/`Nil`/`Actor` are:

```cpp
PRIM(prim_map_companion_apply) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoMap* data = scope.newMap();
    const unsigned long n = argCount(&scope, args);
    for (unsigned long i = 0; i < n; ++i) {
        const ProtoObject* pair = args->getAt(&scope, static_cast<int>(i));
        const ProtoObject* k = pair->getAttribute(&scope, L.tupleFieldKey[1]);
        const ProtoObject* v = pair->getAttribute(&scope, L.tupleFieldKey[2]);
        if (!k || !v)
            throw ScalaError("IllegalArgumentException",
                             "Map(...) expects (key, value) pairs, got " + typeName(&scope, L, pair));
        data = proto::hashedPut(&scope, data, scalaKeySemantics(), k, v);
        scope.returnValue = data->asObject(&scope);   // rooted across the next hashedPut
    }
    return wrapMap(&scope, L, data, /*isSet=*/false);
}
```

Add `"Map"` and `"Set"` to `builtinGlobalNames()` and bind the companions in `installPrimitives` the way `listCompanion` is bound.

- [ ] **Step 5: `->` on `Any`**

`Map(1 -> "a")` needs `->`. Add to the `any[]` table in `Primitives.cpp`:

```cpp
// Scala's ArrowAssoc: `k -> v` is the Tuple2 (k, v). A case-class instance,
// never a ProtoTuple (§4.6).
PRIM(prim_any_arrow) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = self;
    slot[1] = arg(&scope, args, 0, "->", 1);
    return activeCallContext()->engine->construct(&scope, L.tupleCompanion[2], slot, 2);
}
```

`->` has letter precedence in Scala (it is `-` plus `>`, so first-character precedence puts it with `+`/`-`); confirm `Map(1 -> "a", 2 -> "b")` parses as three arguments and not as `Map(1 -> ("a", 2) -> "b")` with the `map-basics` fixture before moving on.

- [ ] **Step 6: Equality, hashing and `show`**

In `src/runtime/Values.cpp`, extend `valuesEqual` so two `Map`s (or two `Set`s) of the same size compare by looking every key of one up in the other with `hashedGet` and comparing the values with `valuesEqual`; a `Map` and a `Set` are never equal. Extend `scalaHash` so a `Map`/`Set` hashes order-independently: XOR-fold `scalaHash(k) * 41 + scalaHash(v)` over the entries (a commutative fold, so hash order cannot leak). Extend `typeName` to answer `"Map"`/`"Set"`.

- [ ] **Step 7: GC pressure**

Append to `tests/cli/gc-pressure.sh`:

```bash
# Collections under a low heap: 20000 entries whose only reference is the Map,
# then every key is read back. A key the collector reclaimed shows up as a
# NoSuchElementException, which is the whole point of ProtoMap's traced keys.
cat > "$TMP/map-pressure.scala" <<'EOF'
// EXPECT: 20000 ok
@main def run(): Unit =
  var m = Map[String, Int]()
  var i = 0
  while i < 20000 do
    m = m + (("k" + i) -> i)
    i += 1
  var seen = 0
  var j = 0
  while j < 20000 do
    if m(("k" + j)) == j then seen += 1
    j += 1
  println(seen.toString + " " + (if seen == 20000 then "ok" else "BAD"))
EOF
out=$(PROTOCORE_HEAP_LIMIT_CELLS=400000 "$P" "$TMP/map-pressure.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: map pressure exited $rc: $out"; exit 1; }
[[ "$out" == "20000 ok" ]] || { echo "FAIL: map pressure printed '$out'"; exit 1; }
```

The keys are built by concatenation, so the only reference to each is the `ProtoMap` slot — precisely the scenario PROTOMAP-SPEC §5's GC-safety test was written for, now exercised from the language.

Add a second block for **identity** keys, which is the half `ProtoMap`'s traced-key guarantee exists for — a value key lives inside the entry list, an identity key *is* the slot key:

```bash
# Identity keys under a low heap: 20000 instances of a class with the default
# equals, referenced ONLY by the Map slot (DESIGN §6.1 bullet 1). If ProtoMap did
# not trace its keys, or if these were misclassified onto the hashed path and the
# instance were dropped, the read-back count would fall below 20000 and print BAD.
cat > "$TMP/map-identity-pressure.scala" <<'EOF'
// EXPECT: 20000 ok
class Key(val n: Int)
@main def run(): Unit =
  var m = Map[Key, Int]()
  var keep = List[Key]()
  var i = 0
  while i < 20000 do
    val k = new Key(i)
    m = m + (k -> i)
    keep = k :: keep
    i += 1
  var seen = 0
  var rest = keep
  while rest.nonEmpty do
    if m.getOrElse(rest.head, -1) == rest.head.n then seen += 1
    rest = rest.tail
  println(seen.toString + " " + (if seen == 20000 then "ok" else "BAD"))
EOF
out=$(PROTOCORE_HEAP_LIMIT_CELLS=600000 "$P" "$TMP/map-identity-pressure.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: identity-key pressure exited $rc: $out"; exit 1; }
[[ "$out" == "20000 ok" ]] || { echo "FAIL: identity-key pressure printed '$out'"; exit 1; }
```

- [ ] **Step 8: Unit tests**

```cpp
TEST(MapSurface, CooperativeNumericKeys) {
    EvalHarness h;
    h.eval("val m = Map(1 -> \"a\")");
    EXPECT_EQ(h.eval("m(1L)"), "a");
    EXPECT_EQ(h.eval("m(1.0)"), "a");
    EXPECT_EQ(h.eval("(m + (1.0 -> \"b\")).size"), "1");
    EXPECT_EQ(h.eval("(m + (1.0 -> \"b\"))(1)"), "b");
}

TEST(MapSurface, AStoredNullIsDistinguishableFromAbsence) {
    EvalHarness h;
    h.eval("val m = Map(1 -> null)");
    EXPECT_EQ(h.eval("m.contains(1)"), "true");
    EXPECT_EQ(h.eval("m.get(1)"), "Some(null)");
    EXPECT_EQ(h.eval("m.get(2)"), "None");
}

TEST(MapSurface, ForcedHashCollisionsStillCompareByEquals) {
    EvalHarness h;
    h.eval("class C(val n: Int) { override def hashCode: Int = 7; "
           "override def equals(o: Any): Boolean = o.isInstanceOf[C] && o.asInstanceOf[C].n == n }");
    h.eval("val m = Map(new C(1) -> \"a\", new C(2) -> \"b\")");
    EXPECT_EQ(h.eval("m.size"), "2");
    EXPECT_EQ(h.eval("m(new C(2))"), "b");
    EXPECT_EQ(h.eval("m.get(new C(3))"), "None");
}

TEST(MapSurface, TheClassificationIsDecidedByAnExactPointerComparison) {
    EvalHarness h;
    // DESIGN §6.1: a class with the default equals is an identity key; the same
    // class with `override def equals` is a value key. The decision is
    // defaultEqualsMethod == the resolved equals, so this pins the mechanism and
    // not just the outcome (plan A0-5).
    h.eval("class Plain(val n: Int)");
    h.eval("class Structural(val n: Int) { "
           "override def equals(o: Any): Boolean = "
           "o.isInstanceOf[Structural] && o.asInstanceOf[Structural].n == n; "
           "override def hashCode: Int = n }");
    EXPECT_EQ(h.eval("Map(new Plain(1) -> 1, new Plain(1) -> 2).size"), "2");
    EXPECT_EQ(h.eval("Map(new Structural(1) -> 1, new Structural(1) -> 2).size"), "1");
    EXPECT_EQ(h.eval("Map(new Plain(1) -> 1).getOrElse(new Plain(1), -1)"), "-1");
    EXPECT_EQ(h.eval("Map(new Structural(1) -> 1).getOrElse(new Structural(1), -1)"), "1");
}

TEST(MapSurface, CharAndIntAreOneKey) {
    EvalHarness h;
    // A0-5 ruling C1: a Char's == is not eq, so it is a value key and 'a' and 97
    // are one key, as in Scala. A Char left on the identity path makes the first
    // assertion print 2.
    EXPECT_EQ(h.eval("Map[Any, Int]('a' -> 1, 97 -> 2).size"), "1");
    EXPECT_EQ(h.eval("Map[Any, Int]('a' -> 1).getOrElse(97, -1)"), "1");
    EXPECT_EQ(h.eval("Map[Any, Int](97 -> 1).getOrElse('a', -1)"), "1");
    EXPECT_EQ(h.eval("Set[Any]('a', 97).size"), "1");
}

TEST(MapSurface, IdentityAndHashedSlotKeysCannotCollide) {
    // What makes §6.1's bullet 1 sound: a hashed slot key is a SmallInteger word
    // and an identity slot key never is. Asserted on the encodings directly,
    // rather than trusted (PROTOMAP-SPEC §4).
    EvalHarness h;
    proto::ProtoContext* ctx = h.runtime().rootContext();
    EXPECT_FALSE(proto::isSmallInt(PROTO_TRUE));
    EXPECT_FALSE(proto::isSmallInt(PROTO_FALSE));
    EXPECT_FALSE(proto::isSmallInt(PROTO_NONE));
    EXPECT_TRUE(proto::isSmallInt(proto::makeSmallInt(97)));
    // A Char is embedded type 2, so it would have been disjoint from a hashed
    // slot key too -- but after the C1 ruling it never appears as an identity
    // slot key at all, which this asserts through the classifier itself.
    EXPECT_FALSE(proto::isSmallInt(ctx->fromUnicodeChar('a')));
}

TEST(MapSurface, EveryClassifiedKindSurvivesARoundTrip) {
    EvalHarness h;
    // One key of every kind §6.1 names. A misclassified kind vanishes, so the
    // size and the read-back count disagree (plan A0-5's loud-failure rule).
    h.eval("class Plain(val n: Int)");
    h.eval("case class Pair(a: Int, b: String)");
    h.eval("object Marker");
    h.eval("val plain = new Plain(1)");
    h.eval("val keys: List[Any] = List(plain, Marker, true, 'a', 7, \"s\", "
           "Pair(1, \"x\"), (1, 2), List(1, 2))");
    h.eval("var m = Map[Any, Int]()");
    h.eval("var i = 0; while i < keys.length do { m = m + (keys(i) -> i); i += 1 }");
    EXPECT_EQ(h.eval("m.size"), "9");
    h.eval("var found = 0; var j = 0; "
           "while j < keys.length do { if m.getOrElse(keys(j), -1) == j then found += 1; j += 1 }");
    EXPECT_EQ(h.eval("found"), "9");
}

TEST(SetSurface, AlgebraAndSubset) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(Set(1, 2, 3) intersect Set(2, 3, 4)).toList.sorted"), "List(2, 3)");
    EXPECT_EQ(h.eval("(Set(1, 2) diff Set(2)).toList"), "List(1)");
    EXPECT_EQ(h.eval("Set(1).subsetOf(Set(1, 2))"), "true");
}
```

**Done when:** `ctest --test-dir build_release -R '18-maps-and-sets|MapSurface|SetSurface' --output-on-failure` passes all twelve fixtures and four unit cases; the eight scalac-verifiable fixtures match byte for byte; `bash tests/cli/gc-pressure.sh build_release/protoscala tests` prints `OK`; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 10: `Vector`

**Files:**
- Modify: `src/runtime/CollectionPrimitives.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `src/runtime/Values.h`, `src/runtime/Values.cpp`, `src/runtime/Primitives.cpp`, `src/compiler/ClassInfo.cpp`
- Test: `tests/conformance/17-collections/vector-*.scala` (new), `tests/unit/test_collections.cpp` (modify)

**Interfaces:**
- `RuntimeLayout` gains `proto::ProtoObject* vectorProto`, `proto::ProtoObject* vectorCompanion` and `const proto::ProtoString* vecDataKey = nullptr; // "__vec__"`.
- `src/runtime/Values.h` already declares `isVectorFast` and `SeqView`/`seqViewOf`/`seqElemAt` (Task 6 Step 1); this task interns `vecDataKey`, so `isVectorFast` starts answering `true`, and adds the `Vector` branch to `seqViewOf` — **no `vectorListOf` helper is introduced**, because `seqViewOf` already is that helper generalised (A0-8):
```cpp
    // In seqViewOf, between the List branch and the Range branch:
    if (isVectorFast(ctx, L, v)) {
        out.list = v->getAttribute(ctx, L.vecDataKey)->asList(ctx);
        out.size = static_cast<long long>(out.list->getSize(ctx));
        out.valid = true;
        return out;
    }
```

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/17-collections/vector-basics.scala`:
```scala
// EXPECT: Vector(1, 2, 3) 3 1 3 Vector(2, 4, 6) Vector(1, 3)
@main def run(): Unit =
  val v = Vector(1, 2, 3)
  println(v.toString + " " + v.length + " " + v(0) + " " + v.last + " " +
    v.map(_ * 2) + " " + v.filter(_ != 2))
```

`tests/conformance/17-collections/vector-append-and-update.scala`:
```scala
// EXPECT: Vector(1, 2, 3) Vector(0, 1, 2) Vector(1, 9) Vector(1, 2, 3, 4)
@main def run(): Unit =
  val v = Vector(1, 2)
  println((v :+ 3).toString + " " + (0 +: v) + " " + v.updated(1, 9) + " " + (v ++ Vector(3, 4)))
```

`tests/conformance/17-collections/vector-equals-list.scala`:
```scala
// EXPECT: true true true false
@main def run(): Unit =
  println((List(1, 2) == Vector(1, 2)).toString + " " + (Vector(1, 2) == List(1, 2)) + " " +
    (List(1, 2).## == Vector(1, 2).##) + " " + (Vector(1, 2) == Vector(2, 1)))
```

Scala 3 answers `true true true false` for exactly this program (`List` and `Vector` are both `Seq`s and `Seq` equality is element-wise); verify it against scalac before committing the directive.

`tests/conformance/17-collections/vector-conversions.scala`:
```scala
// EXPECT: List(1, 2, 3) Vector(1, 2, 3) Vector(0, 1, 2)
@main def run(): Unit =
  println(Vector(1, 2, 3).toList.toString + " " + List(1, 2, 3).toVector + " " +
    (0 until 3).toVector)
```

`tests/conformance/17-collections/vector-as-a-map-key.scala`:
```scala
// EXPECT: hit hit
@main def run(): Unit =
  val m = Map(Vector(1, 2) -> "hit")
  println(m(Vector(1, 2)) + " " + m(List(1, 2)))
```

`m(List(1, 2))` returning `"hit"` is the direct consequence of D59 and is the reason A0-7 puts the equality in one place. scalac answers the same, because `List(1,2) == Vector(1,2)` and their `hashCode`s agree — verify it.

- [ ] **Step 2: Write the natives**

Follow the `Range` shape. `Vector` wraps a `ProtoList` under `__vec__`; every method unwraps, delegates to the same code the `List` natives use, and re-wraps. Extract the shared bodies into free functions in `CollectionPrimitives.cpp` (`listMap`, `listFilter`, `listFoldLeft`, `listSortedImpl`, …) and let both `listProto`'s and `vectorProto`'s natives call them, so the two surfaces cannot drift. `Vector.map` returns a `Vector`, `List.map` returns a `List`; `Vector.toList` and `List.toVector` convert.

- [ ] **Step 3: One equality, one hash**

Task 6 Step 3a already put the single cross-kind comparison in `valuesEqual` and the single cross-kind hash in `scalaHash`, both over `SeqView`. This task adds **only** the `Vector` branch of `seqViewOf`, shown under **Interfaces**, and then verifies that nothing else was needed. The branch it replaces in the earlier draft of this plan — a `vectorListOf` unwrap written inline in `valuesEqual` — is superseded; do not add it. For reference, the comparison Task 6 installed is:

```cpp
    const SeqView sa = seqViewOf(ctx, L, a);
    const SeqView sb = seqViewOf(ctx, L, b);
    if (sa.valid && sb.valid) { /* Task 6 Step 3a */ }
```

Adding the `Vector` branch to `seqViewOf` therefore makes `List == Vector`, `Vector == Range` and `Vector.## == List.##` all work at once, with no second code path to keep in step.

- [ ] **Step 4: Unit tests**

```cpp
TEST(VectorSurface, SeqEqualityAndHashAgreeWithList) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2) == Vector(1, 2)"), "true");
    EXPECT_EQ(h.eval("Vector(1, 2) == List(1, 2)"), "true");
    EXPECT_EQ(h.eval("List(1, 2).## == Vector(1, 2).##"), "true");
    EXPECT_EQ(h.eval("Vector(1, 2).equals(List(1, 2))"), "true");
}

TEST(VectorSurface, VectorAndListAreDistinguishableTypes) {
    EvalHarness h;
    EXPECT_EQ(h.eval("Vector(1).isInstanceOf[Vector[Int]]"), "true");
    EXPECT_EQ(h.eval("List(1).isInstanceOf[Vector[Int]]"), "false");
    EXPECT_EQ(h.eval("Vector(1).toString"), "Vector(1)");
}
```

**Done when:** `ctest --test-dir build_release -R '17-collections/vector|VectorSurface' --output-on-failure` passes all five fixtures and two unit cases; the five scalac-verifiable fixtures match byte for byte; and the full suite is green under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

---

### Task 11: The `String` surface that the rest of the phase needs

**Files:**
- Modify: `src/runtime/Primitives.cpp`
- Test: `tests/conformance/16-strings/string-*.scala` (new), `tests/unit/test_primitives.cpp` (modify)

**Interfaces:** `stringProto`'s table (`Primitives.cpp:900-907`) grows with `split`, `replace`, `stripMargin`, `stripPrefix`, `stripSuffix`, `lastIndexOf`, `toList`, `toVector`, `toSet`, `head`, `last`, `init`, `take`, `drop`, `takeWhile`, `dropWhile`, `map`, `filter`, `foreach`, `mkString`, `compareTo`, `equalsIgnoreCase`, `capitalize`, `repeat`, `toBoolean`, `toLong`, `format`.

- [ ] **Step 1: Write the failing fixtures**

`tests/conformance/16-strings/string-split-and-strip.scala`:
```scala
// EXPECT: List(a, b, c) a.b.c hello world
@main def run(): Unit =
  val s = "a,b,c"
  val m = """|hello
             |world""".stripMargin
  println(s.split(",").toString + " " + s.replace(",", ".") + " " +
    m.split("\n").mkString(" "))
```

`tests/conformance/16-strings/string-char-operations.scala`:
```scala
// EXPECT: List(a, b, c) ABC 3 a c
@main def run(): Unit =
  val s = "abc"
  println(s.toList.toString + " " + s.map(_.toUpper).mkString + " " + s.length + " " +
    s.head + " " + s.last)
```

`tests/conformance/16-strings/string-prefix-suffix.scala`:
```scala
// EXPECT: world hello 0 true
@main def run(): Unit =
  val s = "helloworld"
  println(s.stripPrefix("hello") + " " + s.stripSuffix("world") + " " +
    "abc".compareTo("abc") + " " + "ABC".equalsIgnoreCase("abc"))
```

`tests/conformance/16-strings/string-format.scala`:
```scala
// EXPECT: n=007
@main def run(): Unit =
  println("n=%03d".format(7))
```

Verify all four against scalac. `"a,b,c".split(",")` returns an `Array[String]` in Scala and a `List[String]` here (D12's rule, extended) — record it as **D69** and make the fixture's `// EXPECT:` line the protoScala output, with a comment naming the divergence and scalac's `[Ljava.lang.String;@…` behaviour when printed directly.

- [ ] **Step 2: Write the natives**

`split` takes a **literal separator**, not a regular expression: protoScala has no regex engine and adding one is out of scope. Record as **D70**: `String.split` splits on a literal separator; `"a.b".split(".")` yields `List(a, b)` where Scala (which reads the argument as a regex) yields `List()`. An empty separator raises `IllegalArgumentException: String.split needs a non-empty separator`.

`stripMargin` takes an optional margin character (default `'|'`) and strips leading whitespace up to and including the first occurrence on each line. `format` delegates to the Task 4 formatter: parse the receiver as a printf string, walk the arguments, and reuse `formatOne`.

Every one of these uses `ProtoString::getSlice` and `getAt` rather than `toStdString`; the protoJS "String.prototype rope-flatten anti-pattern" lesson applies directly — flattening a rope to a `std::string` on every `charAt` is exactly the defect that lesson records. `split`, `replace` and `format` do need a linear walk, so they flatten once and say so in a comment.

- [ ] **Step 3: Unit tests**

```cpp
TEST(StringSurface, SplitIsLiteralNotRegex) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"a.b.c\".split(\".\")"), "List(a, b, c)");
    EXPECT_EQ(h.eval("\"abc\".split(\",\")"), "List(abc)");
    EXPECT_EQ(h.eval("\"\".split(\",\")"), "List()");
    EXPECT_EQ(h.eval("\"a,,b\".split(\",\")"), "List(a, , b)");
    EXPECT_EQ(h.eval("\"abc\".split(\"\")"),
              "error: IllegalArgumentException: String.split needs a non-empty separator");
}

TEST(StringSurface, StripMarginHandlesEveryLine) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"|a\\n  |b\".stripMargin"), "a\nb");
    EXPECT_EQ(h.eval("\"a\\nb\".stripMargin"), "a\nb");
}
```

**Done when:** `ctest --test-dir build_release -R '16-strings/string|StringSurface' --output-on-failure` passes all four fixtures and two unit cases; the four fixtures' non-divergent lines match scalac; and the full suite is green.

---

### Task 12: Benchmark suite v1

**Files:**
- New: `benchmarks/comparable/list_ops.scala`, `benchmarks/comparable/map_build.scala`, `benchmarks/comparable/python/list_ops.py`, `benchmarks/comparable/python/map_build.py`
- Modify: `benchmarks/run_benchmarks.py`, `benchmarks/RESULTS.md`, `benchmarks/README.md`, `tests/CMakeLists.txt`

**Interfaces:** `WORKLOADS` in `benchmarks/run_benchmarks.py` gains two entries with the exact shape of the existing ones (`name`, `scala`, `what`, `origin`, `py`, `st`, `clj`).

- [ ] **Step 1: Write `list_ops.scala`**

```scala
// EXPECT: 333283335000 100000 50000
// list_ops.scala - build a 100000-element List, then map / filter / fold it.
// The ROADMAP's benchmark suite v1 "list-ops" workload (Phase 3). It prints the
// fold result, the built length and the filtered length, so a silently wrong
// collection surface fails the run instead of reading as a fast one.

def build(n: Int): List[Int] =
  var xs = List[Int]()
  var i = n
  while i > 0 do
    i -= 1
    xs = i :: xs
  xs

@main def benchListOps(): Unit =
  val xs = build(100000)
  val doubled = xs.map(_ * 2)
  val evens = xs.filter(_ % 2 == 0)
  val total = doubled.foldLeft(0)(_ + _)
  println(total.toString + " " + xs.length + " " + evens.length)
```

Compute the expected total with scalac before writing the `// EXPECT:` line — **never from arithmetic done in this plan**:

```bash
SCALA_HOME=/home/gamarino/Documentos/proyectos/tools/scala3-3.9.0
sed '1d' benchmarks/comparable/list_ops.scala > ../.agent_scratch/phase3/list_ops.scala
"$SCALA_HOME/bin/scalac" -d ../.agent_scratch/phase3/out ../.agent_scratch/phase3/list_ops.scala
java -cp "$SCALA_HOME/lib/*:../.agent_scratch/phase3/out" benchListOps
```

- [ ] **Step 2: Write `map_build.scala`**

```scala
// EXPECT: 50000 1249975000 50000
// map_build.scala - build a 50000-entry Map keyed by String, read every key
// back, and fold the values. The ROADMAP's benchmark suite v1 "map-build"
// workload (Phase 3). It prints the entry count, the value fold and the number
// of keys read back successfully, so a Map that loses entries fails the run.

@main def benchMapBuild(): Unit =
  var m = Map[String, Int]()
  var i = 0
  while i < 50000 do
    m = m + (("k" + i) -> i)
    i += 1
  var total = 0
  var found = 0
  var j = 0
  while j < 50000 do
    val v = m.getOrElse("k" + j, -1)
    if v >= 0 then
      total += v
      found += 1
    j += 1
  println(m.size.toString + " " + total + " " + found)
```

Compute the expected values with scalac the same way.

- [ ] **Step 3: Write the Python twins**

`benchmarks/comparable/python/list_ops.py` and `map_build.py` run the same algorithm with the same N and print the same three numbers separated by single spaces, so the harness's `last_result` comparison is exact. Follow `benchmarks/comparable/python/sum_loop.py`'s shape.

- [ ] **Step 4: Register the workloads**

Append to `WORKLOADS` in `benchmarks/run_benchmarks.py`:

```python
    {"name": "list_ops", "scala": "list_ops.scala",
     "what": "map / filter / foldLeft over a 100000-element List", "origin": "protoScala",
     "py": (PY_TWINS_DIR / "list_ops.py", {}, "<the value scalac printed>"),
     "st": None,
     "clj": None},
    {"name": "map_build", "scala": "map_build.scala",
     "what": "build and read back a 50000-entry Map", "origin": "protoScala",
     "py": (PY_TWINS_DIR / "map_build.py", {}, "<the value scalac printed>"),
     "st": None,
     "clj": None},
```

Replace both `<the value scalac printed>` placeholders with the measured strings **in this step** — the plan leaves them unfilled deliberately because inventing a fold result would be exactly the silent-failure mode the self-reporting rule exists to prevent.

- [ ] **Step 5: Confirm the runner verifies, by breaking it on purpose**

```bash
cmake --build build_release
python3 benchmarks/run_benchmarks.py --name phase3-v1 --runs 5 --warmup 2
sed -i 's|^// EXPECT: |// EXPECT: 999 |' benchmarks/comparable/list_ops.scala
python3 benchmarks/run_benchmarks.py --name phase3-sabotage --runs 1 --warmup 0 2>&1 | grep -i 'list_ops'
git checkout -- benchmarks/comparable/list_ops.scala
```

Expected: the first run has no FAILED cell; the sabotaged run marks the `list_ops` protoScala cell FAILED with `printed … expected '999 …'` and contributes no number to the geometric mean. That is the proof the ROADMAP's "self-reports and is recorded" clause asks for.

- [ ] **Step 6: Record the table**

Add a `## Phase 3 workloads` section to `benchmarks/RESULTS.md` with the machine, date, commit, protoCore commit, load average at the time of the run, and the full table produced by Step 5's first run, plus a one-line note for `fib`, `tak` and `sum_loop` saying they are the suite-v1 members that already existed. Add the two new `.scala` files to the benchmark smoke-check glob in `tests/CMakeLists.txt` (they are run by `tests/conformance/run.sh` like the other `benchmarks/comparable/*.scala`), and pin `PROTOCORE_HEAP_LIMIT_CELLS=2000000` on `benchmarks/map_build.scala`'s CTest case the way `object_tree.scala` already is — 50 000 string keys plus their entries do not fit the 20 000-cell sweep ceiling and an honest out-of-memory there would read as a rooting defect.

**Done when:** `python3 benchmarks/run_benchmarks.py --name phase3-v1` produces a report with no FAILED cell and rows for `fib`, `tak`, `sum_loop`, `list_ops` and `map_build`; the sabotage run of Step 5 marks the cell FAILED; `benchmarks/RESULTS.md` carries the recorded table; and `ctest --test-dir build_release -R benchmarks --output-on-failure` is green.

---

### Task 13: Tutorial chapters 8 and 10

**Files:**
- New: `docs/tutorial/08-collections.md`, `docs/tutorial/10-strings-and-interpolation.md`
- Modify: `docs/TUTORIAL.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-scala-developer.md`
- New: `tests/conformance/tutorial/08-collections-*.scala`, `tests/conformance/tutorial/10-strings-*.scala`

**Interfaces:** every runnable snippet in the two new chapters is the body, verbatim, of a fixture `tests/conformance/tutorial/NN-<topic>-<name>.scala` whose `// EXPECT:` line is the output printed under the snippet in the chapter.

- [ ] **Step 1: Write chapter 8**

`docs/tutorial/08-collections.md` covers, in order: `List` (already known from chapter 7) and its full surface; `Vector` and when to prefer it; `Range` and `for i <- 0 until n`; `Map` and `Set`; `Option` as a collection of at most one; `Either` and `Try`; conversions (`toList`, `toVector`, `toSet`, `toMap`); and a closing "What differs from Scala 3 in this area" section listing D58, D59, D61, D62, D63, D65, D67 and D68 with one sentence each. **Do not list D57, D60 or D64**: they are unused — the `Char`-key and `Range`-equality divergences were removed by the rulings of 2026-09-23 and the `Try` one by by-name landing, so the chapter should instead say, in one sentence, that `Map` keys, `Seq` equality across `List`/`Vector`/`Range`, and `Try { … }` all behave as in Scala.

Write it for **both audiences**. For the Python/JavaScript reader: `List` is not Python's `list` (it is immutable and prepend-friendly, like a linked list), `Vector` is the closer analogue of a Python `list`/JavaScript `Array` for indexed access, `Map` is a `dict`/`Map`, `Set` is a `set`/`Set`, `Range` is `range()`, and "immutable" means `m + (k -> v)` returns a new map rather than changing `m` — with the explicit note that this is *cheap*, because the two maps share almost all of their structure. For the Scala reader: the departures list, and the fact that there is no `Ordering`, no `CanBuildFrom`/`IterableOnce` machinery and no `collect`.

- [ ] **Step 2: Write chapter 10**

`docs/tutorial/10-strings-and-interpolation.md` covers: string literals and triple-quoted strings (chapter 4 introduced them); `s"…"` with `$name` and `${expr}`; `raw"…"`; `f"…"` with the full A0-2 specifier table; `stripMargin`; the `String` methods of Task 11; and a closing departures section listing D54, D55, D56, D66, D69 and D70.

For the Python/JavaScript reader: `s"…"` is the f-string / template literal you already write, `${}` is the same bracket, `$name` is the short form Python 3.12 does not have, and `f"$x%.2f"` is `f"{x:.2f}"` / `x.toFixed(2)`. For the Scala reader: `s`, `f` and `raw` are all there; a custom interpolator needs extension methods (D56) and arrives with them.

- [ ] **Step 3: Turn every snippet into a fixture**

For each runnable snippet, create the matching fixture. For example, the chapter-8 snippet

```scala
val stock = Map("apples" -> 3, "pears" -> 0)
val restocked = stock + ("pears" -> 12)
println(restocked.toList.sortBy(_._1))
```

becomes `tests/conformance/tutorial/08-collections-map-is-immutable.scala`:

```scala
// EXPECT: List((apples,3), (pears,12))
@main def run(): Unit =
  val stock = Map("apples" -> 3, "pears" -> 0)
  val restocked = stock + ("pears" -> 12)
  println(restocked.toList.sortBy(_._1))
```

and the chapter prints `List((apples,3), (pears,12))` under it. Every chapter-8 and chapter-10 snippet that prints gets one; a snippet shown as failing gets an `// EXPECT-ERROR:` fixture quoting the message the binary actually prints.

- [ ] **Step 4: Update the chapter table and the two bridge chapters**

In `docs/TUTORIAL.md`, replace the `8 | Collections | *Planned* (Phase 3).` and `10 | Strings and interpolation | *Planned* (Phase 3).` rows with links and one-line contents, and update the "What runs today" blockquote to name protoScala 0.4.0 and the new surface. In `docs/tutorial/02-*.md`, add the Python/JavaScript bridge sections listed in Step 1 and Step 2. In `docs/tutorial/03-*.md`, add the new D-ids to the departures catalogue, each with the one-sentence explanation the chapter's existing entries use.

**Done when:** `ctest --test-dir build_release -R 'tutorial/08|tutorial/10' --output-on-failure` passes every new fixture; `grep -c '```scala' docs/tutorial/08-collections.md` equals the number of chapter-8 fixtures plus the number of deliberately non-runnable fragments (count them and state the number in the commit message); and `docs/TUTORIAL.md` has no `*Planned* (Phase 3)` row left.

---

### Task 14: Status, deviations, changelog and release 0.4.0

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

Expected: no warning, no error, `100% tests passed` four times, a test count to quote, and a cold-start figure. **Record the load average with the cold-start number** and, if it is above 2.0, say the budget is not claimed as met — the Phase 5 cold-start entry is the precedent and the honest form.

- [ ] **Step 2: `docs/STATUS.md`**

Update the "Current state" blockquote to 0.4.0 and the new test counts. Move the Phase 3 rows from "Not yet implemented" into "Implemented" with the surface each task delivered. Add the `CONCAT` row to the opcode table and change the `39..63` comment. Add a "Provisional deviations (Phase 3)" table with the ids actually used from D54–D70, each naming the plan item it answers or `—`, and note against each whether it was **ruled** by the maintainer or **decided on cost** (the Task 0 triage section carries the ruling or the cost line for every one; nothing is left pending). **Record that D57, D60 and D64 are unused, with one line each:** D57 because the `Char` ruling removed the divergence it described, D60 because `Range == List` now follows Scala, D64 because by-name parameters landed. Do **not** renumber to close the gaps — ids are stable references, and three short notes cost less than a renumber that invalidates every document already citing them. Add to "Known issues / platform dependencies": the one entry cell per value-equality key that DESIGN §6.1's scheme costs, the fact that `Map`/`Set` iteration is ascending-hash (D58) and that every fixture sorts, and the cold-start figure with its load average.

- [ ] **Step 3: `docs/LANGUAGE.md`**

Mark §2's `string interpolation s"" f"" raw""` row ✅ (Phase 3). Replace §4's "full target surface" bullet list with what is now delivered versus what is still ahead, and add a §4.2 listing the exact method surface of `List`, `Vector`, `Range`, `Map`, `Set`, `Option`, `Either` and `Try` as delivered. Add D54–D70 to §5 with the same wording as STATUS.

**Remove the `Seq`/`Iterable` promise (maintainer ruling, 2026-09-23).** §4's bullet list currently reads `… \`Range\`, \`Seq\`/\`Iterable\` as traits, \`Option\`/\`Some\`/\`None\` …`. Delete `\`Seq\`/\`Iterable\` as traits` from it and add one line under the list: "`Seq` and `Iterable` are **not** provided and are not scheduled: no phase's done-when contains them, `case xs: Seq[_]` is rejected at compile time (D65), and `List`, `Vector`, `Range`, `Map` and `Set` share no common ancestor." Also add `collect` to the same not-provided line (D63), since §4's "common collection methods" bullet does not list it but a reader will look for it. Nothing else in §4 promises a type no phase delivers — check the whole section rather than only that bullet, and report anything else found.

- [ ] **Step 4: `docs/ROADMAP.md`**

Mark Phase 3 ✅ with the date and 0.4.0, with a "Delivered beyond the criteria" line naming what this phase added that the done-when did not ask for (the `EQ`/`NE` fast path, the `unary_-` interning fix, `->` on `Any`, the `String` surface of Task 11). Check whether Phase 3's own line still says "requires P1" as if it were open; Phase P1's header was corrected to "✅ — merged and released in protoCore 2.0.0" on 2026-09-23, so only the Phase 3 line may need it. Add a one-line note under Phase 4 recording the ordering coupling of A0-13.

- [ ] **Step 5: `docs/DESIGN.md`**

**§6.1 was already edited during Task 0**, under the maintainer's C1 and C2 rulings of 2026-09-23: `Char` and parameterised `enum` cases moved to the value-equality bullet, bullet 1 now says *singleton* enum cases, two ruling notes were added, and the stale `XFAIL` sentence was replaced. Verify that edit survived (`grep -c 'maintainer ruling, 2026-09-23' docs/DESIGN.md` returns 2) and that A0-5's verbatim quote in this plan still matches §6.1 character for character — re-synchronise the quote if §6.1 moved again.

Two further sentences are stale and Task 14 corrects them, each with a dated note rather than a silent edit:

- **§6** — add a sentence recording A0-8: `List`, `Vector` and `Range` compare and hash through one allocation-free `SeqView`, so `List(1,2) == Vector(1,2) == (1 to 2)` and a `Map` keyed by one is found by another. §6 currently promises only `List(1,2) == Vector(1,2)`.
- §3.4's row `s"a $x b"` → `StringContext("a ", " b").s(x)` — amend it to record A0-1 and D54: `s` and `raw` lower to the `CONCAT` opcode and `f` to `__fmt`, and a user-defined `StringContext` is not consulted.

- [ ] **Step 6: `docs/DECISIONS-LOG.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt`**

Add one `[agent, pending review]` row per A0-n decision with the same shape the Phase 5 rows use (date, decision, taken by, where). Add a `## 0.4.0` section to `CHANGELOG.md` naming the protoCore commit the release was built against. Update `README.md`'s feature list and its "protoScala in 10 minutes" section with a collections and an interpolation example (each one a `tests/conformance/tutorial/` fixture). Bump `project(protoScala VERSION 0.4.0 …)`.

- [ ] **Step 7: Commit**

One commit per task is the norm; where two tasks are one code path (Tasks 2–4 are the interpolation pipeline; Tasks 7–8 are the `List` surface), a combined commit is acceptable and its message says why, as the Phase 5 log does for Tasks 3–7. **Do not create the `v0.4.0` tag** — the maintainer tags.

**Done when:** `ctest --test-dir build_release -N | tail -1` reports the new total, `100% tests passed` in all four configurations of Step 1, `build_release/protoscala --version` prints `0.4.0`, `grep -c 'D5[3-9]\|D6[0-9]' docs/STATUS.md docs/LANGUAGE.md` shows every new id in both files, and `git status --short` is clean after the commits.

---

## Task 0 triage: what needs the maintainer, and what was decided on cost

The maintainer's standing rule, 2026-09-23: *"En la medida que sea razonable, least surprise para el programador. Por otro lado, si alguien escribe usando esos detalles está jugando con fuego y no creo que sea tan común."* — **follow Scala where matching is cheap; where matching would cost real machinery and only unusual code could notice, document the divergence and move on.** Escalation is reserved for what genuinely needs a maintainer: a contradiction inside DESIGN or between the documents, a change to a public surface or a protoCore convention, anything touching the GC or the threading protocol, and anything where following Scala would cost machinery that a **common** usage depends on.

### Escalated — needs a maintainer ruling

**None.** Every item this plan raised has been ruled on or decided on cost. The two that were escalated — A0-5's C1 (`Char` keys) and C2 (parameterised `enum` cases) — were ruled on 2026-09-23, both toward Scala, and `docs/DESIGN.md` §6.1 was edited accordingly. The document contradiction A0-14 surfaced (LANGUAGE §4 promising `Seq`/`Iterable` that no phase delivers) was also ruled, in favour of ROADMAP, and Task 14 Step 3 corrects LANGUAGE. One item is **not** a ruling request but a roadmap note, recorded here so it is not mistaken for a semantics question: `Seq`/`Iterable` as real traits, and `collect`, are features a common Scala idiom uses, and if either is wanted it belongs in a later phase's done-when rather than in this one.

### Ruled by the maintainer (2026-09-23)

| # | Item | Ruling |
|---|---|---|
| R1 | `Map`/`Set` key classification: DESIGN §6.1, or hash everything? (A0-5) | **§6.1 stands.** Implemented as specified, decided by an exact pointer comparison against `defaultEqualsMethod`, with six fixtures built so a misclassification fails loudly |
| R2 | §6.1 C1: is a `Char` an identity key? (A0-5) | **No — follow Scala, and special-case it in the code.** A `Char`'s `==` is not `eq`, so it is a value key; `Map[Any,Int]('a' -> 1, 97 -> 1)` has one key. §6.1 edited; no deviation recorded, **D57 unused** |
| R3 | §6.1 C2: is a parameterised `enum` case an identity key? (A0-5) | **No — it is a case class, so a value key.** "Es el usuario que está haciendo la conversión." §6.1's bullet 1 now says *singleton* enum cases explicitly |
| R4 | `s"…"`/`raw"…"` → `CONCAT`, ignoring a user-defined `StringContext`? (A0-1) | **Stands** (D54). An implementation-strategy difference, not a semantic mismatch on the same input |
| R5 | Omit `collect` until partial functions exist? (A0-11) | **Confirmed** (D63). `PartialFunction` stays out of Phase 3 |
| R6 | LANGUAGE §4 promises `Seq`/`Iterable`; ROADMAP's done-when does not (A0-14) | **ROADMAP governs — not delivered** (D65), and Task 14 Step 3 deletes the promise from LANGUAGE §4 |

### Decided on cost, recorded for review

Each is implemented as stated, recorded in `docs/DECISIONS-LOG.md` as "agent, decided on cost", and carries the one-line cost justification the rule asks for. **None of these needs a reply**; any of them is reversible.

| # | Divergence or choice | Decision, and the cost that decided it |
|---|---|---|
| C1 | `(0 until 3) == List(0, 1, 2)` (A0-8) | **Follow Scala: `true`.** Cheap — an allocation-free `SeqView` that *replaces* the `vectorListOf` helper A0-7 needed anyway, ~58 lines across `valuesEqual` and `scalaHash`, O(1) length short-circuit, and the `EQ` opcode's integer fast path untouched. **D60 withdrawn** |
| C2 | `f"…"` conversion set: no `%n`, no locale, ASCII `,` grouping (A0-2) | **Keep the subset** (D55). Adding a locale means a locale database; `%n` is `\n`. Additive later, and no common usage is blocked |
| C3 | An unknown interpolator is a compile error (A0-3) | **Keep** (D56). Following Scala needs `extension (sc: StringContext)`, which is Phase 4; accepting it now would build a `StringContext` nothing can dispatch on. Phase 4 Task 10 closes it |
| C4 | `Map`/`Set`/`Vector`/`Range` as runtime prototypes, not prelude classes (A0-4) | **Runtime prototypes.** No surface effect; a prelude `Map` would claim the `@Map` type key `builtinTypes()` needs (the Phase 5 `Actor` lesson) |
| C5 | `Map`/`Set` iteration is ascending-hash (A0-6) | **Keep** (D58). Scala guarantees no order either, so there is nothing to match; every fixture sorts. An ordered map is a different type (`ListMap`), scheduled for v0.7+ |
| C6 | `List == Vector` implemented once in `valuesEqual` (A0-7) | **Once.** Two `equals` methods would reintroduce an `==`/`.equals` split. Now generalised to `SeqView` by C1 |
| C7 | A `Range` bound must fit 54 bits (A0-9) | **Keep the divergence** (D61). Matching Scala means boxing both bounds, i.e. an allocation on every `Range` method, to serve ranges of more than 2^53 elements — expensive, and nothing common needs it |
| C8 | `sorted` uses the runtime's `compare`, with a loud error for incomparable elements (A0-10) | **Keep** (D62). Matching Scala needs `Ordering`, i.e. implicits (D3) — a whole subsystem. The common usage (sorting numbers and strings) is identical, and `sortBy`/`sortWith` cover the rest |
| C9 | `Try { e }` by-name, `Try(() => e)` still accepted (A0-12) | **By-name**, since by-name parameters landed on `main` (`bca0352`). No deviation; **D64 unused**. D53's limit applies: lazy only where the compiler resolves the call site |
| C10 | `Failure` keeps `RuntimeError` for Phase 4 to migrate (A0-13) | **Keep for now.** A phase-ordering choice, not a divergence; doing it here would duplicate Phase 4's work and break `Future` failures in between |
| C11 | `foldLeft` accepts both `xs.foldLeft(0)(f)` and `xs.foldLeft(0, f)` (Task 8 Step 2) | **Accept both.** Permissive, not divergent: the Scala spelling works, and the extra one costs a one-shot applier that `Actor.spawn` already demonstrates |
| C12 | `String.split` takes a literal separator, not a regex (Task 11) | **Keep the divergence** (D70). Matching Scala means a regex engine — a dependency this dialect does not have and would not add for `split`. `split(",")`, the common case, is identical; `split(".")` differs and the fixture says so |
| C13 | `String.split` returns a `List`, not an `Array` (Task 11) | **Keep** (D69). There is no `Array` type (D12 already routes varargs to `List`); adding one for `split` would be a new collection with no other use |
| C14 | `Range.map` returns a `List`, not an `IndexedSeq` (Task 6 Step 3) | **Keep** (D68). `IndexedSeq` is a `Seq` trait, which R6 rules out of this phase; the elements and their order are Scala's |
| C15 | `Either` and `Try` have no `withFilter` (Task 5 Step 3) | **Keep** (D67). Scala's needs a `Left` to fall back to, which needs the static type; `for (x <- e if p)` over an `Either` is rare and fails loudly with `NoSuchMethodError` |
| C16 | `%e`/`%f`/`%g` convert through `Double`, so an integer above 2^53 prints rounded (Task 4) | **Keep** (D66). Matching Scala means a bignum decimal formatter; `%d` is exact and is what an integer wants |

## Self-review against the done-when criteria

**ROADMAP Phase 3 "Done when", clause by clause:**

| Clause | Where it is met |
|---|---|
| SmallInteger fast paths with LargeInteger promotion are covered by boundary fixtures (±2^53) | **Task 1** — `tests/conformance/15-integers/` (8 fixtures at `PROTO_SMALL_INT_MAX`/`MIN`, add, subtract, multiply, negate and a factorial that crosses the boundary), plus the two fast-path defects the coverage exposed (`EQ`/`NE`, the `unary_-` interning) |
| `List` passes its fixtures — this phase completes the surface | **Tasks 7 and 8** — `tests/conformance/17-collections/list-*.scala`, 28 new methods |
| `Vector` passes its fixtures | **Task 10** — `tests/conformance/17-collections/vector-*.scala` |
| `Range` passes its fixtures | **Task 6** — `tests/conformance/17-collections/range-*.scala` |
| `Option` passes its fixtures (landed in Phase 2; extended here) | **Task 5 Step 2** — `fold`, `toRight`, `toLeft`, `orNull`, `forall`, `count`, `zip`, `iterator`, `toSeq`; fixture `19-either-try/option-fold-and-toright.scala` |
| `Either` passes its fixtures | **Task 5 Step 3** — `tests/conformance/19-either-try/either-*.scala` |
| `Try` passes its fixtures (landed in Phase 5; extended here) | **Task 5 Steps 1, 4, 5** — the combinators and `Try(() => e)`; fixtures `try-combinators.scala`, `try-to-either.scala`; **the payload stays `RuntimeError` for Phase 4 to migrate (A0-13)** |
| tuples pass their fixtures (landed in Phase 2) | Phase 2's `08-case-classes/`; this phase adds tuple-producing methods (`zip`, `zipWithIndex`, `splitAt`, `partition`, `Map.toList`, `->`), each of which builds a **`Tuple2` case-class instance through `engine->construct(L.tupleCompanion[2], …)`, never a `ProtoTuple`** — Tasks 7, 9 |
| string interpolation passes its fixtures | **Tasks 2, 3, 4** — lexer (already done in Phase 1), parser (Task 2), desugarer + `CONCAT` (Task 3), `f` formatter (Task 4); `tests/conformance/16-strings/interpolation-*.scala`, 14 fixtures |
| `Map`/`Set` pass on `ProtoMap` | **Task 9** — `tests/conformance/18-maps-and-sets/`, 22 fixtures (12 surface, 6 classification, 4 carrying the C1 and C2 rulings), every operation through `proto::hashedPut`/`hashedGet`/`hashedRemove`/`hashedForEach` with one `KeySemantics` implementing **DESIGN §6.1's identity/value classification as specified and as amended by the 2026-09-23 rulings** (A0-5); GC-pressure checks for both key kinds in `tests/cli/gc-pressure.sh` |
| benchmark suite v1 (`fib`, `tak`, `sum-loop`, `list-ops`, `map-build`) self-reports and is recorded in `benchmarks/RESULTS.md` | **Task 12** — `fib`, `tak` and `sum_loop` already exist; `list_ops.scala` and `map_build.scala` are new, both print the work done, both are registered in `WORKLOADS`, and Step 5 **proves** the verification by sabotaging an `// EXPECT:` line and watching the cell go FAILED |
| its tutorial chapters are written (Documentation track) | **Task 13** — chapters 8 and 10, with a fixture per runnable snippet and the bridge/departures chapters extended |
| STATUS.md is updated and the full suite is green | **Task 14** |

**DESIGN clause by clause:** §3.4's interpolation row is amended by Task 14 Step 5 to record A0-1; §3.5's opcode families gain `CONCAT` in the reserved `39..63` band (Task 3); §3.6's SmallInteger fast paths are audited and extended (Task 1); §4.1's value table is unchanged; §4.6's `ProtoTuple` prohibition is honoured at every tuple-producing site (Tasks 7, 9, and the Global Constraints list them explicitly); §5.1's universal `apply` is what makes `m("key")`, `v(0)` and `r(2)` work with no new machinery (Tasks 6, 9, 10); §6's representation table is implemented exactly (Tasks 6, 9, 10) including `List(1,2) == Vector(1,2)`; §6.1's `ProtoMap` scheme is implemented through protoCore's own helper **exactly as §6.1 specifies it** — the identity/value classification quoted verbatim in A0-5, decided by an exact pointer comparison against `RuntimeLayout::defaultEqualsMethod`, and covered by six fixtures written so that a misclassified key fails loudly (a wrong `size`, a missing entry, a printed miss) plus two `XFAIL` counterexamples (C1, C2) put to the maintainer; §10's testing strategy is followed (unit, conformance in both syntaxes, CLI, GC pressure, self-reporting benchmarks, TDD — every task writes its fixtures first); §11's R2 is honoured and R8 is untouched (no new tag).

**Gaps found while reviewing this plan, and closed inline:**

- `foldLeft` has two parameter lists in Scala and a native has one. Closed in Task 8 Step 2 with a one-shot applier and Q15, rather than leaving `xs.foldLeft(0)(_ + _)` broken.
- `Map(1 -> "a")` needs `->` on `Any`, which nothing in DESIGN mentions. Closed in Task 9 Step 5, with a parsing check (`->`'s precedence) before the rest of the task proceeds.
- `Map.get` must distinguish a stored `null` from an absent key. Closed by using `hashedGet`'s `nullptr` return rather than comparing with `PROTO_NONE` — the exact ambiguity `feedback_protocore_proto_none_absence` records — and pinned by the `AStoredNullIsDistinguishableFromAbsence` unit test.
- `sorted`'s comparator may re-enter the VM and allocate, so the elements cannot live in a `std::vector<const ProtoObject*>`. Closed in Task 8 Step 3 by sorting **indices** in `std::` and keeping every object in slots of a dedicated context — the P1 rule that the Phase 5 `Mailbox::push` bug taught.
- `CONCAT`'s intermediate conversions allocate. Closed by writing each converted piece back into its operand-stack slot before the next allocation (Task 3 Step 3), the same rooting discipline.
- DESIGN §6.1 still says `Map`/`Set` fixtures are `XFAIL` until protoCore lands the work, and ROADMAP's Phase P1 header still says "pending maintainer merge", although protoCore 2.1.0 ships `ProtoMap`, the helper **and** `ProtoMPSCQueue`. Both are corrected by Task 14 Steps 4 and 5 rather than left to contradict the code.
- LANGUAGE §4 promises `Seq`/`Iterable` as traits and ROADMAP's done-when does not mention them. **Ruled 2026-09-23 in favour of ROADMAP**: they are not delivered (D65) and Task 14 Step 3 deletes the promise from LANGUAGE §4, because a document that promises what does not exist is worse than an absent feature.
- The first draft proposed answering `isIdentityKey` with a constant `false`, to remove a class of silent wrongness. **Overturned 2026-09-23: DESIGN §6.1 is the authority.** The concern was met instead by making the classification exact — a pointer comparison against `RuntimeLayout::defaultEqualsMethod` — and by building Task 9 Step 1's fixtures so a misclassified key shows up as a wrong `size`, a missing entry or a printed miss. Two genuine problems inside §6.1 surfaced while restoring it and went to the maintainer as concrete counterexamples rather than as policy: C1 (`Char` versus `Int` keys) and C2 (parameterised `enum` cases, where §6.1's two bullets contradicted each other). **Both were ruled toward Scala on 2026-09-23 and `docs/DESIGN.md` §6.1 was edited accordingly**; neither leaves a deviation behind.
- The first draft also had `(0 until 3) == List(0, 1, 2)` diverge, on the assumption that matching Scala meant materialising a `Range` on a hot opcode. **Decided on cost and reversed:** an allocation-free `SeqView` — which *replaces* the `vectorListOf` helper the `Vector` work needed anyway — makes it `true` in about 58 lines with an O(1) length short-circuit, so the divergence was not worth its price. D60 withdrawn.
- Re-triaged the whole of Task 0 against the maintainer's cost-proportional rule: **Phase 3 now escalates nothing.** Sixteen items are recorded as decided-on-cost with the cost line that decided each, six as ruled. What is left for the maintainer is not a semantics queue but one roadmap note: `Seq`/`Iterable` and `collect` are common Scala idioms and belong in a later phase's done-when if they are wanted.
- `ExecutionEngine.cpp`'s `NEG`/`NOT` cases intern `unary_-`/`unary_!` on every execution — the anti-pattern the Global Constraints forbid and an R4 leak besides. Found while reading for Task 1 and closed there, with a measurement rather than an assertion.

**Ordering constraints with Phase 4:**

1. **`Failure`'s payload.** Phase 3 must **not** re-point `Failure` off `RuntimeError` (A0-13, Task 5 Step 1). Phase 4 does it, and closes D44 and D50 with it. Running Phase 4 first instead is also sound — Phase 3 would then extend `Try` against `Throwable` from the start, and A0-13 becomes a no-op.
2. **The benchmark baseline.** Phase 4 wraps every frame's dispatch loop in a retry loop for the handler table. Task 12's suite-v1 table must be measured **before** that change if it is to serve as the reference Phase 4 compares against; if Phase 4 runs first, its own baseline is the Preflight run and Phase 3's table is measured on top of it. Either order works, but the two phases must not both claim "no regression" against the same undated numbers.
3. **Custom interpolators.** A0-3/D56 rejects any interpolator that is not `s`, `f` or `raw` because defining one needs `extension (sc: StringContext)`. Phase 4's extension-method task should close D56 and add a custom-interpolator fixture; if Phase 4 runs first, Phase 3's Task 3 Step 4 can accept an unknown interpolator by desugaring to `StringContext(literals).name(args)` instead, and D56 is never recorded.
4. **`Priority` as an `enum`.** D52 records that `Priority.High`/`Medium`/`Low` are integers because `enum` is Phase 4. Nothing in Phase 3 touches it; Phase 4's `enum` task closes it.
5. **No other coupling.** `Map`, `Set`, `Vector`, `Range`, the `List` surface, the `String` surface and string interpolation need nothing from Phase 4, and Phase 4's exceptions, `super[T].m`, `enum`, named/default arguments, extension methods and nested templates need nothing from Phase 3.
