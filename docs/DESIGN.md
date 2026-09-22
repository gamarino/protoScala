# protoScala — Design Specification

> **Status:** approved design, pre-implementation (2026-09-22). Nothing in
> this document is implemented yet unless [STATUS.md](STATUS.md) says so.
> Sections marked *(platform)* describe work in **protoCore** or in sibling
> runtimes that is part of this project.

protoScala is a dynamic dialect of Scala 3 that runs on the protoCore object
kernel. It is also a **platform validation project**: protoCore is meant to be
a solid base for implementing *any* language, and each new language exposes
the capabilities it is missing. Those capabilities are added to protoCore as
part of this project and, where they solve a problem the sibling runtimes also
have (protoClojure, protoST), those runtimes are migrated as well.

---

## 1. Vision and positioning

protoScala does **not** emulate the JVM, produce Maven/sbt artifacts or
reproduce `scalac`'s static typechecker. Its value proposition:

1. **Instant start-up and small footprint** — target < 20 ms to a REPL prompt
   and ~20 MB RSS, suited to systems scripting and interactive use.
2. **Native structural immutability** — functional collections and case
   classes map onto protoCore's persistent structures.
3. **Real concurrency without a GIL** — native actors with O(1) message
   passing by pointer to immutable data and lock-free mailboxes with three
   priority bands.
4. **Transparent polyglot interop in memory** — Python, JavaScript, Smalltalk
   and Clojure modules are consumed and exposed through protoCore's UMD
   (Unified Module Discovery) without serialization.

### 1.1 Engineering principles

Inherited from protoClojure (P1–P4) and protoST, and binding for every change:

- **P1 — Values live in protoCore structures.** Every value the GC must see
  lives in a `ProtoContext` slot (automatic locals) or in a protoCore
  structure reachable from one. No `std::` container holds `ProtoObject*`
  across an allocation, except tightly scoped optimisations with no
  consequence for the object model or the GC, documented where they occur.
- **P2 — One `ProtoContext` per invocation**, chained through `previous` to
  the thread's root context.
- **P3 — Missing capability extends protoCore.** When protoCore lacks a
  semantic, the capability is added to protoCore — preferably as a **new
  object type with the new semantic**, never by changing the model of an
  existing type other runtimes rely on.
- **P4 — Every deviation from Scala is documented** with a stable id (`D<n>`)
  in [STATUS.md](STATUS.md).
- **P5 — Purity over performance.** A performance fix that fragments
  protoCore's conceptual model is rejected; legitimate performance work
  preserves it.
- **P6 — No mutable-heap GC premises.** protoCore's collector stops user
  threads only to collect stack roots, the mutables-tree root and global
  structure roots; everything else is traced concurrently. No design in this
  project may add stop-the-world work beyond that.

---

## 2. Language scope

The full language surface, with the supported subset per milestone, is in
[LANGUAGE.md](LANGUAGE.md). The decisions that shape the implementation:

| Decision | Choice |
|---|---|
| Surface syntax | **Scala 3, complete**: braces *and* significant indentation (`:` / `then` / `do` / `end` markers), from Phase 1 |
| Types | **Parsed and erased.** Type annotations, type parameters, bounds and type aliases are parsed fully and ignored at run time. Types matter only where they change semantics: type patterns (`case x: Int`) and `isInstanceOf` / `asInstanceOf` |
| Integers | `Int`, `Long` and `BigInt` are one dynamic integer: `SmallInteger` (54-bit) promoting to `LargeInteger` on overflow; no 32/64-bit wrap-around (deviation D1) |
| Floating point | `Double` is protoCore's boxed double; `Float` is an alias of `Double` (D2) |
| Implicits / givens | Out of scope for v0.1 (D3) — they require static types to resolve |
| Macros, inline metaprogramming, Java reflection, Java interop | Out of scope |

---

## 3. Architecture

```
source ──► Lexer ──► Parser ──► AST ──► Desugar ──► Compiler ──► BytecodeModule ──► ExecutionEngine
            │ (INDENT/OUTDENT                 │ (for, apply, infix,        │                 │
            │  insertion)                     │  interpolation, match)     │                 ├─ Primitives / StdLib
            └─ tokens                         └─ core AST                  └─ constants,     ├─ ActorScheduler
                                                                              blocks          └─ ScalaModuleProvider (UMD)
```

The pipeline mirrors protoClojure and protoST so lessons transfer directly.

### 3.1 Repository layout

```
protoScala/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # CLI: script runner, REPL entry, --version/--help
│   ├── frontend/             # Lexer (with offside rule), Parser, AST, Desugar, Linearizer
│   ├── compiler/             # Compiler, Opcodes, BytecodeModule
│   ├── runtime/              # ExecutionEngine, Primitives, Function, CaseClass,
│   │                         # Collections, Exceptions, ActorScheduler, StackGuard
│   ├── repl/                 # readline REPL
│   └── umd/                  # ScalaModuleProvider
├── lib/                      # Prelude written in protoScala (Option, Either, Try, ...)
├── tests/
│   ├── unit/                 # GoogleTest: lexer, parser, desugar, bytecode, runtime
│   ├── conformance/NN-topic/ # black-box .scala fixtures with // EXPECT: directives
│   └── cli/                  # shell tests of the binary
├── benchmarks/               # bench.sh, actor-bench.sh, RESULTS.md
├── examples/
└── docs/                     # this spec, LANGUAGE, ROADMAP, STATUS, INTEROP, platform/, plans/
```

Static libraries: `protoscala_support`, `protoscala_frontend`,
`protoscala_compiler`, `protoscala_runtime`, `protoscala_repl`; binary
`protoscala`. protoCore is found through `PROTO_CORE_PREFIX` or the sibling
tree `../protoCore/build_release` (same logic as protoClojure and protoST).

### 3.2 Lexer

- Tokens: identifiers (alphanumeric, operator identifiers, backquoted),
  keywords (Scala 3 hard and soft keywords), integer/floating/character/string
  literals, triple-quoted and interpolated strings (`s"..."`, `f"..."`,
  `raw"..."`) as structured tokens, comments (nested block comments).
- **Offside rule:** the lexer tracks an indentation-region stack and inserts
  `INDENT` / `OUTDENT` / `NEWLINE` tokens following the Scala 3 reference
  rules (regions opened after `:`, `=`, `=>`, `then`, `else`, `do`, `try`,
  `catch`, `finally`, `match`, `with`, `yield`, `return`, `throw`, class
  bodies; closed by dedent or `end` markers). Inside `(...)` and `[...]` no
  indentation tokens are inserted. Braces and indentation may be mixed exactly
  as Scala 3 allows.
- Unit tests pin every region rule with token-stream fixtures before the
  parser consumes them.

### 3.3 Parser and AST

A hand-written recursive-descent parser with precedence climbing for infix
operators (Scala's precedence by first character; right associativity for
operators ending in `:`). The AST is a C++ tree owned by the frontend (it
never contains `ProtoObject*`; constants are materialised by the compiler).
Type syntax is parsed into a `TypeTree` node that the compiler ignores except
in patterns and `isInstanceOf`.

### 3.4 Desugar

Performed on the AST before code generation:

| Source | Core form |
|---|---|
| `for (x <- xs) yield e` | `xs.map(x => e)` |
| `for (x <- xs if c) yield e` | `xs.withFilter(x => c).map(x => e)` |
| `for (x <- xs; y <- ys) yield e` | `xs.flatMap(x => ys.map(y => e))` |
| `for (x <- xs) body` | `xs.foreach(x => body)` |
| `for (p = e; ...)` value definitions | tupled `map` per the Scala spec |
| `f(args)` where `f` is not a method | `f.apply(args)` |
| `a op b` | `a.op(b)`; `a op: b` → `b.op:(a)` (right-associative) |
| `a(i) = v` | `a.update(i, v)` |
| `x op= y` (no `op=` member) | `x = x op y` |
| `s"a $x b"` | `StringContext("a ", " b").s(x)` compiled to a fast concat primitive |
| `e match { case ... }` | a decision cascade (§5.3) |

### 3.5 Compiler and bytecode

- Fixed 32-bit instructions: 8-bit opcode, 24-bit operand, with an `EXTEND`
  prefix for wider operands (protoST).
- One `BytecodeModule` per function body: constant pool (de-duplicated by
  kind), arity, local count, capture specs, source-line table.
- Opcode families: stack/locals/captures, globals and module members, calls
  (`SEND`, `SEND_SUPER`, `CALL_APPLY`, keyword-argument variants), closures
  (`MAKE_FN`), control flow (jumps, `JUMP_BACK` for loops), arithmetic and
  comparison fast paths (`ADD`, `SUB`, `MUL`, `LT`, `LE`, `GT`, `GE`, `EQ`),
  pattern tests (`TEST_TYPE`, `TEST_PROTO`, `UNAPPLY_FIELDS`), exceptions
  (`THROW`, handler table per module), actors (`SEND_ASYNC`, `ASK`, `AWAIT`).
  The exact numbering is fixed in Phase 1 and extended per phase; it lives in
  `src/compiler/Opcodes.h` and is mirrored in `docs/STATUS.md`.
- Closures capture **per activation**: each activation of a lambda body gets
  its own captured environment (protoST D30 is the counter-example to avoid).

### 3.6 Execution engine

- **Recursive VM with frame snapshots** (the protoST model). `execute()`
  recurses natively per call; each frame owns a `ProtoContext` whose
  automatic locals hold parameters, locals and the operand stack.
- **Native stack guard** from the first commit (protoClojure `StackGuard`):
  each `execute()` compares against a thread-local limit and raises
  `StackOverflowError` instead of crashing; the evaluator runs on a dedicated
  large-stack thread; large buffers live in `[[gnu::noinline]]` helpers.
- **Dispatch:** `switch` loop first; threaded-goto only if a measured profile
  justifies it (protoST measured the loop at 0.73 % of CPU after threading —
  representation and allocation dominate).
- **SmallInteger fast paths** inline the tag test (tag bits 0–9, value in bits
  10–63) and fall back to protoCore `Integer::add/subtract/multiply`, which
  promote to `LargeInteger`.
- **Thread-local active call context** (protoClojure) lets native primitives
  re-enter the VM (`map`, `foldLeft`, ...) without changing the protoCore
  `ProtoMethod` signature. It is saved and restored on every exit path.
- **GC cooperation:** blocking waits run inside `ProtoContext::UnmanagedScope`;
  no user code runs inside a protoCore `CriticalSection`. The need for a GC
  poll at loop back-edges is an open platform question (§9, R1).

---

## 4. Object model

### 4.1 Values

| Scala | protoCore representation |
|---|---|
| `Int`/`Long`/`BigInt` | `SmallInteger` (embedded) → `LargeInteger` (heap) |
| `Double`/`Float` | boxed double |
| `Boolean` | `PROTO_TRUE` / `PROTO_FALSE` |
| `Char` | embedded unicode char |
| `String` | `ProtoString` (rope); Scala methods installed on protoCore's string prototype |
| `Unit` / `()` | a dedicated singleton object `unit` |
| `null` | `PROTO_NONE` (probe presence with `hasOwnAttribute`, never by comparing to `PROTO_NONE`) |
| functions / lambdas | objects whose prototype is `Function<N>`; `apply` executes the bytecode |

### 4.2 Classes, instances, singletons

- A class is a prototype object. `new C(args)` creates
  `C.newChild(ctx, /*isMutable=*/false)` and sets the constructor fields; a
  class that declares any `var` field creates mutable instances
  (`newChild(ctx, true)`) so the same handle observes updates.
- Methods are attributes on the class prototype. The dispatch path uses
  interned attribute keys and `getOwnAttributeDirect`-first lookups (the
  protoPython fast-path lesson), relying on protoCore's per-thread attribute
  cache — no parallel inline caches.
- `object O` is a lazily initialised singleton, created on first access and
  stored in the module's globals.
- A companion object and its class are linked through a `__companion__`
  attribute; `C(args)` on a companion resolves to `C.apply(args)`.

### 4.3 Traits and linearization

- `class C extends B with T1 with T2` is linearized **by the frontend** with
  Scala's algorithm: `L(C) = C, L(T2) ⊕ L(T1) ⊕ L(B)` where `⊕`
  concatenates right-to-left keeping the **last** occurrence of shared
  ancestors. Result for the example: `C, T2, T1, B, <B's ancestors>, AnyRef, Any`.
- The resulting flat list is installed with `setParents(ctx, list)`.
  protoCore attribute lookup walks exactly the object's own flattened chain in
  order, so ordinary dispatch follows Scala's linearization.
- Parent chains are frozen at object creation (protoST D21): a class's parent
  list is complete before any instance exists.

### 4.4 `super` in stackable traits

Resolved **in protoScala** (maintainer decision). Each compiled method records
its defining class/trait by identity. `super.m(args)` inside a method defined
in `T` executes:

1. `parents = self.getParents(ctx)` (the receiver's linearization);
2. find the index of `T` by pointer identity;
3. probe `getOwnAttributeDirect(m)` on each subsequent entry; call the first hit.

The cost is O(linearization length) per `super` call. No name matching (the
protoST wart) and no protoCore change. If profiling shows it matters, the
platform option of a protoCore "lookup after parent" API is recorded as R6.

### 4.5 Case classes

- Instances are immutable objects whose constructor fields are attributes set
  at construction.
- The compiler synthesises on the companion: `apply`, `unapply`; on the class:
  `copy` (named/default arguments), `equals` (structural), `hashCode`
  (structural, stable across runs for strings and numbers), `toString`
  (`Point(1,2)`), `productArity`, `productElement(i)`, `_1..._N`.
- `case object` is a singleton with identity equality.
- `sealed trait` / `enum` produce a prototype and one child per case;
  exhaustiveness is not checked (types are erased; D4).

### 4.6 Tuples

**Scala tuples are case classes, not `ProtoTuple`.** protoCore interns every
`ProtoTuple` node and interned tuples are perennial (`core/ProtoTuple.cpp:214`,
`core/ProtoSpace.cpp:449`): each transient `(a, b)` would stay alive for the
life of the process. `TupleN` are therefore modelled as they are in Scala —
case classes `Tuple2(_1, _2)` ... — with a specialised constructor primitive.
This also gives correct `==`, `hashCode` and `toString` for free. (Whether
protoCore should provide a non-interned or weakly interned tuple is platform
risk R2.)

---

## 5. Invocation, desugaring and pattern matching

### 5.1 The universal `apply` rule

`expr(a1, a2)` where `expr` is a value compiles to a send of `apply`, using
protoCore's calling convention:

```cpp
obj->call(ctx, nullptr, symApply, obj, positionalArgs, keywordArgs);
```

This unifies lambdas, companion objects (`List(1, 2, 3)`), indexed access
(`vec(0)`) and map lookup (`map("key")`). Calls to protoScala-compiled
functions short-circuit inside the VM (no native trampoline); calls to foreign
objects go through `call` with a `ProtoSparseList` of keyword arguments, which
is the same convention every proto* runtime understands.

### 5.2 Named and default arguments

Named arguments are passed in the keyword `ProtoSparseList` keyed by interned
parameter names. Default values are compiled into the callee's prologue.

### 5.3 Pattern matching

`e match { case ... }` compiles to a cascade:

1. **Type / class test** — native tag tests for primitives (`isSmallInt`,
   string, double, char, boolean) or prototype membership (`isInstanceOf`
   against the class prototype) for classes and traits.
2. **Extraction** — case classes read their fields by interned key
   (`getOwnAttributeDirect`); tuples are case classes; `h :: t` on a `List`
   extracts `getAt(0)` and the tail slice; a user `unapply` returning
   `Option` is called for custom extractors.
3. **Binding** — pattern variables bind to local slots; guards run after
   binding; the first matching case wins; no match throws `MatchError`.

Supported patterns: literals, wildcards, variables, typed patterns,
constructor patterns, tuples, `::`, alternatives (`|`), binders (`x @ p`),
sequence patterns with `_*`, and guards.

---

## 6. Collections

| Scala | Representation |
|---|---|
| `List[T]` | `ProtoList` (persistent AVL) — maintainer decision. `::` prepends in O(log n); `h :: t` extracts head and tail slice in O(log n). Scala `List` methods are installed on protoCore's list prototype, so a raw `ProtoList` *is* a Scala `List` |
| `Vector[T]` | an object (prototype `Vector`) holding a `ProtoList` in one attribute, so `List` and `Vector` stay distinguishable |
| `Map[K,V]`, `Set[T]` | objects (prototypes `Map`, `Set`) holding a **`ProtoSparseListObject`** (§6.1) |
| `TupleN` | case classes (§4.6) |
| `Option`/`Some`/`None`, `Either`, `Try` | written in protoScala in `lib/` |
| `Range` | an object with `start/end/step` and lazy iteration |

`List(1,2) == Vector(1,2)` is `true` (Scala `Seq` equality) — implemented in
the `equals` of both.

### 6.1 Map and Set on `ProtoSparseListObject` *(platform)*

protoCore's `ProtoSparseList` keys are untraced `unsigned long`s and
`ProtoSet` keys by `getHash` without collision handling, so neither gives
Scala's semantics alone. The maintainer's design adds a **new protoCore type,
`ProtoSparseListObject`**, identical to `ProtoSparseList` except that **the key
is a `ProtoObject*` the GC traces**. `ProtoSparseList` is not changed. The
full specification, including the tagged-pointer budget, is in
[platform/PSLO-SPEC.md](platform/PSLO-SPEC.md). protoScala uses it as follows:

- **Identity-equality keys** — objects whose `==` is `eq` (instances of
  classes with the default `equals`, `object`s, `case object`s, enum cases,
  symbols, booleans, chars): the key is the object itself. It is traced by the
  GC and recovered directly; the value slot holds `v`. No buckets, no
  collisions.
- **Value-equality keys** — numbers, strings, case classes (including
  tuples), collections, and classes that override `equals`: the key is the
  Scala `hashCode` encoded as a `SmallInteger` word (non-pointer, so the GC
  ignores it and it can never collide with an identity key). The value slot
  holds an entry `(k, v)` — a two-element `ProtoList`, *never* a `ProtoTuple`
  (§4.6) — or, on a genuine hash collision, a `ProtoList` of entries compared
  with `==`.
- `Set` uses the same scheme with the element as the entry.
- The language supplies equality and hash (Scala's cooperative numeric
  equality: `1 == 1L == 1.0`, and `##`). protoCore supplies the traced-key
  structure and, if the platform spec adopts it, a shared hashed-collection
  helper parameterised by the language's `hash`/`equals`.

Until the protoCore work lands, `Map`/`Set` conformance fixtures are `XFAIL`.

---

## 7. Exceptions

- `throw e` raises a C++ exception `ScalaThrow` whose payload is rooted in a
  slot of the throwing frame's context and re-rooted in the catching frame.
- `try { } catch { case ... } finally { }`: each module carries a handler
  table (pc range → handler pc); the VM catches `ScalaThrow` at the frame that
  owns the range, runs the `catch` cases as a pattern match and rethrows on no
  match. `finally` runs on every exit path.
- Scala has no resumable exceptions, so protoST's separate handler stack is
  not needed.
- Native errors (`std::runtime_error` from protoCore, stack overflow, bad
  arguments) are translated to Scala exceptions (`RuntimeException`,
  `StackOverflowError`, `IllegalArgumentException`); they are never swallowed.
- Control-flow signals (e.g. `FutureYield`, §8) do not derive from
  `std::exception`, so no generic handler can intercept them.

---

## 8. Concurrency: native actors

The model is protoST's (lock-free, protoCore-native state), with protoClojure's
Scala-friendly surface. protoClojure's C++-heap mailbox (unrooted pointers,
swallowed handler exceptions, silently dropped arguments) is **not** copied.

### 8.1 Semantics

```scala
val worker = Actor.spawn(initialState) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}

worker ! Increment(10)            // fire-and-forget
val f = worker ? GetValue         // ask: returns a Future
val result = f.await              // cooperative inside an actor; parks the main thread
```

The handler receives `(state, msg)` and returns `(newState, reply)`; the
reply completes the `Future` of an ask. Priority bands: `!` and `?` use
`Medium`; `actor.send(msg, Priority.High)` / `Priority.Low` and
`actor.ask(msg, priority)` select the others.

### 8.2 Implementation

- **Actor** — an object with `__state__`, `__handler__`, per-band
  `__mailbox__` attributes (immutable `ProtoList`s) and a `__sched__` flag.
- **Send** — builds an immutable envelope `(msg, future?)` and appends it to
  the band's mailbox with a `setAttributeIfEqual` CAS loop (O(1) message
  passing by pointer; no copies). The sender that moves `__sched__` 0→1
  enqueues the actor; the 0/1/2 state machine (idle / queued-or-running /
  wakeup-pending) guarantees one message at a time per actor. `finishDrain`
  CASes the state before re-checking the mailbox (protoST 0.2.0 fix).
- **Ready queues** — one lock-free Treiber stack per band with ABA-tagged
  heads; nodes pooled; each live actor is anchored once in a protoCore
  structure (`__live_actors__`) because the stack is invisible to the GC.
  Workers drain High, then Medium, then Low.
- **Worker pool** — `protoCore::newThread` workers (so they join the GC
  quorum), one per physical core by default, overridable with
  `PROTOSCALA_WORKERS`; spin-before-park, then park on a semaphore inside
  `UnmanagedScope`.
- **Futures** — a lock-free state machine on a `__state__` attribute.
  `await` inside an actor throws `FutureYield`: the engine snapshots the actor
  frames into `__suspended_frame__`, registers the actor as a waiter and the
  worker proceeds with other work; completing the future reschedules the
  actor, which resumes from the snapshot. On a non-actor thread `await` parks
  on a semaphore inside `UnmanagedScope`.
- **Handler failures** complete the ask's `Future` with the exception
  (`Failure`) and are reported on stderr; the actor keeps its previous state
  and stays alive. Supervision trees are out of scope for v0.1.
- **Shutdown** — every worker is joined before the `ProtoSpace` is destroyed,
  on every exit path, including errors and `sys.exit`.

---

## 9. Modules and polyglot interop (UMD)

- `ScalaModuleProvider` (alias `scala`) is registered with
  `proto::ProviderRegistry` and loads `.scala` files; a module's exports are
  its top-level definitions.
- **Prefix routing is a protoScala convention.** protoCore's resolver passes
  the whole logical path to every provider in the space's resolution chain,
  so protoScala routes prefixes itself:

  | Import | Resolution |
  |---|---|
  | `import py.numpy as np` | `getProviderForSpec("provider:py")->tryLoad("numpy")` |
  | `import js.d3 as d3` | `provider:js` |
  | `import st.PumpTwin as Twin` | `provider:st` |
  | `import clj.core as clj` | `provider:clj` |
  | `import util.Strings` | the space's resolution chain (protoScala first) |

- A foreign object is an ordinary `ProtoObject`; calls use the standard
  positional + keyword convention with no FFI layer and no copies.
- Interned symbols are per space: never cache them in function-local statics
  (protoST INTEROP §2.3).
- Details and type mapping in [INTEROP.md](INTEROP.md).

---

## 10. Testing strategy

- **Unit (GoogleTest)** — lexer token streams (including every offside
  rule), parser AST dumps, desugar output, bytecode emission, linearization,
  runtime primitives.
- **Conformance** — black-box `.scala` programs under
  `tests/conformance/NN-topic/`; first line is a directive:
  `// EXPECT: <last stdout line>`, `// EXPECT-ERROR[: <substring>]`,
  `// XFAIL: ...`, `// XFAIL-ERROR...`. One CTest case per file; files
  starting with `_` are helpers.
- **CLI** — shell tests of the binary (flags, REPL, exit codes, stack guard).
- **GC pressure** — selected fixtures run under a low `ProtoSpace` memory
  limit to force collections deterministically.
- **Benchmarks self-report** — every benchmark prints the work it computed
  and the runner verifies it; exit code alone never counts as success.
- TDD: each feature starts with failing fixtures.

---

## 11. Risk register and platform questions

Items marked *maintainer* are design decisions for protoCore's maintainer; this
project raises them and does not decide them unilaterally.

| Id | Risk / question | Impact | Owner |
|---|---|---|---|
| R1 | Allocation-free loops have no GC poll: a tight `while` can stall a stop-the-world pause (protoST S3). Needs an agreed back-edge poll API | GC latency | maintainer |
| R2 | Every `ProtoTuple` is interned and perennial. Scala avoids it (§4.6); **protoClojure maps vectors to `ProtoTuple`**, so long-running protoClojure programs retain every vector ever built. Options: weak interner, non-interned tuple type, or migration | memory growth | maintainer + platform track |
| R3 | `getAttribute` stops after 500 parent steps | deep trait hierarchies | maintainer |
| R4 | `createSymbol` leaks for strings longer than 6 bytes (protoClojure known issue) | memory | maintainer |
| R5 | One runtime per process (process-global UMD module cache, protoST K1) | multi-runtime tests | maintainer |
| R6 | `super` is O(n) per call (§4.4); a protoCore "lookup after parent" API is the escape hatch | performance | protoScala, later |
| R7 | No public API to attach a foreign OS thread | embedding | maintainer |
| R8 | Tagged-pointer budget: 37 of 64 pointer tags and 11 of 16 embedded types are free; every new protoCore type must justify a tag | platform longevity | platform spec |

---

## 12. Implementation phases

Summarised here; milestones, done-when criteria and tracks are in
[ROADMAP.md](ROADMAP.md); task-level plans are in [plans/](plans/).

| Phase | Content |
|---|---|
| 0 | Repository skeleton, build against protoCore, test harnesses, `--version` |
| P1 *(platform)* | `ProtoSparseListObject` in protoCore (+ hashed-collection helper), tests, full rebuild of every embedder |
| 1 | Lexer (with offside rule), parser, AST, core compiler + VM (expressions, `val`/`var`/`def`, `if`/`while`, lambdas), `println`, REPL |
| 2 | Classes, objects, traits + linearization, case classes, universal `apply`, for-comprehensions, pattern matching |
| 3 | SmallInteger fast paths, `List`/`Vector`/`Map`/`Set`/`Range`, prelude (`Option`, `Either`, `Try`), string interpolation |
| 4 | Exceptions, `super` in stackable traits, enums and sealed hierarchies, named/default arguments |
| 5 | Actors, priority bands, cooperative futures |
| 6 | UMD provider and prefix routing, CPack `.deb`/`.tgz` |
| C *(platform)* | protoClojure: maps/sets onto `ProtoSparseListObject`; vectors off `ProtoTuple` (R2) |
| S *(platform)* | protoST: `Dictionary`/`Set` onto `ProtoSparseListObject` (unblocks its D32 decision) |
