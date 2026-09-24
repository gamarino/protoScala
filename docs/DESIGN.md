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

1. **Instant start-up and small footprint** — target < 25 ms to a REPL prompt
   and ~20 MB RSS, suited to systems scripting and interactive use. (The target
   was 20 ms through Phase 1; it was raised to 25 ms in Phase 2, when the
   embedded Scala prelude began to grow — the standard library will keep adding
   start-up work, and 25 ms keeps the "instant" property without turning every
   prelude addition into a performance negotiation.)
2. **Native structural immutability** — functional collections and case
   classes map onto protoCore's persistent structures.
3. **Real concurrency without a GIL** — native actors with O(1) message
   passing by pointer to immutable data and lock-free mailboxes with three
   priority bands.
4. **Transparent polyglot interop in memory** — Python, JavaScript, Smalltalk
   and Clojure modules are consumed and exposed through protoCore's UMD
   (Unified Module Discovery) without serialization.

In one sentence: **an agile, interoperable, easily integrable and, above all,
very simple to use Scala.** protoScala is *not* a fast Scala. Competing with
the JVM on integer arithmetic or micro-benchmark loops is an explicit
non-goal: real processes are not optimized integer loops. Where the platform
is meant to shine is actors, complex persistent structures and deep object
graphs, plus instant start-up and in-memory interop. Language features,
simplicity of use and integration take priority over raw performance work,
and benchmark suites grow toward those workloads.

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
| `s"a $x b"` | the pieces joined by the `CONCAT` opcode (Phase 3, 2026-09-23, D54): `s` and `raw` lower directly to it and `f` to a call of the native `__fmt`, so no `StringContext` is built and a user-defined one is not consulted. Each piece is converted with `toScalaString` and joined with `ProtoString::appendLast`, an O(log n) rope join, so `s"$a$b"` on two strings copies neither |
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
- A companion object and its class are linked **at compile time**, in the
  compiler's type namespace, so `C(args)` resolves to `C.apply(args)` without
  a runtime link (Phase 2, Q3). A `__companion__` attribute was the original
  sketch, but a two-way link between two immutable objects cannot be built and
  nothing at run time needs it: class prototypes stay immutable (P6, no
  mutables-tree entry). A one-way `__companion__` on the object's class can be
  added later if interop asks for it.

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

Delivered in **Phase 2** (Q5): plain `super.m` is the same algorithm whether or
not stackable traits are involved, so stackable traits already work
(`tests/conformance/07-classes/stackable-traits.scala`).

**Amended 2026-09-24 (Phase 4).** `super[T].m` names the ancestor explicitly. The
site records `T`'s type key and an `exact` flag, and step 3 above becomes: probe
`T` **itself** first and only then continue after it. `super[A].m` therefore finds
`A`'s own `m` when `A` defines one and the `m` that `A` inherits when it does not,
which is what "the member `m` as seen from `A`" means. Starting strictly after `T`
would skip precisely the definition the programmer named.

Two consequences, both recorded: Scala additionally requires `T` to be a **direct**
parent, and protoScala checks only membership in the receiver's linearization,
because `ClassInfo` stores the flattened linearization and no direct-parent list
(**D76**, permissiveness only — no program scalac accepts behaves differently
here). And `super[T].m` written *inside* `T` would find `T`'s own `m` and recurse
for ever, so it is refused at compile time.

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

**Amended 2026-09-24 (Phase 4), with the rationale the section did not state.**

*The key convention.* The `keywordParameters` key is the **address of the interned
`ProtoString` symbol** for the parameter's name, obtained with
`ProtoString::createSymbol`. Interning guarantees exactly one address per name in
a `ProtoSpace`, so the address *is* the name's identity; and because UMD is
in-process, the same address is valid in every runtime, which is the unambiguity
the convention buys. `fromUTF8String`, `fromUTF8` and `fromStdString` do **not**
intern: they answer a different pointer for the same text, and a key built from
one of them matches nothing — silently, exactly as if the argument had not been
passed. The sharp edge is that protoCore embeds a **short** string in the pointer
word itself, so a non-interning key works by accident for a short parameter name
and fails only for one too long to embed; both halves are pinned by
`tests/unit/test_exceptions.cpp`
(`KeywordConvention.ANonInternedKeyMatchesNothing`).

An **integer-keyed `ProtoSparseList` is correct here, not a concession.** Interned
strings are perennial — never collected, never moved — so there is nothing for the
collector to trace. `ProtoMap` was added to protoCore because `Map`/`Set` need
arbitrary **collectable** object keys that the collector must trace; that is a
different problem. It is the same principle that makes `ProtoTuple` interned and
perennial, and the same reason transient data must never be mapped to it (§4.6).
A future reader should not mistake the integer key for a GC hazard and propose
migrating keyword dictionaries to `ProtoMap`.

*Where the binding happens, and why.* In the **callee**, not the caller.
`BytecodeModule` carries the parameter names (interned by `linkSymbols`) and one
optional default block per parameter; `execute`'s prologue fills positional
parameters, then each keyword argument into its parameter's slot by name, then each
still-unfilled parameter from its default block. A caller that does not know its
callee statically therefore still works. The maintainer's ruling of 2026-09-23, in
their own words:

> *"la plataforma es lazy binding, aunque Scala no lo sea"* — the platform is
> late-binding even when the source language is not.

A runtime built on protoCore may resolve at **run time** what its source language
resolves at compile time; that follows directly from a prototype-based,
dynamically dispatched, type-erased kernel, and it is why binding named arguments
in the callee is the *right* design here rather than a fallback forced by D4. No
future reader should mistake it for a limitation to be fixed by adding a static
resolution path. **The behavioural bar is what late binding does not excuse:**
every error names what was expected and what arrived — an unknown name, a
duplicate and a missing argument each raise an `IllegalArgumentException`
identifying the method and the parameter (**D81**). Late *detection* is accepted;
silent failure is not.

*Defaults.* Arguments are evaluated at the call site in **source order**, Scala's
rule, however the names reorder the binding. A default block mirrors the callee's
own slot prefix — its parameters, its locals and its captures — and is run with
those slots as its arguments, so a default may read the parameters declared before
it and any enclosing local, with no capture machinery of its own (**D88**).

### 5.3 Pattern matching

`e match { case ... }` compiles to a cascade:

1. **Type / class test** — native tag tests for primitives (`isSmallInt`,
   string, double, char, boolean) or prototype membership for classes and
   traits. Membership is tested with a **per-class marker attribute** keyed by
   the type key and found through the cached `getAttribute` walk, not with
   protoCore's `isInstanceOf` (Phase 2, Q2): that implementation stopped after
   50 visited objects, which gives false negatives on the flattened chains
   protoScala installs. The marker is exact, allocation-free and bounded by the
   chain length. The protoCore fix specified in
   [platform/ISINSTANCEOF-FIX.md](platform/ISINSTANCEOF-FIX.md) replaces it
   once it is merged and every embedder has been rebuilt.
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
| `Map[K,V]`, `Set[T]` | objects (prototypes `Map`, `Set`) holding a **`ProtoMap`** (§6.1) |
| `TupleN` | case classes (§4.6) |
| `Option`/`Some`/`None`, `Either`, `Try` | written in protoScala in `lib/` |
| `Range` | an object with `start/end/step` and lazy iteration |

`List(1,2) == Vector(1,2)` is `true` (Scala `Seq` equality).

**Implemented once, over one allocation-free view (Phase 3, 2026-09-23).** A
`SeqView` (`src/runtime/Values.cpp`) reads a `List`, a `Vector`'s `__vec__` list
and a `Range` — the last arithmetically, never materialised — and both
`valuesEqual` and `scalaHash` decide over it. So `List(1,2) == Vector(1,2) ==
(1 to 2)`, the three hash alike, and a `Map` keyed by one is found by another.
Deciding it in `valuesEqual`, before any prototype dispatch, is what keeps the
`EQ` opcode — which never dispatches — in agreement with `a.equals(b)`; two
`equals` methods would have reintroduced the classic `a == b` ≠ `a.equals(b)`
split. Because the view allocates nothing and the length check short-circuits,
`(0 until 1000000000) == List(1)` answers `false` in O(1).

### 6.1 Map and Set on `ProtoMap` *(platform)*

protoCore's `ProtoSparseList` keys are untraced `unsigned long`s and
`ProtoSet` keys by `getHash` without collision handling, so neither gives
Scala's semantics alone. The maintainer's design adds a **new protoCore type,
`ProtoMap`**, identical to `ProtoSparseList` except that **the key
is a `ProtoObject*` the GC traces**. `ProtoSparseList` is not changed. The
full specification, including the tagged-pointer budget, is in
[platform/PROTOMAP-SPEC.md](platform/PROTOMAP-SPEC.md). protoScala uses it as follows:

- **Identity-equality keys** — objects whose `==` is `eq` (instances of
  classes with the default `equals`, `object`s, `case object`s, **singleton**
  enum cases, symbols, booleans): the key is the object itself. It is traced by
  the GC and recovered directly; the value slot holds `v`. No buckets, no
  collisions.
- **Value-equality keys** — numbers, **chars**, strings, case classes
  (including tuples and **parameterised enum cases, which are case classes**),
  collections, and classes that override `equals`: the key is the
  Scala `hashCode` encoded as a `SmallInteger` word (non-pointer, so the GC
  ignores it and it can never collide with an identity key). The value slot
  holds an entry `(k, v)` — a two-element `ProtoList`, *never* a `ProtoTuple`
  (§4.6) — or, on a genuine hash collision, a `ProtoList` of entries compared
  with `==`.
- **`Char` is a value-equality key** (maintainer ruling, 2026-09-23). An
  earlier version of this section listed it among the identity keys. That was
  wrong on this section's own criterion: a `Char`'s `==` is **not** `eq`,
  because Scala's cooperative equality makes `'a' == 97` true and `'a'.##`
  equal to `97`, so `Map('a' -> 1)` and a lookup by `97` must find one key.
  `Char` is therefore the one kind the classification function special-cases
  explicitly, rather than pretending it behaves like the other immediates.
- **A parameterised `enum` case is a case class, so it is a value-equality
  key** (maintainer ruling, 2026-09-23). Writing `case Leaf(n: Int)` inside an
  `enum` *is* writing a case class, so it is classified as one and two
  separately built `Leaf(1)`s are one key, as in Scala. Only a **singleton**
  enum case (a `case object`) is an identity key. Before this ruling the two
  bullets above overlapped and disagreed for a parameterised case.
- `Set` uses the same scheme with the element as the entry.
- The language supplies equality and hash (Scala's cooperative numeric
  equality: `1 == 1L == 1.0`, and `##`). protoCore supplies the traced-key
  structure and, if the platform spec adopts it, a shared hashed-collection
  helper parameterised by the language's `hash`/`equals`.

The classification follows Scala wherever Scala has an answer; a divergence
needs a *platform* reason, not a convenience. `Char` and parameterised enum
cases are the two places that rule was applied (2026-09-23).

protoCore's `ProtoMap`, and the hashed-collection helper this section relies
on, were merged and released in **protoCore 2.0.0**; the `Map`/`Set`
conformance fixtures are therefore live, not `XFAIL`.

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

**Implemented 2026-09-24 (Phase 4).** Everything above is confirmed, including
"protoST's separate handler stack is not needed"; the four points below record how
the wording above is realised, because each was a real decision.

*Where the in-flight value lives while the stack unwinds.* The throwing frame's
`ProtoContext` is a C++ stack object that unwinding destroys, so "rooted in a slot
of the throwing frame's context" needs an operational reading: **every frame the
exception passes through re-roots the payload in its own
`ProtoContext::returnValue`**, unconditionally and before the handler search.
That slot is traced — protoCore's root scan reads `currentCtx->returnValue` for
every context on the thread's context stack — and `~ProtoContext` anchors a
non-null `returnValue` into the *parent's* young chain before submitting its own
young generation, so the payload stays a root one frame at a time. It is also the
slot the exception path never otherwise uses: `execute` assigns it only after the
frame returns. Alternatives considered and rejected: a per-thread pin stack (one
attribute write per throw, for a window this closes for free) and keeping the
value only inside the C++ exception object (P1-violating: a `finally` body
allocates).

*Where the handler search happens.* **Outside the C++ catch block**, as a retry
loop around `runLoop`: `ExecutionEngine::runFrame` is
`for (;;) { try { return runLoop(...); } catch (ScalaThrow&) { … ; continue; } }`.
Because the handler is entered by `continue` — after the catch block has been left
— a `catch` or `finally` body runs with no live C++ handler, an ordinary `ip` and
an ordinary operand stack, and therefore suspends cooperatively like any other
bytecode. The load-bearing reason, measured rather than assumed, is stronger: the
loop is what lets a frame catch a **second** exception — one raised by its own
handler body, by the `RETHROW` a non-matching cascade emits, or by a `finally`.
Entering the handler from inside the catch abandons the loop, so the frame's own
handler table is never consulted again; doing so turns eleven conformance fixtures
red (`nested-try`, `catch-in-a-loop`, `finally-in-a-loop`,
`finally-runs-on-failure`, `rethrow-from-a-handler` among them) and grows the
native stack once per caught exception.

*How `finally` is implemented.* A `Finally` handler-table entry plus an **inline
copy** on every normal exit, so the exceptional path has one mechanism (the entry
fires, the body runs, `RETHROW` re-raises the saved value) and the normal path is
straight-line code with no table lookup. For `try B catch C finally F` the compiler
emits a `Catch` entry covering `B` and a `Finally` entry covering `B ∪ C`, in that
order, and `handlerFor(pc)` answers the **first** entry in table order whose range
contains `pc`; nested `try`s append before enclosing ones, so table order is
Scala's search order. A handler body's pc lies outside its own range, so a handler
can never catch its own exception, and the cleanup itself is not protected by its
own entry — which is why a throwing `finally` replaces the in-flight exception
(**D72**) rather than looping. A `return` emits every enclosing cleanup inline
before its `RETURN`, innermost first and after the returned expression has been
evaluated; each such inlined copy is **excluded** from its own `try`'s handler
ranges, because the `try` has already been left (**D87**). RAII was rejected: the
cleanup would run inside a destructor, where a Scala throw crosses a `noexcept`
boundary and terminates the process.

*The native-error translation table.*

| Source | Becomes | Where |
|---|---|---|
| `ScalaError(cls, msg)` from any native throw site | an instance of the prelude class named `cls` | `runFrame`, **lazily**: the handler search runs first, so the uncaught path allocates nothing — it may be dying of `OutOfMemoryError` |
| `ScalaError` whose `cls` names no prelude class | `RuntimeException(cls + ": " + msg)`, keeping the original name | `materialiseError` |
| protoCore `std::runtime_error` | `RuntimeException(what())` | `runLoop` |
| `std::invalid_argument` | `IllegalArgumentException(what())` | `runLoop`, caught by its **exact** type |
| `std::out_of_range` | `IndexOutOfBoundsException(what())` | `runLoop`, caught by its **exact** type |
| `std::overflow_error` | `ArithmeticException(what())` | `runLoop`, **before** the `runtime_error` clause |
| `std::bad_alloc` and the `PROTOCORE_HEAP_LIMIT_CELLS` failure | `OutOfMemoryError` — an `Error`, so `case e: Exception` does not catch it | `runLoop` |
| the native stack guard | `StackOverflowError` — an `Error` | unchanged site, now catchable |
| `std::length_error` from bytecode limits | a **compile-time** failure reported by `Session` | not catchable |
| `std::logic_error` (a compiler or VM bug) | **not translated**; reaches `main.cpp` as `protoscala: internal error: …` | **not catchable** (**D74**) |
| `FutureYield` | **never translated, never catchable** | it is not a `std::exception` and `runLoop` catches it first |
| a UMD provider's or foreign method's C++ exception | `RuntimeException`, through the boundary shape of ROADMAP Phase 6 | Phase 6 |

`std::invalid_argument` and `std::out_of_range` both derive from
`std::logic_error`, so they are caught by their exact types and the engine carries
**no `std::logic_error` clause at all**: that is what keeps D74 true, and
`tests/unit/test_exceptions.cpp` (`NativeTranslation.AVmDefectIsNotCatchable`)
pins it. `ScalaThrow` derives from `std::exception` and deliberately **not** from
`std::runtime_error`, because `runLoop`'s protoCore bridge would otherwise rewrite
every Scala `throw` into a `RuntimeException`; a `static_assert` and a throw/catch
test pin that too.

---

## 8. Concurrency: native actors

**Model: protoClojure's actor system** (`protoClojure/src/runtime/ActorScheduler.*`,
`docs/tutorial/13-actors.md`, `benchmarks/actor-bench.sh`) — a worker pool, a
single-method invariant, three priority bands and a lock-free per-actor
mailbox — with a Scala surface, cooperative `await`, and the corrections listed
in §8.4. protoClojure's own design follows protoST's mailbox, so the three
runtimes share one actor model.

### 8.1 Surface

```scala
case class Increment(by: Int)
case object GetValue

// An actor owns a state and a handler (state, msg) => (newState, reply), or
// (state, msg) => newState when there is no reply to give (D45).
val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}

counter ! Increment(10)                    // fire-and-forget, Medium band
val f: Future[Int] = counter ? GetValue    // ask: Future of the reply, Medium band
println(f.await)                           // 10

counter.send(Increment(1), Priority.High)  // explicit band: High / Medium / Low
counter.ask(GetValue, Priority.Low)        // Future, explicit band
counter.value                              // current state, no message (like protoClojure's @actor)
Actor.isActor(counter)                     // true
Actor.stats                                // ActorStats(workers = N, messagesProcessed = K)
```

| protoClojure | protoScala |
|---|---|
| `(actor v)` | `Actor.spawn(v)(handler)` — the handler is fixed at spawn (Scala idiom: behaviour + messages) |
| `(send a f & args)` → promise | `a ! msg` (Unit) / `a ? msg` → `Future` |
| `send-h` / `send` / `send-l` | `Priority.High` / `Medium` (default) / `Low` via `send` / `ask` |
| `@a` | `a.value` |
| `(actor? x)` | `Actor.isActor(x)` |
| `(actor-stats)` → `{:workers :messages-processed}` | `Actor.stats` → `ActorStats(workers, messagesProcessed)` |
| `@promise` blocks the thread (1 ms polls) | `future.await` — cooperative inside an actor (§8.3) |

The printed form is distinct from other objects: `Actor(10)`.

### 8.2 Scheduler (protoClojure architecture)

- **Single-method invariant** — at any instant at most one message per actor
  is being processed. One `claimed` flag per actor is the single source of
  truth: the worker that claims the actor drains a batch (up to 8 messages,
  protoClojure's value), then the end-of-batch release CAS races the senders'
  claim CAS and at most one wins, so no concurrent sender can schedule the
  actor on a second worker (the protoClojure session-19 race and its fix).
- **Per-actor mailbox** — three **`ProtoMPSCQueue`s**, one per band: a new
  protoCore type (maintainer decision on R9; spec in
  [platform/PMQ-SPEC.md](platform/PMQ-SPEC.md)) that keeps protoClojure's
  lock-free O(1) push and whole-batch `takeAll`, with every queued item traced
  by the GC. Bands drain High, then Medium, then Low.
- **Global ready queues** — one per band; workers take the highest non-empty
  band first. protoClojure's ready queues sit behind one mutex + condition
  variable and its README records them as the MPMC bottleneck; protoScala uses
  lock-free per-band ready stacks with ABA-tagged heads (protoST
  `ReadyStack.h`) and spin-before-park (protoST: −91 % parks).
- **Worker pool** — workers created with `ProtoSpace::newThread` so they join
  the GC quorum; `PROTOSCALA_ACTOR_WORKERS`, default `max(2, cores − 2)`,
  cap 16 (protoClojure). Parked workers wait inside `UnmanagedScope`, so a
  parked worker never delays a GC pause.
- **Worker context** — each worker re-installs the thread-local active call
  context captured when the scheduler starts, so a handler sees the full
  runtime; each message runs in its own `ProtoContext` (P2).
- **Shutdown** — `ActorScheduler::shutdown(ctx)` joins every worker before the
  `ProtoSpace` is destroyed, on every exit path (normal end, uncaught
  exception, `sys.exit`).

### 8.3 Futures and cooperative `await`

- `Future` is a protoCore object with a lock-free state machine on a
  `__state__` attribute (pending → success | failure), completed with a single
  CAS (protoClojure `deliverPromise`).
- **Inside an actor**, `await` on a pending future throws the control signal
  `FutureYield` (not a `std::exception`); **each recursive
  `ExecutionEngine::execute` frame catches it, prepends its own record
  (module, instruction offset, the in-flight call's base slot and the live
  slots below it) to the `__snapshot__` list on the actor, and rethrows**, so
  the list reads outermost-first and the recursive VM stays recursive
  (implemented 2026-09-23; the limits it imposes are D43). `await` registers
  the actor as a waiter of the future and releases the worker to other actors.
  Resuming re-materialises the frames by recursion, writing the awaited value
  where the in-flight call would have left its result. Completing the future
  re-enqueues the actor, which resumes from the snapshot. The actor stays
  claimed while suspended, so the single-method invariant holds across the
  suspension (protoST `FutureYield`).
- **Outside an actor** (main thread, plain threads) `await` parks on a
  semaphore inside `UnmanagedScope` and is woken by the completing CAS — no
  polling.
- `Future.apply { ... }` runs a computation on the worker pool;
  `map`/`flatMap`/`recover` chain without blocking; for-comprehensions over
  futures work through the ordinary desugaring.

**Amended 2026-09-24 (Phase 4): how exceptions and a suspension interact.** Six
invariants, each with a named test, because getting one wrong breaks this section
silently rather than loudly.

1. **`FutureYield` is never a Scala exception.** It does not derive from
   `std::exception` (§7), `runLoop`'s `catch (FutureYield&)` stays the **first**
   clause, and `runFrame` catches only `ScalaThrow` and `ScalaError`. A suspension
   therefore passes straight through the retry loop untouched.
   *(`ScalaThrowShape.AFutureYieldIsNotAStdException`, `await-inside-try.scala`.)*
2. **No `finally` runs on a suspension.** A suspended frame is going to be
   *resumed*, not abandoned, so its cleanup has not been reached. This falls out of
   invariant 1, and it is the whole reason §7 puts the handler search outside the
   catch rather than in it.
   *(`finally-does-not-run-on-suspension.scala`, which prints from the cleanup
   rather than counting in a local: the frame snapshot is taken before the retry
   loop sees the suspension, so a counter a wrongly-run cleanup incremented would
   be discarded by the resume and the fixture would pass with the bug present.)*
3. **`resumeFrames` calls `runFrame`, never `runLoop`.** A resumed frame with no
   handler table active would silently fail to catch anything raised after the
   resume. *(`await-inside-finally.scala` goes red when this is broken.)*
4. **The bound exception variable survives a suspension for free.**
   `appendSuspendedFrame` saves `slots[0, pendingBase)` and `pendingBase` is an
   operand-stack index, always `>= arity + localCount`, so every local slot — a
   `catch` clause's bound `e` and the hidden `finally` save slot included — is
   inside the saved prefix. *(`await-inside-catch.scala`.)*
5. **A failed future resumes by raising, not by returning.** `await` on a failed
   future raises the carried `Throwable` *at the `await`'s call site*, so an
   enclosing `try` in the suspended handler catches it and the rest of the handler
   still runs. The resume path calls `runFrameRaising` on the innermost frame,
   which searches the handler table at the pc of the in-flight call (`ipOffset - 1`,
   the same index `runLoop`'s own catch reports) and then falls into the ordinary
   retry loop. **This retires D50.**
   *(`await-a-failed-future-inside-try.scala`,
   `await-a-failed-future-runs-the-finally.scala`.)*
6. **A handler exception still leaves the actor alive.** `ActorScheduler::deliver`
   catches, in this order, `FutureYield&` (rethrow — the suspension path),
   `ScalaThrow&` (complete the ask with `Failure(t.value)`) and `ScalaError&`
   (materialise, then complete). In both failure cases the actor keeps its previous
   state and the error is reported on stderr (§8.4). `Thread.start`'s body has the
   same chain minus the first clause, so a throwing thread reports and joins
   instead of terminating the process.
   *(`actor-survives-a-handler-exception.scala`, `thread-body-throws.scala`,
   `14-futures/failed-ask-carries-the-exception.scala`.)*

One residual limit, recorded as **D75**: an actor parked on a future that never
completes never runs the `finally` of the `try` it suspended inside. The shutdown
diagnostic reports how many actors are parked, and that is the only notice; Scala
has no equivalent situation, and making it otherwise would mean running arbitrary
user code during shutdown.

### 8.4 Corrections to protoClojure's implementation

protoClojure's STATUS and code record defects that protoScala must not
inherit:

| protoClojure defect | protoScala requirement |
|---|---|
| `ActorMessage` (fn, args, promise) and `ActorState::value` live on the C++ heap, unrooted (`ActorScheduler.h:61-89`) | every message payload, reply future and actor state is reachable from a protoCore structure while queued or in flight (P1): mailboxes are `ProtoMPSCQueue`s (R9, decided) |
| handler exceptions are swallowed and the result becomes nil | a handler exception completes the ask's future with `Failure(e)`, is reported on stderr, and the actor keeps its previous state; supervision trees are out of scope for v0.1 |
| send arguments beyond 15 silently dropped | a message is one object (any size); no argument limit exists |
| `deref` polls every 1 ms | parking on a semaphore; cooperative suspension inside actors |
| one OS thread per future / per `pmap` element | futures run on the worker pool |

### 8.5 Actor benchmarks

Harness `benchmarks/actor-bench.sh`, modelled on protoClojure's: each mode is a
`.scala` script that fires about 1,000,000 messages (runs of 1–4 s, well above
noise) and ends by printing `Actor.stats`. The runner **verifies the
self-reported `messagesProcessed` against the expected count before computing a
rate** — a silent failure never reads as infinite throughput. Every mode runs
with `PROTOSCALA_ACTOR_WORKERS` = 1, 2, 4, 6, 8, 16.

| Mode | Script | What it measures |
|---|---|---|
| `single` | `actor-throughput.scala` | 1 sender × 1 actor, 1M trivial messages — per-actor pipeline floor |
| `fan-out` | `actor-fanout.scala` | 1 sender × 1000 actors × 1000 messages — ready-queue stress |
| `MPSC` | `actor-mpsc.scala` | 4 sender threads × 250K → 1 actor — sender contention on one mailbox |
| `MPMC` | `actor-mpmc.scala` | 4 senders × 4 actors round-robin — both contention paths |
| `ping-pong` | `actor-pingpong.scala` | 2 actors exchanging asks, 200K round trips — ask/reply latency |
| `await` | `actor-await.scala` | 1000 actors each awaiting an ask to another actor — cooperative suspension (must finish with `workers = 1`; a blocking `await` would deadlock) |
| `priority` | `actor-priority.scala` | Low-band flood of 1M messages plus 1000 High-band probes — reports High-band latency p50/p99 |
| `saturation-8` | `actor-saturation-8.scala` | 8 actors × N/8 **CPU-bound** messages (a 20,000-iteration summation each, ~1.5 ms) — the actors-versus-workers axis |
| `saturation-32` | `actor-saturation-32.scala` | 32 actors × N/32 CPU-bound messages, identical total work — separates scheduler limits from actor-count limits |

> **Open for the maintainer (2026-09-23).** Whether this `saturation-*`
> addition stays is still undecided — see DECISIONS-LOG, "Still open". It is
> the only part of §8.5 that does not follow the section's own uniform rule.

The two `saturation-*` modes **deviate from the 1,000,000-trivial-message rule
above, deliberately**: they fire a few thousand expensive messages instead. The
seven modes above are each capped by their own structural concurrency (1 or 4
producers, or the single-method invariant) rather than by the machine's cores,
and `fan-out` is additionally producer-bound — 78-99% of its measurable window
sits inside the sender's loop. **None of them can exhibit a rise up to the
physical core count.** The `saturation-*` modes put the cost inside the handler
so the send loop falls below 1% of the run and the workers become the
constraint. They mirror protoST's `saturation_8a.st` / `saturation_32a.st`,
which measured 1.00× / 1.70× / 2.83× / 3.11× at w=6 on this machine before
regressing under SMT oversubscription. Their protoClojure twins carry the same
shape and the same message count, and their self-reported `processed` value is
the sum the actors computed, not a message count.

Reporting rules:

- `benchmarks/RESULTS.md` records machine, date, commit, worker count at the
  peak and the full table; the README shows the peak table, as protoClojure's
  does.
- **Comparison baseline:** protoClojure's `actor-bench.sh` on the same machine
  and date (identical `single`/`fan-out`/`MPSC`/`MPMC` shapes, so the rows are
  directly comparable), and, when a JVM is available, an equivalent Scala 3 +
  Akka Typed program as an external reference (reported with its JVM version
  and warm-up policy, never mixed into the protoCore rows).
- Any claimed improvement is backed by `perf stat -r 3` cycles; any change in
  the mailbox or scheduler re-runs the whole table.
- GC-pressure variants of `single` and `fan-out` run under a low `ProtoSpace`
  memory limit in the conformance suite (correctness, not speed): message
  counts must match and no crash or leak may occur.

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

### 10.1 Documentation

Documentation is part of every phase, not a final step. The dual-audience
tutorial ([TUTORIAL.md](TUTORIAL.md), 15 chapters) serves traditional Scala
programmers — an honest catalogue of departures keyed to the D-ids — and
developers coming from Python or JavaScript — Scala from first principles with
bridges to concepts they know. Each phase writes the chapters for the features
it delivers, and every runnable snippet is also a conformance fixture under
`tests/conformance/tutorial/`, so the text cannot drift from the
implementation.

---

## 11. Risk register and platform questions

Items marked *maintainer* are design decisions for protoCore's maintainer; this
project raises them and does not decide them unilaterally.

| Id | Risk / question | Impact | Owner |
|---|---|---|---|
| R1 | Allocation-free loops have no GC poll: a tight `while` can stall a stop-the-world pause (protoST S3). Needs an agreed back-edge poll API | GC latency | maintainer |
| R2 | Every `ProtoTuple` is interned and perennial. Scala avoids it (§4.6); **protoClojure maps vectors to `ProtoTuple`**, so long-running protoClojure programs retain every vector ever built. Options: weak interner, non-interned tuple type, or migration | memory growth | maintainer + platform track |
| R3 | `getAttribute` stops after 500 parent steps; `isInstanceOf` stops after 50 visited objects and keeps 64 pending siblings, which gives false negatives on the flattened chains protoScala installs (Phase 2, Q2). protoScala works around the second with per-class marker attributes (§5.3); the maintainer's fix ([platform/ISINSTANCEOF-FIX.md](platform/ISINSTANCEOF-FIX.md)) removes both caps and lands with the protoCore 2.0.0 merge | deep trait hierarchies | maintainer (fix specified, not yet merged here) |
| R4 | `createSymbol` leaks for strings longer than 6 bytes (protoClojure known issue) | memory | maintainer |
| R5 | One runtime per process (process-global UMD module cache, protoST K1) | multi-runtime tests | maintainer |
| R6 | `super` is O(n) per call (§4.4); a protoCore "lookup after parent" API is the escape hatch. **Now exercised**: Phase 2 ships `SEND_SUPER`, so every `super.m` walks the receiver's linearization | performance | protoScala, later |
| R7 | No public API to attach a foreign OS thread | embedding | maintainer |
| R9 | Actor mailboxes must be GC-visible (§8.4). **Decided (2026-09-22): a new protoCore type, `ProtoMPSCQueue`** — lock-free MPSC queue with GC-traced items, shared by protoScala, protoClojure and protoST ([platform/PMQ-SPEC.md](platform/PMQ-SPEC.md)). Its GC strategy is settled with the maintainer in the P2 plan's Task 0. **Consumed since 0.3.0 through the `Mailbox` seam** (`src/runtime/Mailbox.h`), which compiles onto `ProtoMPSCQueue` when the linked protoCore provides it and onto a CAS'd `ProtoList` otherwise; protoCore 2.0.0 does not, so 0.3.0 ships the fallback and `protoscala --version` names the backend in use | correctness / throughput | decided; P2 |
| R8 | Tagged-pointer budget: 37 of 64 pointer tags and 11 of 16 embedded types are free today; P1 and P2 take one tag each (35 left); every new protoCore type must justify a tag | platform longevity | platform specs |

---

## 12. Implementation phases

Summarised here; milestones, done-when criteria and tracks are in
[ROADMAP.md](ROADMAP.md); task-level plans are in [plans/](plans/).

| Phase | Content |
|---|---|
| 0 | Repository skeleton, build against protoCore, test harnesses, `--version` |
| P1 *(platform)* | `ProtoMap` in protoCore (+ hashed-collection helper), tests, full rebuild of every embedder |
| P2 *(platform)* | `ProtoMPSCQueue` in protoCore (GC-traced lock-free mailbox), tests, microbenchmarks, full rebuild of every embedder |
| 1 | Lexer (with offside rule), parser, AST, core compiler + VM (expressions, `val`/`var`/`def`, `if`/`while`, lambdas), `println`, REPL |
| 2 | Classes, objects, traits + linearization, case classes, universal `apply`, for-comprehensions, pattern matching, plain `super` (§4.4), the `Option` prelude and a minimal `List` |
| 3 | SmallInteger fast paths, `List`/`Vector`/`Map`/`Set`/`Range`, prelude (`Option`, `Either`, `Try`), string interpolation |
| 4 | Exceptions, `super` in stackable traits, enums and sealed hierarchies, named/default arguments |
| 5 | Actors, priority bands, cooperative futures |
| 6 | UMD provider and prefix routing, CPack `.deb`/`.tgz` |
| C *(platform)* | protoClojure: maps/sets onto `ProtoMap`; vectors off `ProtoTuple` (R2); actor mailboxes onto `ProtoMPSCQueue` |
| S *(platform)* | protoST: `Dictionary`/`Set` onto `ProtoMap` (unblocks its D32 decision); actor mailboxes onto `ProtoMPSCQueue` |
