# protoScala Phase 2 — Object Model, `apply`, `for`, `match` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Idiomatic Scala data modelling on protoCore: classes (constructor parameters, `val`/`var` fields, methods, auxiliary constructors), traits with Scala linearization (unit-tested against `scalac`-documented examples), singleton `object`s and companions, case classes and case objects with every synthesised member, tuples as case classes, the universal `apply` rule (and `update`, setters), for-comprehensions over `List` (and over any value with `map`/`flatMap`/`withFilter`/`foreach`), pattern matching with every pattern of DESIGN §5.3, `isInstanceOf`/`asInstanceOf` — in brace and indentation syntax, from scripts and the REPL. Release 0.2.0.

**Architecture:** The frontend parses templates, patterns and enumerators into new AST nodes; `Desugar` rewrites `for` into `map`/`flatMap`/`withFilter`/`foreach`, synthesises case-class companions and expands pattern `val`s; a pure `Linearizer` computes Scala's class linearization on type keys; the compiler keeps a compile-time type namespace (`ClassInfo` in `GlobalTable`) and emits a class as one `MAKE_CLASS` instruction that builds an **immutable prototype whose protoCore parent chain is exactly the linearization**, so ordinary `getAttribute` dispatch follows Scala's rules (DESIGN §4.3). Methods are compiled functions that receive the receiver in slot 0; instances are `newChild` of the class prototype (immutable, or mutable when the class or an ancestor declares a `var`); case-class members, tuples and `List` operations are native primitives driven by per-class metadata; `Option` lives in a small Scala prelude compiled at start-up.

**Tech Stack:** C++20, CMake ≥ 3.20, protoCore (shared lib, `../protoCore/build_release`), GoogleTest 1.14, libreadline; Scala 3.9.0 (`../tools/scala3-3.9.0`, optional) only as a reference for expected outputs and the benchmark JVM column.

**Spec:** docs/DESIGN.md §1–§6 (+ docs/LANGUAGE.md §2–§3, docs/ROADMAP.md "Phase 2", Documentation track)

## Global Constraints

Every Phase 1 constraint still holds (plans/2026-09-22-phase-1-core-language.md, "Global Constraints"); repeated here with the Phase 2 additions:

- C++20, CMake ≥ 3.20; every target builds with `-Wall -Wextra -Wpedantic` and **no warnings**.
- DESIGN §1.1 P1–P6 are binding. In particular P1: no `std::` container holds a `ProtoObject*` across an allocation; the only `ProtoObject*` a C++ structure may keep are interned symbols (perennial) and the runtime prototypes pinned in root-context slots. Every new runtime value (class prototypes, instances, tuples, lists being built) lives in a `ProtoContext` slot or in a protoCore structure reachable from one; the class prototypes live in the globals object.
- P2: one `ProtoContext` per invocation; helpers that allocate several objects in a row open a child context and hand the result back through `returnValue` (the pattern of `ExecutionEngine::callNative`, `src/runtime/ExecutionEngine.cpp:60-70`).
- P3/P5: no protoCore change in this phase. Where protoCore's API is insufficient (the `isInstanceOf` traversal limits, Design note 5) the plan uses protoCore attributes and records the platform question for the maintainer.
- P4: every departure from Scala 3 gets a stable id. Phase 2 adds **D28–D34** (listed in Task 17) and extends D5 and D10; each is recorded in `docs/STATUS.md` and `docs/LANGUAGE.md` §5 and cross-referenced from tutorial chapter 3.
- P6: no new stop-the-world work. Class prototypes are **immutable** (no mutables-tree entry); only instances of classes with `var` fields, lazy-val holders and the singleton holders of `object`s are mutable.
- **Never map transient data to `ProtoTuple`** (DESIGN §4.6, R2): Scala tuples are case-class instances of `Tuple2`..`Tuple22`; argument packs and field lists are `ProtoList`s.
- `PROTO_NONE` is Scala `null` *and* "attribute missing": presence is probed with `hasOwnAttribute`/`hasAttribute`, never by comparing a lookup result with `PROTO_NONE` alone.
- Interned symbols are per `ProtoSpace`; never cache them in function-local `static`s. New symbols go in `RuntimeLayout` (created in `Runtime::Runtime`) or in `BytecodeModule` constants (interned by `linkSymbols`).
- Nothing is written outside `/home/gamarino/Documentos/proyectos`: no `/tmp`, no `$HOME`; tests create temporaries in the CTest working directory; ad-hoc scratch goes to `../.agent_scratch/phase2/`. When `scalac` is run to confirm an expected output, run it with `HOME=../.agent_scratch/phase2/jvm/.home JAVA_OPTS="-XX:-UsePerfData -Djava.io.tmpdir=../.agent_scratch/phase2/jvm/tmp"` and `-d ../.agent_scratch/phase2/jvm/out`.
- All code comments, messages and documentation in professional English. User-facing messages never mention internal phase names.
- Conformance fixtures: first line is a directive (`// EXPECT: <last stdout line>`, `// EXPECT-ERROR[: <substring>]`, `// XFAIL...`); one CTest case per file; files starting with `_` are helpers. **Every fixture whose syntax differs between braces and indentation exists in both variants** (`-braces` / `-indent` suffixes).
- Commits use the repository's configured git identity (never `-c user.*` or `--author`), end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`, go to `main` and are pushed (`git push origin main`; maintainer-authorised for this phase). Never force-push, never rewrite published history, never create the `v0.2.0` tag (the maintainer tags).
- Build and test only with: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release` (run from `protoScala/`). protoCore is not modified in this phase, so no clean rebuild of embedders is needed.

---

## Preflight (before Task 1)

- [ ] **Step 1: Confirm the Phase 1 baseline is committed and green**

Run: `cd /home/gamarino/Documentos/proyectos/protoScala && git status --short && git log --oneline | head -3`
Expected: an empty status (a clean tree) and `10bfb84 docs: positioning — ...` (or a later commit) on top. If the tree is not clean, **stop and ask the maintainer**; do not commit someone else's changes.

Run: `cmake -B build_release -S . && cmake --build build_release && ctest --test-dir build_release`
Expected: `100% tests passed` (335 tests on 2026-09-22: 204 unit, 105 conformance, 18 CLI, 8 benchmark smoke).

- [ ] **Step 2: Record the protoCore commit the phase is built against**

Run: `git -C ../protoCore log --oneline | head -1`
Expected: one line; paste it into the Task 17 CHANGELOG entry ("built against protoCore `<hash>`").

- [ ] **Step 3: Confirm the Phase 2 surface is still rejected (the failing starting point)**

```bash
mkdir -p ../.agent_scratch/phase2
printf 'class P(val x: Int)\nprintln(new P(1).x)\n' > ../.agent_scratch/phase2/probe.scala
build_release/protoscala ../.agent_scratch/phase2/probe.scala
```
Expected: `probe.scala:1:1: error: 'class' definitions are not implemented yet`, exit 1.

---

## Design notes that apply to several tasks

These are consequences of the spec plus facts found in the code (file:line references are to the trees of 2026-09-22). Choices that are design decisions are listed in **Open questions for the maintainer** at the end, each with the provisional behaviour this plan implements.

1. **protoCore attribute lookup walks the receiver's own flattened chain.** `ProtoObject::getAttribute` (protoCore `core/ProtoObject.cpp:537-704`) probes the receiver's own attributes, then the object of each `ParentLink` of **the receiver's** link list in order (`nextLink = currentLink->parent`, lines 690-698); it never descends into a parent's own parents. `newChild` (lines 398-423) builds the child's list as "link to the receiver" + **the receiver handle's** `oc->parent`. Therefore an instance created with `C->newChild(ctx)` walks `[C, <C's own chain>...]`: if `C`'s own chain is exactly `L(C)` minus `C` (installed with `setParents`), instance dispatch follows Scala's linearization with no further work (DESIGN §4.3). The chain must be **complete in C's handle cell**, which is why class prototypes are built as immutable shapes (note 2).
2. **Why class prototypes are immutable.** For a *mutable* object, `setParents`/`addParent` change only the snapshot in the mutables tree, while `newChild` copies the handle's born-with chain (`oc->parent`, not the snapshot): a mutable class whose parents are set after creation gives instances the old chain. protoST hit exactly this (its STATUS D21: "`newChild` copies that frozen base chain; a later `addParent`/`setParents` on the (mutable) class ... is invisible to the class's instances"). `MAKE_CLASS` therefore builds `anyProto->newChild(ctx, false)`, replaces its chain with `setParents(ctx, chain)` (immutable branch: a fresh cell carrying exactly `chain`, `core/ProtoObject.cpp:1247-1300`), then adds every member with `setAttribute` (immutable: each call returns a new cell with the same parent chain, lines 706-830). The final cell is the class prototype. Nothing mutates it afterwards, so it never enters the mutables tree (P6) and its lookups skip snapshot resolution. Task 5 pins these protoCore facts in unit tests (`ObjectModel.*`) before anything relies on them.
3. **`ExecutionEngine::send` cannot call Scala methods today.** `src/runtime/ExecutionEngine.cpp:87-108` handles native methods and plain attributes and throws `methods written in Scala on objects are not implemented yet` for anything else (lines 105-107). Phase 2 replaces it with `dispatch`, which passes the receiver as argument 0 of a compiled method (`BytecodeModule::isMethod()`), and keeps `send` as the entry point for native code.
4. **`show`, `valuesEqual` and `typeName` treat every object as `<object>` / identity** (`src/runtime/Values.cpp:121-193`). Phase 2 makes them dispatch to the Scala `toString`/`equals`/`hashCode` of an instance through `activeCallContext()` (the engine is always active while Scala code runs, `ExecutionEngine.cpp:17-28,47-58`), and adds `ExecutionEngine::showTopLevel` for the two places that print outside a run (REPL echo, the unit-test harness).
5. **protoCore `isInstanceOf` has traversal limits.** `ProtoObject::isInstanceOf` (`core/ProtoObject.cpp:437-525`) walks prototype chains depth-first from `getPrototype`, stops after **50 visited objects** (returning `PROTO_FALSE`) and keeps at most **64** pending siblings (silently dropping the rest). With protoScala's flattened chains every class revisits the chains of its ancestors, so a class-membership test can give a false negative for hierarchies with about ten ancestors. The plan tests class membership with a **marker attribute** instead: every class prototype carries an own attribute whose key is its own type key (`@Point` → `PROTO_TRUE`), and `x` is an instance of `C` iff `x->getAttribute(ctx, @C) == PROTO_TRUE` — one cached chain walk, no allocation, exact, bounded only by R3 (500 steps). DESIGN §5.3 names `isInstanceOf`; this is Open question Q2.
6. **Values allocated in a context live as long as that context.** A `ProtoContext` keeps the cells allocated in it young until it is destroyed; its `returnValue` is re-rooted in the previous context on destruction (protoCore `core/ProtoContext.cpp:239-256`). Helpers that allocate in a loop (building a list, calling a function per element) therefore use one short-lived child context per step and hand the step's result back through `returnValue` — the pattern of `list_foreach` (`src/runtime/Primitives.cpp:630-646`) and protoClojure's `src/runtime/ListBuilder.h`. While a builder's own context is open it is the innermost context of the thread: nested calls take it (or a child of it) as their parent, never the builder's parent.
7. **The compiler has one namespace today.** `GlobalTable` (`src/compiler/GlobalTable.h:97-150`) maps term names to runtime global keys (`x`, `x#1`, ...). Phase 2 adds a **type namespace** in the same table: a class, trait or the class of an `object` gets a type key `@Name` (or `@Name#N` when a REPL input shadows it). `@` and `#` never occur in a Scala identifier, so type keys, term keys and user attribute names never collide. The prototype of a class is stored in the globals object under its type key; a term `C` (a companion object) under its term key. The compile-time description of a type is a `ClassInfo` (Task 4).
8. **Private members are class-qualified attribute keys.** A `private` member `x` of the class with type key `@Account` is stored under the attribute key `Account::x` (`::` after an alphanumeric identifier can never be one Scala identifier). Code compiled inside `Account` or its companion uses that key; code anywhere else can only name `x`, which the instance does not have, so the access fails with `NoSuchMethodError: value x is not a member of Account`. This is D5's "private enforced as a lookup restriction on the defining class" with no run-time check (Open question Q4).
9. **Methods have no captures.** Templates are top-level only in Phase 2 (Open question Q6), so a method body reaches its members through `this` (slot 0) and every other name is a global: methods are compiled detached from any enclosing function (`FunctionState::parent == nullptr`). Lambdas inside a method capture `this` like any other local (`Compiler::captureInto`, `src/compiler/Compiler.cpp:309-319`).
10. **Construction of immutable instances.** `new C(a)` creates `C->newChild(ctx, mutable)` and calls the constructor method `<init>` with it in slot 0. Each field store (`STORE_FIELD`) replaces slot 0 with `slot0->setAttribute(k, v)`: for an immutable instance that is a new version of the object, for a mutable one the same handle. The constructor returns the final `this`, which `NEW` pushes. Superclass and trait initialisers are methods called the same way (`INVOKE_INIT`), in Scala's order (verified with `scalac` 3.9.0: `class C extends B with T1 with T2`, with `B extends A`, prints `init A`, `init B`, `init T1`, `init T2`, `init C`). A reference to `this` that escapes before the last field is stored observes an earlier version (D28, Open question Q1).
11. **Scala facts used as expected outputs** were confirmed with `scalac`/`java` from `../tools/scala3-3.9.0` (run in `.agent_scratch`, not in the tree): `Point(1, 2).hashCode == -694993394`, `Box("abc").hashCode == -1480185351`, `(1, "a").hashCode == 1971805870`, `(1, 2).hashCode == 1316541600`, `"abc".hashCode == 96354`, `Unique.hashCode == -1756661775` (case object), `Empty().hashCode == 67081517`; `(1,a)` and `(1,2,3)` are the tuple `toString`s; `Point(1,2).copy(y = 5)` prints `Point(1,5)`; `Point.unapply(p)` returns `p`; case classes have `_1`.. accessors; `3L.isInstanceOf[Int]` is `false` (D29 here); the `MatchError` message is `5 (of class java.lang.Integer)` / `Point(1,2) (of class Point)`; `for (x <- List(1,2,3) if { println("f" + x); true }) println("x" + x)` prints `f1 x1 f2 x2 f3 x3` (`withFilter` is lazy); `for { x <- List(1,2); y <- List(10,20) if x + y != 21 } yield x * y` is `List(10, 20, 40)`; the stackable `super` chain of Note 10's classes yields `T2>T1>B>A`. The hash algorithm (Scala 3's synthesised `hashCode`: MurmurHash3 with seed `0xcafebabe`, `productPrefix.hashCode` mixed first, then each element's `##`, then `finalizeHash(h, arity)`; arity 0 → `productPrefix.hashCode`) reproduces every value above (checked with a reference computation).
12. **Lessons carried over:** disassemble before blaming caches (`--disassemble`); compiler scope stacks stay `std::deque` (protoST D29); REPL modules are retained for the session (`src/repl/Session.h:55-60`); `readline`'s `RETURN` macro stays `#undef`ined; benchmarks self-report and are verified; GC-pressure runs use `PROTOCORE_HEAP_LIMIT_CELLS`.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/frontend/AST.h` / `AST.cpp` (modify) | New nodes `TemplateDef`, `New`, `Match`, `For`; `Pattern`, `CaseDef`, `Enumerator`, `Modifiers`, `ParentRef`; class-parameter flags on `Param`; `ValDef::pattern`, `ValDef::mods`, `DefDef::mods`/`synthetic`; dump formats; `destroyTree` for the new nodes |
| `src/frontend/Parser.h` / `Parser.cpp` (modify) | Templates (both syntaxes), modifiers, type-parameter clauses with variance and bounds, `new`, `this`, `super.m`, `match` and patterns, `{ case ... }` literals, `for` enumerators, placeholder syntax `_`, pattern `val`s, abstract members |
| `src/frontend/Linearizer.h` / `Linearizer.cpp` (new) | Pure Scala linearization (SLS 5.1.2) on type keys (DESIGN §3.1 places it in the frontend) |
| `src/frontend/Desugar.cpp` (modify) | `for` → `map`/`flatMap`/`withFilter`/`foreach`; case-class companions (`apply`, `unapply`); pattern `val` expansion; `obj.x = v` / `a(i) = v` / `obj.x op= v`; traversal of the new nodes |
| `src/compiler/ClassInfo.h` / `ClassInfo.cpp` (new) | `ClassInfo`, `MemberInfo`, key helpers, the built-in types (`Any`, `AnyRef`, `Product`, `Serializable`, `Tuple2`..`Tuple22`) |
| `src/compiler/GlobalTable.h` (modify) | Type namespace (`declareType`, `defineType`, `findType`, `findTypeByKey`), `BindingKind::Object` |
| `src/compiler/Opcodes.h` (modify) | Opcodes 64–77 and `TypeCode` |
| `src/compiler/BytecodeModule.h` / `.cpp` (modify) | Constant kinds `Names`, `ClassSpec`, `SuperSite`, `KwSendSite`; `isMethod` flag; symbol linking and disassembly of the new constants and opcodes |
| `src/compiler/Compiler.h` / `Compiler.cpp` (modify) | Template-aware name resolution, `this`, assignments to members, tuples, named-argument sends, type tests, unit-level hoisting of templates |
| `src/compiler/CompileTemplates.cpp` (new) | `Compiler` members for templates: class infos, `MAKE_CLASS`, methods, constructors, setters, lazy members, `new`, `super` |
| `src/compiler/CompilePatterns.cpp` (new) | `Compiler` members for `match`: the decision cascade and every pattern kind |
| `src/runtime/Runtime.h` / `Runtime.cpp` (modify) | `anyRefProto`, `productProto`, `serializableProto`, `withFilterProto`, `listCompanion`, `Tuple2`..`Tuple22` prototypes and their keys; new interned keys; binding of the built-in type keys in the globals object |
| `src/runtime/Hashing.h` (new) | Pure MurmurHash3 (Scala's `productHash`, `seqHash`), Java `String.hashCode`, Scala `##` of numbers |
| `src/runtime/Values.h` / `Values.cpp` (modify) | Scala-instance detection, `show`/`valuesEqual`/`typeName` dispatch, `scalaHash` (`##`), default `toString` |
| `src/runtime/ExecutionEngine.h` / `.cpp` (modify) | `dispatch`/`callMember` (Scala methods, bound methods, lazy members, fields), the new opcodes, `construct`, `showTopLevel` |
| `src/runtime/PrimitiveSupport.h` (new) | The argument helpers and `PRIM` macro moved out of `Primitives.cpp`, shared by the primitive files |
| `src/runtime/Primitives.h` / `Primitives.cpp` (modify) | `Any` members (`hashCode`, `##`, instance-aware `toString`/`equals`), `hashCode` of primitives, `List`/`Nil` globals and the Phase 2 `List` methods, `WithFilter`, `__raise` |
| `src/runtime/ProductPrimitives.cpp` (new) | Case-class members (`toString`, `equals`, `hashCode`, `productArity`, `productElement`, `productPrefix`, `copy`, `_1`..`_22`), tuple constructors |
| `lib/prelude.scala` (new) | `Option`, `Some`, `None` in protoScala |
| `src/support/PreludeSource.cpp.in` (new), `src/runtime/Prelude.h` / `Prelude.cpp` (new) | The prelude embedded in the binary and compiled at session start |
| `src/repl/Session.h` / `Session.cpp` (modify) | Built-in types, prelude, echo of templates, `showTopLevel` for results |
| `tests/unit/EvalHarness.h` (modify) | Built-in types, prelude, `showTopLevel` |
| `tests/unit/test_linearizer.cpp`, `test_objectmodel.cpp`, `test_hashing.cpp` (new); `test_parser.cpp`, `test_desugar.cpp`, `test_compiler.cpp`, `test_bytecode.cpp`, `test_engine.cpp`, `test_primitives.cpp` (modify) | Unit tests |
| `tests/conformance/07-classes/`, `08-case-classes/`, `09-apply/`, `10-lists/`, `11-pattern-matching/`, `12-for-comprehensions/`, `tests/conformance/tutorial/02-*`, `03-*`, `06-*`, `07-*`, `09-*` (new) | Fixtures |
| `tests/cli/repl-classes.sh` (new), `tests/cli/gc-pressure.sh` (modify), `tests/CMakeLists.txt` (modify) | CLI checks |
| `benchmarks/comparable/attr_lookup.scala`, `object_tree.scala`, `benchmarks/comparable/python/object_tree.py` (new); `benchmarks/run_benchmarks.py`, `benchmarks/README.md`, `benchmarks/RESULTS.md` (modify); `benchmarks/reports/<date>-phase2.md` (generated) | Benchmarks |
| `docs/TUTORIAL.md`, `docs/tutorial/06-classes-objects-and-traits.md`, `07-case-classes-and-pattern-matching.md`, `09-for-comprehensions.md` (new), `02-…`, `03-…`, `05-…` (modify) | Tutorial |
| `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `CHANGELOG.md`, `README.md`, `CMakeLists.txt` | Status, deviations D28–D34, opcode table, version 0.2.0 |

`CMakeLists.txt` gains `src/frontend/Linearizer.cpp` (frontend), `src/compiler/ClassInfo.cpp`, `CompileTemplates.cpp`, `CompilePatterns.cpp` (compiler), `src/runtime/ProductPrimitives.cpp`, `src/runtime/Prelude.cpp` and the generated `PreludeSource.cpp` (runtime). The library order is unchanged: `frontend ← compiler ← runtime ← repl ← protoscala`.

### Opcodes added in this phase (reserved range 64..95, `src/compiler/Opcodes.h:56`)

| # | Name | Stack effect | Operand / notes |
|---|---|---|---|
| 64 | `MAKE_CLASS` | `[p1..pk m1..mn] -> [cls]` | `ClassSpec` constant: k parents (the runtime chain), n member values in the order of the spec's member keys |
| 65 | `NEW` | `[cls a1..an] -> [obj]` | `SendSite` (constructor key `<init>` or `<init>N`, n) |
| 66 | `INVOKE_INIT` | `[cls this a1..an] -> [this']` | `SendSite` (constructor key, n): runs `cls`'s own initialiser on `this` |
| 67 | `STORE_FIELD` | `[v] -> []` | `Symbol` attribute key; `slot[0] = slot[0].setAttribute(key, v)` (constructors only) |
| 68 | `SET_FIELD` | `[obj v] -> []` | `Symbol` attribute key; `obj` must be mutable (setters only) |
| 69 | `SEND_SUPER` | `[this a1..an] -> [r]` | `SuperSite` (name, n, defining type key): DESIGN §4.4 |
| 70 | `TEST_TYPE` | `[v] -> [Boolean]` | `TypeCode` (built-in types) |
| 71 | `TEST_PROTO` | `[v] -> [Boolean]` | `Symbol` type key: class membership through the marker attribute (Design note 5) |
| 72 | `UNAPPLY_FIELDS` | `[v] -> [f1..fn]` | `Names` constant: the attribute keys, read with `getOwnAttributeDirect` |
| 73 | `UNCONS` | `[list] -> [head tail]` | non-empty `List`: `getAt(0)` and `removeFirst` |
| 74 | `MATCH_ERROR` | `[v] -> ⊥` | throws `MatchError: <v> (of class <T>)` |
| 75 | `CAST_FAIL` | `[v] -> ⊥` | `String` constant (target type): throws `ClassCastException` |
| 76 | `MAKE_TUPLE` | `[a1..an] -> [t]` | n in 2..22 |
| 77 | `SEND_KW` | `[recv a1..an v1..vm] -> [r]` | `KwSendSite` (name, n, keyword names): named arguments to native methods |
| 78 | `NEW_SPREAD` | `[cls a1..an list] -> [obj]` | `SendSite` (constructor key, n): `new C(a, xs*)`, the list's elements follow a1..an (added by Task 7) |
| 79..95 | reserved | | |

---

### Task 1: AST and parser for templates (`class`, `trait`, `object`, `case`), `new`, `this`, `super`

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/AST.cpp`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`
- Test: `tests/unit/test_parser.cpp`

**Interfaces:**
- Consumes: the Phase 1 parser (`Parser::parseDefinition`, `parseDefDef`, `parseValDef`, `parseParamClause`, `parseSimple`, `checkEndMarker`; `src/frontend/Parser.cpp:767-925,454-527,690-703`).
- Produces:
  - `struct Modifiers { bool isPrivate, isProtected, isOverride, isAbstract, isFinal, isSealed; }`.
  - `enum class TemplateKind : uint8_t { Class, Trait, Object }`, `struct ParentRef { TypePtr type; std::vector<NodePtr> args; bool hasArgs; SourcePos pos; }`, `struct TemplateDef : Node` (`NodeKind::TemplateDef`), `struct New : Node` (`NodeKind::New`).
  - `Param` gains `bool isVal, isVar; Modifiers mods;` (class parameters); `ValDef` gains `Modifiers mods; PatternPtr pattern;` (the pattern is filled by Task 2); `DefDef` gains `Modifiers mods; bool synthetic;`; a `DefDef` or `ValDef` with a null body/rhs inside a template is an **abstract member**; a `DefDef` named `this` is an **auxiliary constructor**.
  - `this` parses as `Ident("this")`; `super.m` as `Select(Ident("super"), "m")` (the compiler validates both).
  - Dump formats (documented in `AST.cpp`'s header comment):
    `(class [abstract] [final] [sealed] Name [[A B]] [(<class-param>...)] [(extends <parent>...)] [(self s)] (body <stat>...))` with the kind word `class`, `case-class`, `trait`, `object` or `case-object`; a class parameter renders as `[private ]val x:Int`, `[private ]var x:Int` or `x:Int` (plus ` = <default>`); a parent renders as `T` or `(T <arg>...)` when it has an argument list; `(new T <arg>...)`; `ValDef`/`DefDef` modifiers render as ` private`/` override`/` abstract` right after `(val`/`(def` (only the ones set, in that order).

- [ ] **Step 1: Write the failing parser tests**

Append to `tests/unit/test_parser.cpp`:

```cpp
namespace {
std::string u(const std::string& src) { return dump(*protoScala::parseSource(src)); }

std::string parseErrorOf(const std::string& src) {
    try {
        protoScala::parseSource(src);
    } catch (const ParseError& err) {
        return err.what();
    }
    return "";
}
} // namespace

TEST(ParserTemplates, ClassWithParametersBothSyntaxes) {
    const std::string expected =
        "(unit (class Point (val x:Int var y:Int z:Int) (body "
        "(def sum (infix + x y)) (val scaled (infix * x z)))))";
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int) {\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"
                "}\n"),
              expected);
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int):\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"),
              expected);
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int):\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"
                "end Point\n"),
              expected);
    EXPECT_EQ(u("class Empty"), "(unit (class Empty (body)))");
    EXPECT_EQ(u("class Unit0()"), "(unit (class Unit0 () (body)))");
}

TEST(ParserTemplates, CaseClassesObjectsAndTraits) {
    EXPECT_EQ(u("case class P(x: Int, y: Int)"), "(unit (case-class P (x:Int y:Int) (body)))");
    EXPECT_EQ(u("case object Nada"), "(unit (case-object Nada (body)))");
    EXPECT_EQ(u("object O:\n  val a = 1\n  def f(n: Int) = n + a\n"),
              "(unit (object O (body (val a (int 1)) (def f ((n:Int)) (infix + n a)))))");
    EXPECT_EQ(u("trait Shape {\n  def area: Double\n  def describe = \"area \" + area\n}"),
              "(unit (trait Shape (body (def area : Double) "
              "(def describe (infix + (str \"area \") area)))))");
    EXPECT_EQ(u("sealed abstract class Expr"), "(unit (class abstract sealed Expr (body)))");
    EXPECT_EQ(u("final case class Box[+A](value: A)"),
              "(unit (case-class final Box [A] (value:A) (body)))");
}

TEST(ParserTemplates, ExtendsWithArgumentsAndMixins) {
    EXPECT_EQ(u("class C(n: Int) extends B(n, 2) with T1 with T2"),
              "(unit (class C (n:Int) (extends (B n (int 2)) T1 T2) (body)))");
    EXPECT_EQ(u("class C extends B, T1, T2"), "(unit (class C (extends B T1 T2) (body)))");
    EXPECT_EQ(u("class C(x: Int)\n    extends B(x)\n    with T:\n  def f = 1\n"),
              "(unit (class C (x:Int) (extends (B x) T) (body (def f (int 1)))))");
    EXPECT_EQ(u("case object None extends Option[Nothing]"),
              "(unit (case-object None (extends Option[Nothing]) (body)))");
    EXPECT_EQ(u("trait Greeter(val greeting: String)"),
              "(unit (trait Greeter (val greeting:String) (body)))");
}

TEST(ParserTemplates, MembersModifiersAndAuxiliaryConstructors) {
    EXPECT_EQ(u("class A {\n  private val secret = 1\n  override def toString = \"A\"\n"
                "  abstract override def put(x: Int) = super.put(x)\n}"),
              "(unit (class A (body (val private secret (int 1)) "
              "(def override toString (str \"A\")) "
              "(def override abstract put ((x:Int)) (apply (. super put) x)))))");
    EXPECT_EQ(u("class R(val n: Int, val d: Int):\n  def this(n: Int) = this(n, 1)\n"),
              "(unit (class R (val n:Int val d:Int) (body (def this ((n:Int)) "
              "(apply this n (int 1))))))");
    EXPECT_EQ(u("class A(private val k: Int)"), "(unit (class A (private val k:Int) (body)))");
    EXPECT_EQ(u("abstract class Q { val size: Int }"),
              "(unit (class abstract Q (body (val size : Int))))");
}

TEST(ParserTemplates, NewThisSuperAndSelfAlias) {
    EXPECT_EQ(e("new Point(1, 2)"), "(new Point (int 1) (int 2))");
    EXPECT_EQ(e("new Point(1, 2).x"), "(. (new Point (int 1) (int 2)) x)");
    EXPECT_EQ(e("new Box[Int](3)"), "(new Box[Int] (int 3))");
    EXPECT_EQ(e("new Empty"), "(new Empty)");
    EXPECT_EQ(e("this.x"), "(. this x)");
    EXPECT_EQ(e("super.toString"), "(. super toString)");
    EXPECT_EQ(u("class A { self =>\n  def me = self\n}"),
              "(unit (class A (self self) (body (def me self))))");
}

TEST(ParserTemplates, Errors) {
    EXPECT_NE(parseErrorOf("case class P").find("case class must have a parameter list"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("new T { def f = 1 }").find("anonymous classes are not implemented yet"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("class A(x: Int)(y: Int)")
                  .find("multiple constructor parameter lists are not implemented yet"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("def f: Int").find("'=' expected"), std::string::npos);  // outside a template
    EXPECT_NE(parseErrorOf("super.f[Int]; super[T].f").find("super[T]"), std::string::npos);
    EXPECT_NE(parseErrorOf("case 1 => 2").find("'case'"), std::string::npos);
}

TEST(ParserTemplates, IncompleteTemplatesAskForMoreInput) {
    EXPECT_TRUE(parseErrorOf("class A {").find("unexpected end of input") == 0 ||
                parseErrorOf("class A {").find("unclosed") != std::string::npos);
    EXPECT_NE(parseErrorOf("class A:\n").find("indented template body"), std::string::npos);
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R ParserTemplates --output-on-failure`
Expected: FAIL — `'class' definitions are not implemented yet` and friends.

- [ ] **Step 2: Extend the AST**

In `src/frontend/AST.h`, extend `NodeKind` (append after `Import`, keeping the Phase 1 values):

```cpp
enum class NodeKind : uint8_t {
    IntLit, FloatLit, StringLit, CharLit, BoolLit, NullLit, UnitLit, InterpString,
    Ident, Select, Apply, TypeApply, Infix, Prefix, Assign, If, While, Return,
    Block, Lambda, Typed, Parens, Tuple, Splice, NamedArg,
    ValDef, DefDef, Import,
    TemplateDef, New, Match, For,   // Phase 2
};
```

Add before `struct Param`:

```cpp
// Modifiers of a definition or class parameter. Only `private` changes the
// meaning of a program (D5: class-qualified member keys); the others are
// recorded for the checks the compiler does make (abstract, final, sealed).
struct Modifiers {
    bool isPrivate = false;    // also private[this] and private[pkg]
    bool isProtected = false;
    bool isOverride = false;
    bool isAbstract = false;
    bool isFinal = false;
    bool isSealed = false;
};
```

Extend `Param` (after `SourcePos pos;`):

```cpp
    // Class parameters only: `val x: T`, `var x: T`, and their modifiers.
    bool isVal = false;
    bool isVar = false;
    Modifiers mods;
```

Forward-declare the pattern tree (Task 2 defines it) before `struct ValDef` and extend `ValDef` and `DefDef`:

```cpp
struct Pattern;
using PatternPtr = std::unique_ptr<Pattern>;

struct ValDef : Node {
    ValDef(SourcePos p) : Node(NodeKind::ValDef, p) {}
    ~ValDef() override;             // defined in AST.cpp (Pattern is incomplete here)
    std::string name;               // empty when `pattern` is set
    bool isVar = false;
    bool isLazy = false;
    Modifiers mods;
    TypePtr type;                   // may be null
    NodePtr rhs;                    // null: an abstract member (templates only)
    PatternPtr pattern;             // `val (a, b) = e` (Task 2); null for a simple name
};
```

In `DefDef`, add after `bool curried = false;`:

```cpp
    Modifiers mods;
    bool synthetic = false;  // written by Desugar (case-class companions), not by the user
    // `body` is null for an abstract member (templates only). A DefDef named
    // "this" is an auxiliary constructor (templates only).
```

Add after `struct Import`:

```cpp
enum class TemplateKind : uint8_t { Class, Trait, Object };

// One entry of an `extends` clause: `B(args)`, `T`, `Option[A]`.
struct ParentRef {
    TypePtr type;                 // a Name or Applied type tree
    std::vector<NodePtr> args;    // constructor / trait arguments
    bool hasArgs = false;         // an argument list was written (possibly empty)
    SourcePos pos;
};

// class, trait, object, case class, case object.
struct TemplateDef : Node {
    TemplateDef(SourcePos p) : Node(NodeKind::TemplateDef, p) {}
    TemplateKind kind = TemplateKind::Class;
    bool isCase = false;
    Modifiers mods;
    std::string name;
    std::vector<std::string> typeParams;   // erased
    bool hasParamClause = false;           // `class C()` vs `class C`
    std::vector<Param> ctorParams;         // primary constructor (or trait) parameters
    std::vector<ParentRef> parents;        // the extends clause, superclass first
    std::string selfName;                  // `self =>` alias of `this`, or empty
    std::vector<NodePtr> body;             // template statements
    bool synthetic = false;                // a companion object created by Desugar
};

// new T(args)
struct New : Node {
    New(SourcePos p) : Node(NodeKind::New, p) {}
    TypePtr type;
    std::vector<NodePtr> args;
    bool hasArgs = false;
};
```

`Match` and `For` are declared by Task 2; until then `NodeKind::Match` and `NodeKind::For` have no node struct and are never created.

- [ ] **Step 3: Rendering and non-recursive destruction of the new nodes**

In `src/frontend/AST.cpp`, document the formats of the Interfaces section in the header comment, add `ValDef::~ValDef() = default;` after the includes (Task 2 includes the complete `Pattern` there), and add to `render` (with helpers):

```cpp
void renderMods(std::string& out, const Modifiers& m) {
    if (m.isPrivate) out += " private";
    if (m.isOverride) out += " override";
    if (m.isAbstract) out += " abstract";
}

void renderClassParam(std::string& out, const Param& p) {
    if (p.mods.isPrivate) out += "private ";
    if (p.isVal) out += "val ";
    if (p.isVar) out += "var ";
    renderParam(out, p);
}

void renderParent(std::string& out, const ParentRef& p) {
    if (!p.hasArgs) {
        renderType(out, *p.type);
        return;
    }
    out += '(';
    renderType(out, *p.type);
    renderChildren(out, p.args);
    out += ')';
}
```

```cpp
        case NodeKind::TemplateDef: {
            const auto& x = as<TemplateDef>(n);
            out += '(';
            switch (x.kind) {
                case TemplateKind::Class:  out += x.isCase ? "case-class" : "class"; break;
                case TemplateKind::Trait:  out += "trait"; break;
                case TemplateKind::Object: out += x.isCase ? "case-object" : "object"; break;
            }
            if (x.mods.isAbstract) out += " abstract";
            if (x.mods.isFinal) out += " final";
            if (x.mods.isSealed) out += " sealed";
            out += ' ' + x.name;
            if (!x.typeParams.empty()) {
                out += " [";
                for (std::size_t k = 0; k < x.typeParams.size(); ++k) {
                    if (k) out += ' ';
                    out += x.typeParams[k];
                }
                out += ']';
            }
            if (x.hasParamClause) {
                out += " (";
                for (std::size_t k = 0; k < x.ctorParams.size(); ++k) {
                    if (k) out += ' ';
                    renderClassParam(out, x.ctorParams[k]);
                }
                out += ')';
            }
            if (!x.parents.empty()) {
                out += " (extends";
                for (const ParentRef& p : x.parents) {
                    out += ' ';
                    renderParent(out, p);
                }
                out += ')';
            }
            if (!x.selfName.empty()) out += " (self " + x.selfName + ")";
            out += " (body";
            renderChildren(out, x.body);
            out += "))";
            break;
        }
        case NodeKind::New: {
            const auto& x = as<New>(n);
            out += "(new ";
            renderType(out, *x.type);
            renderChildren(out, x.args);
            out += ')';
            break;
        }
```

In the `ValDef` case, call `renderMods(out, x.mods)` right after the opening word (before the name, with the name then preceded by a space as today: `(val private secret (int 1))`); in the `DefDef` case call it right after `"(def"` and the annotations. Both leave Phase 1 dumps unchanged (no modifiers set).

In `releaseChildren` add:

```cpp
        case NodeKind::TemplateDef: {
            auto& t = as<TemplateDef>(n);
            takeParams(t.ctorParams);
            for (auto& p : t.parents) for (auto& a : p.args) take(a);
            for (auto& s : t.body) take(s);
            return;
        }
        case NodeKind::New: for (auto& a : as<New>(n).args) take(a); return;
```

- [ ] **Step 4: Parser declarations**

In `src/frontend/Parser.h`, add to the private section:

```cpp
    // Templates (Phase 2)
    bool inTemplateBody_ = false;  // parsing the statements of a template body (abstract members allowed)
    Modifiers parseModifiers(bool* isLazy);
    NodePtr parseTemplateDef(Modifiers mods);           // at `case`, `class`, `trait` or `object`
    std::vector<Param> parseClassParamClause();         // after the name: `(` ... `)`
    std::vector<ParentRef> parseParents();              // after `extends`
    std::vector<NodePtr> parseTemplateBody(std::string* selfName);  // `{...}` or `:` + indented block
    NodePtr parseTemplateStat();
    std::vector<std::string> parseTypeParams();         // `[` ... `]`, variance and bounds erased
    NodePtr parseNew();                                 // at `new`
```

- [ ] **Step 5: Modifiers, definitions and type parameters**

In `src/frontend/Parser.cpp`, replace the modifier loop of `parseDefinition` (lines 788-808) by a call to `parseModifiers` and dispatch templates:

```cpp
// Hard and soft modifiers before a definition. `lazy` is reported apart;
// implicit/given are rejected (D3).
Modifiers Parser::parseModifiers(bool* isLazy) {
    Modifiers mods;
    for (;;) {
        const Token& t = peek();
        if (t.kind == TokenKind::KwImplicit || t.kind == TokenKind::KwGiven)
            fail(kImplicitsUnsupported, t);
        if (t.kind == TokenKind::KwLazy) {
            *isLazy = true;
            advance();
            continue;
        }
        if (!isHardModifier(t.kind) && !isSoftModifier(t)) break;
        switch (t.kind) {
            case TokenKind::KwPrivate:   mods.isPrivate = true; break;
            case TokenKind::KwProtected: mods.isProtected = true; break;
            case TokenKind::KwOverride:  mods.isOverride = true; break;
            case TokenKind::KwAbstract:  mods.isAbstract = true; break;
            case TokenKind::KwFinal:     mods.isFinal = true; break;
            case TokenKind::KwSealed:    mods.isSealed = true; break;
            default: break;              // soft modifiers: accepted and ignored
        }
        const bool qualifiable =
            t.kind == TokenKind::KwPrivate || t.kind == TokenKind::KwProtected;
        advance();
        if (qualifiable && at(TokenKind::LBracket)) {  // private[this], private[pkg]
            advance();
            if (at(TokenKind::KwThis)) advance();
            else expect(TokenKind::Identifier, "access qualifier");
            expect(TokenKind::RBracket, "']'");
        }
    }
    return mods;
}
```

In `parseDefinition`, after the annotations:

```cpp
    bool isLazy = false;
    const Modifiers mods = parseModifiers(&isLazy);
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::KwVal: {
            advance();
            NodePtr v = parseValDef(t.pos, false, isLazy);
            as<ValDef>(*v).mods = mods;
            return v;
        }
        case TokenKind::KwVar: {
            if (isLazy) fail("'lazy' is not allowed on a var", t);
            advance();
            NodePtr v = parseValDef(t.pos, true, false);
            as<ValDef>(*v).mods = mods;
            return v;
        }
        case TokenKind::KwDef: {
            if (isLazy) fail("'lazy' is not allowed on a def", t);
            advance();
            NodePtr d = parseDefDef(t.pos, std::move(annotations));
            as<DefDef>(*d).mods = mods;
            return d;
        }
        case TokenKind::KwImport:
            if (isLazy || !annotations.empty()) fail("an import takes no modifiers", t);
            return parseImport();
        case TokenKind::KwCase:
            if (peek(1).kind == TokenKind::KwClass || peek(1).kind == TokenKind::KwObject) {
                if (isLazy) fail("'lazy' is not allowed on a class or object", t);
                return parseTemplateDef(mods);
            }
            fail("'case' is only allowed in a match or in a pattern-matching function "
                 "literal `{ case ... }`", t);
        case TokenKind::KwClass: case TokenKind::KwObject: case TokenKind::KwTrait:
            if (isLazy) fail("'lazy' is not allowed on a class, trait or object", t);
            return parseTemplateDef(mods);
        case TokenKind::KwEnum: case TokenKind::KwType: case TokenKind::KwPackage:
        case TokenKind::KwExport:
            notImplemented("'" + t.text + "' definitions", t);
        default:
            if (isExtensionStart(t, peek(1))) notImplemented("'extension' definitions", t);
            fail("definition expected but '" + spelling(t) + "' found", t);
    }
```

Type parameters (used by `def` and templates; replaces the loop of `parseDefDef`, lines 897-909):

```cpp
// `[` TypeParam {`,` TypeParam} `]`. Names are kept; variance (`+A`, `-A`),
// higher-kinded parameters (`F[_]`), bounds (`<:`, `>:`) and context bounds
// (`: Ordering`) are parsed and erased (DESIGN §2).
std::vector<std::string> Parser::parseTypeParams() {
    expect(TokenKind::LBracket, "'['");
    std::vector<std::string> names;
    while (!at(TokenKind::RBracket)) {
        if (atIdent("+") || atIdent("-")) advance();
        if (at(TokenKind::Underscore)) {
            advance();
            names.push_back("_");
        } else {
            names.push_back(expect(TokenKind::Identifier, "type parameter").text);
        }
        if (at(TokenKind::LBracket)) {  // F[_]: the arity of a type constructor
            int depth = 0;
            do {
                if (at(TokenKind::EndOfFile)) fail("']' expected", peek());
                if (at(TokenKind::LBracket)) ++depth;
                if (at(TokenKind::RBracket)) --depth;
                advance();
            } while (depth > 0);
        }
        while (at(TokenKind::Subtype) || at(TokenKind::Supertype) || at(TokenKind::Colon)) {
            advance();
            parseType();
        }
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RBracket, "']'");
    return names;
}
```

`parseDefDef` (lines 893-925) becomes:

```cpp
NodePtr Parser::parseDefDef(SourcePos pos, std::vector<std::string> annotations) {
    auto node = std::make_unique<DefDef>(pos);
    node->annotations = std::move(annotations);
    if (at(TokenKind::KwThis) && inTemplateBody_) {  // auxiliary constructor
        advance();
        node->name = "this";
    } else {
        node->name = expect(TokenKind::Identifier, "method name").text;
    }
    if (at(TokenKind::LBracket)) node->typeParams = parseTypeParams();
    while (at(TokenKind::LParen) && !peek().firstOnLine)
        node->paramLists.push_back(parseParamClause());
    if (at(TokenKind::Colon)) {
        advance();
        node->resultType = parseType();
    }
    if (at(TokenKind::LBrace))
        fail("procedure syntax is not supported in Scala 3; write `def " + node->name +
                 "(): Unit = ...`",
             peek());
    if (!at(TokenKind::Equals)) {
        // An abstract member: only in a template body, never for a constructor.
        if (inTemplateBody_ && node->name != "this") return node;
        fail("'=' expected: abstract methods are only allowed in classes, traits and objects",
             peek());
    }
    advance();
    node->body = parseExprOrIndented();
    return node;
}
```

In `parseValDef` (lines 840-860), accept an abstract `val x: T` inside a template body: replace the `if (!at(TokenKind::Equals))` check by

```cpp
    if (!at(TokenKind::Equals)) {
        if (inTemplateBody_ && node->type) return node;  // abstract member
        fail("'=' expected: a value definition needs an initialiser", peek());
    }
```

(Task 2 replaces the "patterns in val definitions" rejection at lines 845-848.)

`parseBlockBody` (lines 651-672) saves `inTemplateBody_`, sets it to `false` for the block and restores it before returning, so a `def` without body inside a method body is still an error.

- [ ] **Step 6: Templates**

```cpp
// [case] (class | trait | object) Name [TypeParams] [ConstrMods] [ParamClause]
// [extends Parents] [derives Types] [TemplateBody]
NodePtr Parser::parseTemplateDef(Modifiers mods) {
    const Token& start = peek();
    auto node = std::make_unique<TemplateDef>(start.pos);
    node->mods = mods;
    if (at(TokenKind::KwCase)) {
        advance();
        node->isCase = true;
    }
    switch (peek().kind) {
        case TokenKind::KwClass:  node->kind = TemplateKind::Class; break;
        case TokenKind::KwTrait:  node->kind = TemplateKind::Trait; break;
        case TokenKind::KwObject: node->kind = TemplateKind::Object; break;
        default: fail("'class', 'trait' or 'object' expected", peek());
    }
    advance();
    node->name = expect(TokenKind::Identifier, "class name").text;
    if (at(TokenKind::LBracket)) node->typeParams = parseTypeParams();
    if (node->kind != TemplateKind::Object) {
        // `class C private (x: Int)`: a constructor access modifier (advisory, D5).
        if ((at(TokenKind::KwPrivate) || at(TokenKind::KwProtected)) &&
            peek(1).kind == TokenKind::LParen) {
            advance();
        }
        if (at(TokenKind::LParen) && !peek().firstOnLine) {
            node->hasParamClause = true;
            node->ctorParams = parseClassParamClause();
            if (at(TokenKind::LParen) && !peek().firstOnLine)
                unsupported("multiple constructor parameter lists", peek());
        }
    }
    if (node->isCase && node->kind == TemplateKind::Class && !node->hasParamClause)
        fail("A case class must have a parameter list; write `case class " + node->name +
                 "()` or a case object",
             start);
    if (at(TokenKind::KwExtends)) {
        advance();
        node->parents = parseParents();
    }
    if (atIdent("derives")) {  // type-class derivation: parsed and ignored (D3)
        advance();
        parseType();
        while (at(TokenKind::Comma)) {
            advance();
            parseType();
        }
    }
    // A template body on the same line, or `{` on the next line (Scala allows a
    // newline before a template body).
    if (at(TokenKind::Newline) && peek(1).kind == TokenKind::LBrace) advance();
    if (at(TokenKind::LBrace) || at(TokenKind::ColonEol) ||
        (at(TokenKind::Colon) && peek(1).kind == TokenKind::EndOfFile))
        node->body = parseTemplateBody(&node->selfName);
    return node;
}

// `(` [ClassParam {`,` ClassParam}] `)`;
// ClassParam ::= {Modifier} [`val` | `var`] id `:` Type [`=` Expr]
std::vector<Param> Parser::parseClassParamClause() {
    expect(TokenKind::LParen, "'('");
    if (at(TokenKind::KwImplicit) || (atIdent("using") && peek(1).kind != TokenKind::Colon))
        fail(kImplicitsUnsupported, peek());
    std::vector<Param> params;
    while (!at(TokenKind::RParen)) {
        Param p;
        p.pos = peek().pos;
        bool lazyIgnored = false;
        p.mods = parseModifiers(&lazyIgnored);
        if (lazyIgnored) fail("'lazy' is not allowed on a class parameter", peek());
        if (at(TokenKind::KwVal)) {
            advance();
            p.isVal = true;
        } else if (at(TokenKind::KwVar)) {
            advance();
            p.isVar = true;
        }
        p.name = expect(TokenKind::Identifier, "parameter name").text;
        expect(TokenKind::Colon, "':' and a parameter type");
        p.type = parseType();
        p.byName = p.type->kind == TypeTree::Kind::ByName;
        if (atIdent("*")) {
            advance();
            p.repeated = true;
        }
        if (at(TokenKind::Equals)) {
            advance();
            p.defaultValue = parseExpr();
        }
        params.push_back(std::move(p));
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    return params;
}

// Parents ::= ConstrApp {(`with` | `,`) ConstrApp}; ConstrApp ::= SimpleType [ArgumentExprs]
std::vector<ParentRef> Parser::parseParents() {
    std::vector<ParentRef> out;
    for (;;) {
        ParentRef p;
        p.pos = peek().pos;
        p.type = parseSimpleType();
        if (at(TokenKind::LParen) && !peek().firstOnLine) {
            advance();
            p.args = parseArgs();
            p.hasArgs = true;
            if (at(TokenKind::LParen) && !peek().firstOnLine)
                unsupported("multiple constructor argument lists", peek());
        }
        out.push_back(std::move(p));
        if (at(TokenKind::KwWith) || at(TokenKind::Comma)) {
            advance();
            continue;
        }
        return out;
    }
}

// `{` [SelfAlias] stats `}`  |  `:` Indent [SelfAlias] stats Outdent
std::vector<NodePtr> Parser::parseTemplateBody(std::string* selfName) {
    TokenKind terminator;
    if (at(TokenKind::LBrace)) {
        advance();
        terminator = TokenKind::RBrace;
    } else {
        // ColonEol, or a plain `:` at the very end of the input (`class A:` typed
        // at the REPL): both need an indented body; at end of input the error
        // is "unexpected end of input", so the REPL asks for more lines.
        if (!at(TokenKind::ColonEol) && !at(TokenKind::Colon)) fail("template body expected", peek());
        advance();
        if (!at(TokenKind::Indent)) fail("an indented template body is expected after ':'", peek());
        advance();
        terminator = TokenKind::Outdent;
    }
    while (skipOneNewline()) {}
    // Self alias: `self =>`, `self: T =>`, `this: T =>` (the type is erased).
    bool alias = false;
    if ((at(TokenKind::Identifier) || at(TokenKind::KwThis)) && peek(1).kind == TokenKind::Arrow) {
        if (at(TokenKind::Identifier)) *selfName = peek().text;
        advance();
        advance();
        alias = true;
    } else if ((at(TokenKind::Identifier) || at(TokenKind::KwThis)) &&
               peek(1).kind == TokenKind::Colon) {
        const std::size_t save = i_;
        const std::string name = at(TokenKind::Identifier) ? peek().text : "";
        advance();
        advance();
        bool isAlias = false;
        try {
            parseInfixType();
            isAlias = at(TokenKind::Arrow);
        } catch (const ParseError&) {
            isAlias = false;
        }
        if (isAlias) {
            advance();
            *selfName = name;
            alias = true;
        } else {
            i_ = save;  // an ordinary statement `x: T` (a typed expression)
        }
    }
    // `{ self =>` followed by members on deeper lines: Layout opened an
    // indented region after `=>`; the members end with its Outdent.
    const bool aliasRegion = alias && at(TokenKind::Indent);
    if (aliasRegion) advance();
    const TokenKind statsEnd = aliasRegion ? TokenKind::Outdent : terminator;
    const bool saved = inTemplateBody_;
    inTemplateBody_ = true;
    std::vector<NodePtr> stats;
    for (;;) {
        while (skipOneNewline()) {}
        if (at(statsEnd)) break;
        if (at(TokenKind::EndOfFile)) fail("", peek());
        if (at(TokenKind::EndMarker)) {
            if (stats.empty())
                fail("misaligned end marker: 'end " + peek().text +
                     "' does not close a preceding construct", peek());
            checkEndMarker(*stats.back(), peek());
            advance();
            continue;
        }
        stats.push_back(parseTemplateStat());
        const TokenKind k = peek().kind;
        if (k != TokenKind::Newline && k != TokenKind::Semicolon && k != statsEnd)
            fail("';' or newline expected but '" + spelling(peek()) + "' found", peek());
    }
    inTemplateBody_ = saved;
    if (aliasRegion) {
        expect(TokenKind::Outdent, "end of template body");
        while (skipOneNewline()) {}
    }
    expect(terminator, terminator == TokenKind::RBrace ? "'}'" : "end of template body");
    return stats;
}

NodePtr Parser::parseTemplateStat() {
    if (atDefinitionStart()) return parseDefinition({});
    return parseExpr();
}

// new T | new T(args) | new T[A](args). Anonymous class bodies are not
// supported (Open question Q6).
NodePtr Parser::parseNew() {
    auto node = std::make_unique<New>(expect(TokenKind::KwNew, "'new'").pos);
    node->type = parseSimpleType();
    if (at(TokenKind::LParen) && !peek().firstOnLine) {
        advance();
        node->args = parseArgs();
        node->hasArgs = true;
    }
    if (at(TokenKind::LParen) && !peek().firstOnLine)
        unsupported("multiple constructor argument lists", peek());
    if ((at(TokenKind::LBrace) && !peek().firstOnLine) || at(TokenKind::ColonEol) ||
        at(TokenKind::KwWith))
        unsupported("anonymous classes", peek());
    return node;
}
```

`unsupported(feature, at)` (`Parser.cpp:125-127`) appends " is not implemented yet". Give it a second parameter, `bool plural = false`, that selects " are not implemented yet" (declaration in `Parser.h`: `[[noreturn]] void unsupported(const std::string& feature, const Token& at, bool plural = false) const;`); the call sites for "anonymous classes", "multiple constructor parameter lists" and "multiple constructor argument lists" pass `true`, so the messages read "anonymous classes are not implemented yet" and so on.

In `parseSimple` (lines 518-522), replace the four `unsupported` lines:

```cpp
        case TokenKind::KwNew:
            base = parseNew();
            break;
        case TokenKind::KwThis:
            base = std::make_unique<Ident>(t.pos, "this");
            advance();
            break;
        case TokenKind::KwSuper:
            advance();
            if (at(TokenKind::LBracket)) unsupported("super[T] (a qualified super call)", peek());
            if (!at(TokenKind::Dot)) fail("'.' expected after 'super'", peek());
            base = std::make_unique<Ident>(t.pos, "super");  // the compiler checks the context
            break;
```

(`Underscore` and `KwMatch` are handled by Task 2.)

In `checkEndMarker` add `case NodeKind::TemplateDef: ok = d == as<TemplateDef>(previous).name; break;`.

- [ ] **Step 7: Run the tests**

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Parser' --output-on-failure`
Expected: all `Parser*` tests PASS (the Phase 1 parser tests are unchanged).

Run: `ctest --test-dir build_release`
Expected: 100% pass. (Programs with templates now fail in the compiler with `definition used as an expression` — Task 6 compiles them.)

- [ ] **Step 8: Commit**

```bash
git add src/frontend/AST.h src/frontend/AST.cpp src/frontend/Parser.h src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "parser: classes, traits, objects, case classes, new, this, super

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 2: Parser for `match`, patterns, `{ case ... }`, `for`, placeholders and pattern `val`s

**Files:**
- Modify: `src/frontend/AST.h`, `src/frontend/AST.cpp`, `src/frontend/Parser.h`, `src/frontend/Parser.cpp`
- Test: `tests/unit/test_parser.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `struct Pattern { Kind kind; SourcePos pos; std::string name; NodePtr expr; TypePtr type; std::vector<PatternPtr> args; }` with `Kind { Wildcard, Var, Literal, Stable, Typed, Bind, Alt, Extractor, Tuple, SeqWildcard }`; `struct CaseDef { PatternPtr pattern; NodePtr guard; NodePtr body; SourcePos pos; }`; `struct Match : Node { NodePtr scrutinee; std::vector<CaseDef> cases; }`; `struct Enumerator { Kind { Generator, Guard, Value }; PatternPtr pattern; NodePtr expr; SourcePos pos; }`; `struct For : Node { std::vector<Enumerator> enums; NodePtr body; bool isYield; }`; `std::string dump(const Pattern&)`; `PatternPtr clonePattern(const Pattern&)` and `NodePtr cloneSimpleExpr(const Node&)` (literals, identifiers, selections — used by Desugar).
  - Placeholder sections: `_` inside an expression becomes a fresh parameter `_$N` of a lambda wrapping the smallest enclosing *Expr* that properly contains it (SLS 6.23.2).
  - `{ case p => e ... }` is `Lambda(x$N => Match(x$N, cases))`.
  - Dump formats: pattern `_`, `x`, a literal as its expression, `(stable <path>)`, `(: <pat> <type>)`, `(@ x <pat>)`, `(| <pat>...)`, `(unapply <path> <pat>...)`, `(tuple-pat <pat>...)`, `_*` / `(_* rest)`; `(match <scrutinee> (case <pat> [(if <guard>)] <body>)...)`; `(for-yield|for-do (<- <pat> <expr>) (if <expr>) (= <pat> <expr>) <body>)`; `(val-pat <pat> <rhs>)` / `(var-pat ...)`.

- [ ] **Step 1: Failing tests**

Append to `tests/unit/test_parser.cpp`:

```cpp
TEST(ParserPatterns, MatchBothSyntaxes) {
    const std::string expected =
        "(match x (case (int 1) (str \"one\")) (case n (if (infix > n (int 1))) (str \"many\")) "
        "(case _ (str \"none\")))";
    EXPECT_EQ(e("x match {\n  case 1 => \"one\"\n  case n if n > 1 => \"many\"\n"
                "  case _ => \"none\"\n}"),
              expected);
    EXPECT_EQ(e("x match { case 1 => \"one\" case n if n > 1 => \"many\" case _ => \"none\" }"),
              expected);
    EXPECT_EQ(dump(*protoScala::parseSource(
                  "val r = x match\n  case 1 => \"one\"\n  case n if n > 1 => \"many\"\n"
                  "  case _ => \"none\"\nprintln(r)\n")),
              "(unit (val r " + expected + ") (apply println r))");
    // A multi-line case body is an indented block.
    EXPECT_EQ(e("x match\n  case 1 =>\n    val y = 2\n    y\n  case _ => 0"),
              "(match x (case (int 1) (block (val y (int 2)) y)) (case _ (int 0)))");
}

TEST(ParserPatterns, EveryPatternKind) {
    auto p = [](const std::string& pat) {
        const std::string d = e("v match { case " + pat + " => 0 }");
        // "(match v (case <pat> (int 0)))": 15 characters before <pat>, 10 after it
        return d.substr(15, d.size() - 15 - 10);
    };
    EXPECT_EQ(p("-1"), "(int -1)");
    EXPECT_EQ(p("\"s\""), "(str \"s\")");
    EXPECT_EQ(p("'c'"), "(char 'c')");
    EXPECT_EQ(p("true"), "true");
    EXPECT_EQ(p("null"), "null");
    EXPECT_EQ(p("()"), "()");
    EXPECT_EQ(p("_"), "_");
    EXPECT_EQ(p("x"), "x");
    EXPECT_EQ(p("Nil"), "(stable Nil)");
    EXPECT_EQ(p("`x`"), "(stable x)");
    EXPECT_EQ(p("Color.Red"), "(stable (. Color Red))");
    EXPECT_EQ(p("i: Int"), "(: i Int)");
    EXPECT_EQ(p("_: List[Int]"), "(: _ List[Int])");
    EXPECT_EQ(p("p @ Point(x, _)"), "(@ p (unapply Point x _))");
    EXPECT_EQ(p("1 | 2 | 3"), "(| (int 1) (int 2) (int 3))");
    EXPECT_EQ(p("(a, b)"), "(tuple-pat a b)");
    EXPECT_EQ(p("(a)"), "a");
    EXPECT_EQ(p("h :: t"), "(unapply :: h t)");
    EXPECT_EQ(p("a :: b :: rest"), "(unapply :: a (unapply :: b rest))");
    EXPECT_EQ(p("List(a, _*)"), "(unapply List a _*)");
    EXPECT_EQ(p("List(a, rest*)"), "(unapply List a (_* rest))");
    EXPECT_EQ(p("List(a, rest @ _*)"), "(unapply List a (_* rest))");
    EXPECT_EQ(p("Some(Point(1, y))"), "(unapply Some (unapply Point (int 1) y))");
    EXPECT_EQ(p("Empty()"), "(unapply Empty)");
}

TEST(ParserPatterns, CaseLambdasAndPatternVals) {
    EXPECT_EQ(e("xs.map { case (a, b) => a + b }"),
              "(apply (. xs map) (lambda (x$1) (match x$1 (case (tuple-pat a b) "
              "(infix + a b)))))");
    EXPECT_EQ(dump(*protoScala::parseSource("val (a, b) = pair\nvar h :: t = xs")),
              "(unit (val-pat (tuple-pat a b) pair) (var-pat (unapply :: h t) xs))");
    EXPECT_EQ(dump(*protoScala::parseSource("val Point(x, y) = p")),
              "(unit (val-pat (unapply Point x y) p))");
}

TEST(ParserPatterns, ForComprehensions) {
    const std::string yield =
        "(for-yield (<- x xs) (if (infix > x (int 1))) (<- y ys) (= z (infix * x y)) "
        "(infix + z (int 1)))";
    EXPECT_EQ(e("for (x <- xs if x > 1; y <- ys; z = x * y) yield z + 1"), yield);
    EXPECT_EQ(e("for { x <- xs if x > 1\n y <- ys\n z = x * y } yield z + 1"), yield);
    EXPECT_EQ(e("for\n  x <- xs if x > 1\n  y <- ys\n  z = x * y\nyield z + 1"), yield);
    EXPECT_EQ(e("for x <- xs do println(x)"), "(for-do (<- x xs) (apply println x))");
    EXPECT_EQ(e("for (x <- xs) println(x)"), "(for-do (<- x xs) (apply println x))");
    EXPECT_EQ(e("for ((a, b) <- ps) yield a"), "(for-yield (<- (tuple-pat a b) ps) a)");
    EXPECT_EQ(e("for (Some(v) <- os) yield v"), "(for-yield (<- (unapply Some v) os) v)");
}

TEST(ParserPatterns, PlaceholderSyntax) {
    EXPECT_EQ(e("_ + 1"), "(lambda (_$1) (infix + _$1 (int 1)))");
    EXPECT_EQ(e("xs.map(_ * 2)"), "(apply (. xs map) (lambda (_$1) (infix * _$1 (int 2))))");
    EXPECT_EQ(e("xs.map(_.toString)"), "(apply (. xs map) (lambda (_$1) (. _$1 toString)))");
    EXPECT_EQ(e("f(_)"), "(lambda (_$1) (apply f _$1))");
    EXPECT_EQ(e("_ + _"), "(lambda (_$1 _$2) (infix + _$1 _$2))");
    EXPECT_EQ(e("xs.foreach(println(_))"),
              "(apply (. xs foreach) (lambda (_$1) (apply println _$1)))");
}

TEST(ParserPatterns, PatternErrors) {
    EXPECT_NE(parseErrorOf("x match { }").find("'case' expected"), std::string::npos);
    EXPECT_NE(parseErrorOf("x match\n1").find("'{' or an indented block of cases"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("for (x <- xs)").find("unexpected end of input"), std::string::npos);
    EXPECT_NE(parseErrorOf("for x <- xs println(x)").find("'do' or 'yield' expected"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("lazy val (a, b) = p").find("lazy pattern definitions"),
              std::string::npos);
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R ParserPatterns --output-on-failure`
Expected: FAIL (`match is not implemented yet`, ...).

- [ ] **Step 2: The pattern, match and for nodes**

In `src/frontend/AST.h`, after `struct New`:

```cpp
// A pattern (SLS 8). `expr` holds the literal of a Literal pattern and the
// path (Ident / Select) of a Stable or Extractor pattern; `args` the
// sub-patterns (Typed and Bind: exactly one).
struct Pattern {
    enum class Kind : uint8_t {
        Wildcard,     // _
        Var,          // x
        Literal,      // 1, "s", 'c', true, null, ()
        Stable,       // Nil, `x`, Color.Red: matched with ==
        Typed,        // x: T, _: T
        Bind,         // x @ p
        Alt,          // p1 | p2
        Extractor,    // C(p1, ...), h :: t
        Tuple,        // (p1, p2, ...)
        SeqWildcard,  // _* , rest*, rest @ _* (last argument of a sequence extractor)
    };
    Kind kind = Kind::Wildcard;
    SourcePos pos;
    std::string name;              // Var, Bind, SeqWildcard binder ("" for `_*`)
    NodePtr expr;
    TypePtr type;                  // Typed
    std::vector<PatternPtr> args;
};

struct CaseDef {
    PatternPtr pattern;
    NodePtr guard;   // may be null
    NodePtr body;
    SourcePos pos;
};

struct Match : Node {
    Match(SourcePos p) : Node(NodeKind::Match, p) {}
    NodePtr scrutinee;
    std::vector<CaseDef> cases;
};

struct Enumerator {
    enum class Kind : uint8_t { Generator, Guard, Value };  // p <- e | if c | p = e
    Kind kind = Kind::Generator;
    PatternPtr pattern;   // null for a guard
    NodePtr expr;
    SourcePos pos;
};

struct For : Node {
    For(SourcePos p) : Node(NodeKind::For, p) {}
    std::vector<Enumerator> enums;
    NodePtr body;
    bool isYield = false;
};

std::string dump(const Pattern& p);

// Deep copies used by Desugar (a for-comprehension pattern appears in the
// withFilter lambda and in the map lambda). cloneSimpleExpr copies literal,
// Ident and Select trees only (what a pattern contains) and throws
// std::logic_error for anything else.
NodePtr cloneSimpleExpr(const Node& n);
PatternPtr clonePattern(const Pattern& p);
```

- [ ] **Step 3: Rendering, cloning and destruction**

In `src/frontend/AST.cpp`:

```cpp
void renderPattern(std::string& out, const Pattern& p) {
    using K = Pattern::Kind;
    switch (p.kind) {
        case K::Wildcard: out += '_'; return;
        case K::Var: out += p.name; return;
        case K::Literal: render(out, *p.expr); return;
        case K::Stable:
            out += "(stable ";
            render(out, *p.expr);
            out += ')';
            return;
        case K::Typed:
            out += "(: ";
            renderPattern(out, *p.args[0]);
            out += ' ';
            renderType(out, *p.type);
            out += ')';
            return;
        case K::Bind:
            out += "(@ " + p.name + ' ';
            renderPattern(out, *p.args[0]);
            out += ')';
            return;
        case K::Alt: case K::Extractor: case K::Tuple:
            out += p.kind == K::Alt ? "(|" : p.kind == K::Tuple ? "(tuple-pat" : "(unapply ";
            if (p.kind == K::Extractor) render(out, *p.expr);
            for (const PatternPtr& a : p.args) {
                out += ' ';
                renderPattern(out, *a);
            }
            out += ')';
            return;
        case K::SeqWildcard:
            out += p.name.empty() ? "_*" : "(_* " + p.name + ")";
            return;
    }
}
```

`render` gains:

```cpp
        case NodeKind::Match: {
            const auto& x = as<Match>(n);
            out += "(match ";
            render(out, *x.scrutinee);
            for (const CaseDef& c : x.cases) {
                out += " (case ";
                renderPattern(out, *c.pattern);
                if (c.guard) {
                    out += " (if ";
                    render(out, *c.guard);
                    out += ')';
                }
                out += ' ';
                render(out, *c.body);
                out += ')';
            }
            out += ')';
            break;
        }
        case NodeKind::For: {
            const auto& x = as<For>(n);
            out += x.isYield ? "(for-yield" : "(for-do";
            for (const Enumerator& en : x.enums) {
                using EK = Enumerator::Kind;
                out += en.kind == EK::Generator ? " (<- " : en.kind == EK::Guard ? " (if " : " (= ";
                if (en.pattern) {
                    renderPattern(out, *en.pattern);
                    out += ' ';
                }
                render(out, *en.expr);
                out += ')';
            }
            out += ' ';
            render(out, *x.body);
            out += ')';
            break;
        }
```

and the `ValDef` case renders a pattern definition as `(val-pat <pat> <rhs>)` / `(var-pat ...)` when `x.pattern` is set. Define `ValDef::~ValDef() = default;` here (after the complete `Pattern`). `dump(const Pattern&)` wraps `renderPattern`.

Cloning:

```cpp
NodePtr cloneSimpleExpr(const Node& n) {
    switch (n.kind) {
        case NodeKind::IntLit: {
            const auto& x = as<IntLit>(n);
            auto c = std::make_unique<IntLit>(x.pos);
            c->value = x.value; c->fitsLong = x.fitsLong; c->digits = x.digits; c->base = x.base;
            return c;
        }
        case NodeKind::FloatLit: {
            auto c = std::make_unique<FloatLit>(n.pos);
            c->value = as<FloatLit>(n).value; c->text = as<FloatLit>(n).text;
            return c;
        }
        case NodeKind::StringLit: {
            auto c = std::make_unique<StringLit>(n.pos);
            c->value = as<StringLit>(n).value;
            return c;
        }
        case NodeKind::CharLit: {
            auto c = std::make_unique<CharLit>(n.pos);
            c->value = as<CharLit>(n).value;
            return c;
        }
        case NodeKind::BoolLit: return std::make_unique<BoolLit>(n.pos, as<BoolLit>(n).value);
        case NodeKind::NullLit: return std::make_unique<NullLit>(n.pos);
        case NodeKind::UnitLit: return std::make_unique<UnitLit>(n.pos);
        case NodeKind::Ident: return std::make_unique<Ident>(n.pos, as<Ident>(n).name);
        case NodeKind::Select: {
            const auto& s = as<Select>(n);
            return std::make_unique<Select>(s.pos, cloneSimpleExpr(*s.qualifier), s.name);
        }
        default:
            throw std::logic_error("cloneSimpleExpr: not a literal or a path");
    }
}

PatternPtr clonePattern(const Pattern& p) {
    auto c = std::make_unique<Pattern>();
    c->kind = p.kind;
    c->pos = p.pos;
    c->name = p.name;
    if (p.expr) c->expr = cloneSimpleExpr(*p.expr);
    if (p.type) c->type = cloneType(*p.type);
    for (const PatternPtr& a : p.args) c->args.push_back(clonePattern(*a));
    return c;
}
```

with a local `TypePtr cloneType(const TypeTree&)` (copies kind, name, pos and args recursively). `releaseChildren` takes the scrutinee, every guard and body of a `Match` (patterns are shallow and are freed normally), and every enumerator expression and the body of a `For`.

- [ ] **Step 4: Parser declarations**

Add to `Parser.h` (private):

```cpp
    // Placeholder syntax: one frame per Expr being parsed (SLS 6.23.2).
    std::vector<std::vector<std::string>> placeholderFrames_;
    int placeholderCounter_ = 0;
    int caseLambdaCounter_ = 0;
    NodePtr parseExprNoPlaceholders();   // the Phase 1 parseExpr body
    NodePtr placeholder(SourcePos pos);  // at `_`
    // Pattern matching and for-comprehensions
    NodePtr parseMatch(NodePtr scrutinee);                // at `match`
    CaseDef parseCaseClause(TokenKind terminator);
    NodePtr parseCaseBody(TokenKind terminator);
    NodePtr parseCaseLambda(SourcePos pos, TokenKind terminator);  // at the first `case`
    PatternPtr parsePattern();                            // p1 | p2 | ...
    PatternPtr parsePattern1();                           // typed patterns
    PatternPtr parsePattern2();                           // x @ p
    PatternPtr parseInfixPattern(int minPrec, int assocPrec = -1);
    PatternPtr parseSimplePattern();
    std::vector<PatternPtr> parsePatternArgs();           // after `(`, up to and including `)`
    NodePtr parseFor();                                   // at `for`
    void parseEnumerators(For& f, TokenKind terminator);
    Enumerator parseGeneratorOrValue();
```

- [ ] **Step 5: Placeholders**

Rename the Phase 1 `Parser::parseExpr` to `parseExprNoPlaceholders` and add:

```cpp
// Expr, with placeholder sections: a `_` that this Expr properly contains
// (and no inner Expr does) becomes a parameter of a lambda wrapping it;
// a bare `_` belongs to the enclosing Expr (`f(_)` is `x => f(x)`).
NodePtr Parser::parseExpr() {
    placeholderFrames_.emplace_back();
    struct Pop {
        std::vector<std::vector<std::string>>& frames;
        ~Pop() { frames.pop_back(); }
    } pop{placeholderFrames_};
    NodePtr e = parseExprNoPlaceholders();
    std::vector<std::string> names = std::move(placeholderFrames_.back());
    if (names.empty()) return e;
    if (e->kind == NodeKind::Ident && names.size() == 1 && as<Ident>(*e).name == names[0]) {
        if (placeholderFrames_.size() < 2)
            fail("unbound placeholder '_': write an explicit lambda", peek());
        placeholderFrames_[placeholderFrames_.size() - 2].push_back(names[0]);
        return e;
    }
    auto lambda = std::make_unique<Lambda>(e->pos);
    for (std::string& n : names) {
        Param p;
        p.name = std::move(n);
        p.pos = e->pos;
        lambda->params.push_back(std::move(p));
    }
    lambda->body = std::move(e);
    return lambda;
}

NodePtr Parser::placeholder(SourcePos pos) {
    if (placeholderFrames_.empty()) fail("unbound placeholder '_'", peek());
    std::string name = "_$" + std::to_string(++placeholderCounter_);
    placeholderFrames_.back().push_back(name);
    return std::make_unique<Ident>(pos, std::move(name));
}
```

In `parseSimple`, the `Underscore` case becomes `base = placeholder(t.pos); advance(); break;`. (`lambdaAhead` already claims `_ =>`; `_*` in argument lists is still a `Typed`/`Splice` via `parseType`.)

- [ ] **Step 6: `match`, cases and case lambdas**

In `parseExprNoPlaceholders`, replace `if (at(TokenKind::KwMatch)) unsupported("match", peek());` (line 217) by

```cpp
    while (at(TokenKind::KwMatch)) e = parseMatch(std::move(e));
```

and remove the `KwMatch` line of `parseSimple` (line 522). Then:

```cpp
// e match { cases } | e match <Indent> cases <Outdent>. A case at the same
// indentation as `match` is not supported (D23).
NodePtr Parser::parseMatch(NodePtr scrutinee) {
    auto m = std::make_unique<Match>(scrutinee->pos);
    m->scrutinee = std::move(scrutinee);
    advance();  // match
    TokenKind terminator;
    if (at(TokenKind::LBrace)) {
        advance();
        terminator = TokenKind::RBrace;
    } else if (at(TokenKind::Indent)) {
        advance();
        terminator = TokenKind::Outdent;
    } else {
        fail("'{' or an indented block of cases expected after 'match'", peek());
    }
    while (skipOneNewline()) {}
    if (!at(TokenKind::KwCase)) fail("'case' expected", peek());
    while (at(TokenKind::KwCase)) {
        m->cases.push_back(parseCaseClause(terminator));
        while (skipOneNewline()) {}
    }
    expect(terminator, terminator == TokenKind::RBrace ? "'}'" : "end of the cases");
    return m;
}

// case Pattern [if Guard] => Block
CaseDef Parser::parseCaseClause(TokenKind terminator) {
    CaseDef c;
    c.pos = expect(TokenKind::KwCase, "'case'").pos;
    c.pattern = parsePattern();
    if (at(TokenKind::KwIf)) {
        advance();
        c.guard = parseInfix(0);
    }
    expect(TokenKind::Arrow, "'=>'");
    c.body = parseCaseBody(terminator);
    return c;
}

// The statements after `=>`, up to the next `case` or the end of the cases.
NodePtr Parser::parseCaseBody(TokenKind terminator) {
    if (at(TokenKind::Indent)) return parseIndentedBlock();
    auto block = std::make_unique<Block>(peek().pos);
    const bool saved = inTemplateBody_;
    inTemplateBody_ = false;
    for (;;) {
        const TokenKind k = peek().kind;
        if (k == TokenKind::KwCase || k == terminator || k == TokenKind::EndOfFile ||
            k == TokenKind::EndMarker)
            break;
        if (k == TokenKind::Newline || k == TokenKind::Semicolon) {
            const TokenKind next = peek(1).kind;
            advance();
            if (next == TokenKind::KwCase || next == terminator) break;
            continue;
        }
        block->stats.push_back(parseBlockStat(terminator));
    }
    inTemplateBody_ = saved;
    if (block->stats.size() == 1 && block->stats[0]->kind != NodeKind::ValDef &&
        block->stats[0]->kind != NodeKind::DefDef)
        return std::move(block->stats[0]);
    return block;
}

// `{ case p => e ... }`: a one-parameter function whose body matches its argument (D34).
NodePtr Parser::parseCaseLambda(SourcePos pos, TokenKind terminator) {
    const std::string param = "x$" + std::to_string(++caseLambdaCounter_);
    auto m = std::make_unique<Match>(pos);
    m->scrutinee = std::make_unique<Ident>(pos, param);
    while (at(TokenKind::KwCase)) {
        m->cases.push_back(parseCaseClause(terminator));
        while (skipOneNewline()) {}
    }
    auto lambda = std::make_unique<Lambda>(pos);
    Param p;
    p.name = param;
    p.pos = pos;
    lambda->params.push_back(std::move(p));
    lambda->body = std::move(m);
    return lambda;
}
```

`parseBlockExpr` (lines 636-641) checks for a case lambda first:

```cpp
NodePtr Parser::parseBlockExpr() {
    const SourcePos pos = expect(TokenKind::LBrace, "'{'").pos;
    while (at(TokenKind::Newline)) advance();
    if (at(TokenKind::KwCase)) {
        NodePtr lambda = parseCaseLambda(pos, TokenKind::RBrace);
        expect(TokenKind::RBrace, "'}'");
        return lambda;
    }
    NodePtr block = parseBlockBody(TokenKind::RBrace, pos);
    expect(TokenKind::RBrace, "'}'");
    return block;
}
```

- [ ] **Step 7: Patterns**

```cpp
namespace {
// A variable pattern is a simple lower-case identifier (SLS 8.1.1); a
// back-quoted identifier or an upper-case one is a stable identifier.
bool isVarId(const Token& t) {
    if (t.kind != TokenKind::Identifier || t.backquoted || t.isOperator) return false;
    const unsigned char c = static_cast<unsigned char>(t.text[0]);
    return c == '_' || (c >= 'a' && c <= 'z') || c >= 0x80;
}
PatternPtr makePattern(Pattern::Kind k, SourcePos pos) {
    auto p = std::make_unique<Pattern>();
    p->kind = k;
    p->pos = pos;
    return p;
}
} // namespace

PatternPtr Parser::parsePattern() {
    PatternPtr first = parsePattern1();
    if (!atIdent("|")) return first;
    auto alt = makePattern(Pattern::Kind::Alt, first->pos);
    alt->args.push_back(std::move(first));
    while (atIdent("|")) {
        advance();
        alt->args.push_back(parsePattern1());
    }
    return alt;
}

// x: T | _: T | Pattern2. The type is a simple or tuple type (so that `|`
// still separates alternatives).
PatternPtr Parser::parsePattern1() {
    if ((isVarId(peek()) || at(TokenKind::Underscore)) && peek(1).kind == TokenKind::Colon) {
        const Token& t = advance();
        advance();  // :
        auto typed = makePattern(Pattern::Kind::Typed, t.pos);
        auto inner = makePattern(t.kind == TokenKind::Underscore ? Pattern::Kind::Wildcard
                                                                : Pattern::Kind::Var, t.pos);
        if (t.kind != TokenKind::Underscore) inner->name = t.text;
        typed->args.push_back(std::move(inner));
        typed->type = at(TokenKind::LParen) ? parseType() : parseSimpleType();
        return typed;
    }
    return parsePattern2();
}

PatternPtr Parser::parsePattern2() {
    if (isVarId(peek()) && peek(1).kind == TokenKind::At) {
        const Token& t = advance();
        advance();  // @
        auto bind = makePattern(Pattern::Kind::Bind, t.pos);
        bind->name = t.text;
        bind->args.push_back(parseInfixPattern(0));
        return bind;
    }
    return parseInfixPattern(0);
}

// SimplePattern {op SimplePattern}: `h :: t` is `::(h, t)`; operators ending
// in ':' are right-associative. `|` and a final `*` are not operators here.
PatternPtr Parser::parseInfixPattern(int minPrec, int assocPrec) {
    PatternPtr lhs = parseSimplePattern();
    while (at(TokenKind::Identifier) && peek().isOperator && !peek().backquoted &&
           peek().text != "|" && !(peek().text == "*" && peek(1).kind == TokenKind::RParen)) {
        const Token& opTok = peek();
        const std::string op = opTok.text;
        const int p = precedence(op);
        if (p < minPrec) break;
        const bool right = isRightAssociative(op);
        if (assocPrec == p && !right) break;
        advance();
        PatternPtr rhs = right ? parseInfixPattern(p, p) : parseInfixPattern(p + 1);
        auto ex = makePattern(Pattern::Kind::Extractor, lhs->pos);
        ex->name = op;
        ex->expr = std::make_unique<Ident>(opTok.pos, op);
        ex->args.push_back(std::move(lhs));
        ex->args.push_back(std::move(rhs));
        lhs = std::move(ex);
    }
    return lhs;
}

PatternPtr Parser::parseSimplePattern() {
    checkNativeStack(StackUse::Source);
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::Underscore:
            advance();
            return makePattern(Pattern::Kind::Wildcard, t.pos);
        case TokenKind::IntLit: case TokenKind::FloatLit: case TokenKind::StringLit:
        case TokenKind::CharLit: case TokenKind::KwTrue: case TokenKind::KwFalse:
        case TokenKind::KwNull: {
            auto lit = makePattern(Pattern::Kind::Literal, t.pos);
            lit->expr = parseSimple();  // a literal; a following `.x` is rejected below
            if (lit->expr->kind == NodeKind::Select || lit->expr->kind == NodeKind::Apply)
                fail("a literal pattern cannot be selected or applied", t);
            return lit;
        }
        case TokenKind::LParen: {
            advance();
            if (at(TokenKind::RParen)) {
                advance();
                auto unit = makePattern(Pattern::Kind::Literal, t.pos);
                unit->expr = std::make_unique<UnitLit>(t.pos);
                return unit;
            }
            PatternPtr first = parsePattern();
            if (at(TokenKind::RParen)) {
                advance();
                return first;
            }
            auto tuple = makePattern(Pattern::Kind::Tuple, t.pos);
            tuple->args.push_back(std::move(first));
            while (at(TokenKind::Comma)) {
                advance();
                tuple->args.push_back(parsePattern());
            }
            expect(TokenKind::RParen, "')'");
            return tuple;
        }
        case TokenKind::Identifier: {
            if (t.text == "-" && !t.backquoted &&
                (peek(1).kind == TokenKind::IntLit || peek(1).kind == TokenKind::FloatLit)) {
                auto lit = makePattern(Pattern::Kind::Literal, t.pos);
                lit->expr = parsePrefix();  // folds the sign into the literal
                return lit;
            }
            if (t.text == "_*" && !t.backquoted) {  // lexed as one identifier
                advance();
                return makePattern(Pattern::Kind::SeqWildcard, t.pos);
            }
            if (isVarId(t) && peek(1).kind != TokenKind::Dot &&
                !(peek(1).kind == TokenKind::LParen && !peek(1).firstOnLine)) {
                advance();
                auto var = makePattern(Pattern::Kind::Var, t.pos);
                var->name = t.text;
                return var;
            }
            // A stable path, possibly an extractor: A, a.B, A.B(p...), A[T](p...)
            NodePtr path = std::make_unique<Ident>(t.pos, t.text);
            std::string text = t.text;
            advance();
            while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
                advance();
                const Token& name = advance();
                text += "." + name.text;
                const SourcePos pp = path->pos;
                path = std::make_unique<Select>(pp, std::move(path), name.text);
            }
            if (at(TokenKind::LBracket)) {  // type arguments of an extractor: erased
                advance();
                parseType();
                while (at(TokenKind::Comma)) {
                    advance();
                    parseType();
                }
                expect(TokenKind::RBracket, "']'");
            }
            if (at(TokenKind::LParen) && !peek().firstOnLine) {
                advance();
                auto ex = makePattern(Pattern::Kind::Extractor, t.pos);
                ex->name = text;
                ex->expr = std::move(path);
                ex->args = parsePatternArgs();
                return ex;
            }
            auto stable = makePattern(Pattern::Kind::Stable, t.pos);
            stable->expr = std::move(path);
            return stable;
        }
        default:
            fail("pattern expected but '" + spelling(t) + "' found", t);
    }
}

// After `(`: patterns, the last one possibly a sequence wildcard
// (`_*`, `rest*`, `rest @ _*`); up to and including `)`.
std::vector<PatternPtr> Parser::parsePatternArgs() {
    std::vector<PatternPtr> args;
    while (!at(TokenKind::RParen)) {
        if (isVarId(peek()) && peek(1).kind == TokenKind::Identifier && peek(1).text == "*" &&
            peek(2).kind == TokenKind::RParen) {
            auto seq = makePattern(Pattern::Kind::SeqWildcard, peek().pos);
            seq->name = advance().text;
            advance();  // *
            args.push_back(std::move(seq));
            break;
        }
        if (isVarId(peek()) && peek(1).kind == TokenKind::At && peek(2).kind == TokenKind::Identifier &&
            peek(2).text == "_*") {
            auto seq = makePattern(Pattern::Kind::SeqWildcard, peek().pos);
            seq->name = advance().text;
            advance();  // @
            advance();  // _*
            args.push_back(std::move(seq));
            break;
        }
        args.push_back(parsePattern());
        if (!at(TokenKind::Comma)) break;
        advance();
    }
    expect(TokenKind::RParen, "')'");
    for (std::size_t k = 0; k + 1 < args.size(); ++k)
        if (args[k]->kind == Pattern::Kind::SeqWildcard)
            fail("a sequence wildcard must be the last pattern argument", peek());
    return args;
}
```

(If the lexer produces `_` followed by `*` instead of the single identifier `_*`, `parseSimplePattern`'s `Underscore` case must also accept a following `*` before `)`; `Parser::parseSimpleType` lines 1021-1036 show both spellings occur, so handle both.)

- [ ] **Step 8: Pattern `val`s**

In `parseValDef`, replace the rejection at lines 845-848 by:

```cpp
    const bool simpleName = at(TokenKind::Identifier) && !peek().isOperator &&
                            !peek().backquoted &&
                            (peek(1).kind == TokenKind::Colon || peek(1).kind == TokenKind::Equals ||
                             peek(1).kind == TokenKind::Newline ||
                             peek(1).kind == TokenKind::EndOfFile ||
                             peek(1).kind == TokenKind::Semicolon);
    if (!simpleName) {
        if (at(TokenKind::Identifier) && peek(1).kind == TokenKind::Comma)
            notImplemented("several names in one value definition", peek());
        if (isLazy) notImplemented("lazy pattern definitions", peek());
        node->pattern = parsePattern2();
        if (at(TokenKind::Colon)) {  // val (a, b): (Int, Int) = ...
            advance();
            parseType();
        }
        expect(TokenKind::Equals, "'='");
        node->rhs = parseExprOrIndented();
        return node;
    }
```

- [ ] **Step 9: `for`**

In `parseExprNoPlaceholders` replace `case TokenKind::KwFor: unsupported(...)` by `case TokenKind::KwFor: return parseFor();` and add:

```cpp
// for (enums) [yield|do] body | for { enums } [yield|do] body |
// for <Indent> enums <Outdent> (yield|do) body | for enums (yield|do) body
NodePtr Parser::parseFor() {
    auto node = std::make_unique<For>(advance().pos);
    bool delimited = true;
    if (at(TokenKind::LParen)) {
        advance();
        parseEnumerators(*node, TokenKind::RParen);
        expect(TokenKind::RParen, "')'");
    } else if (at(TokenKind::LBrace)) {
        advance();
        parseEnumerators(*node, TokenKind::RBrace);
        expect(TokenKind::RBrace, "'}'");
    } else if (at(TokenKind::Indent)) {
        advance();
        parseEnumerators(*node, TokenKind::Outdent);
        expect(TokenKind::Outdent, "end of the enumerators");
    } else {
        delimited = false;
        parseEnumerators(*node, TokenKind::EndOfFile);
    }
    if (at(TokenKind::Newline) &&
        (peek(1).kind == TokenKind::KwYield || peek(1).kind == TokenKind::KwDo))
        advance();
    if (at(TokenKind::KwYield)) {
        advance();
        node->isYield = true;
    } else if (at(TokenKind::KwDo)) {
        advance();
    } else if (!delimited) {
        fail("'do' or 'yield' expected", peek());
    }
    node->body = parseExprOrIndented();
    return node;
}

// Generator {(`;` | nl) Enumerator | Guard}: guards may follow a generator
// or another guard without a separator.
void Parser::parseEnumerators(For& f, TokenKind terminator) {
    while (at(TokenKind::Newline)) advance();
    f.enums.push_back(parseGeneratorOrValue());
    if (f.enums.back().kind != Enumerator::Kind::Generator)
        fail("a for-comprehension must start with a generator `p <- e`", peek());
    for (;;) {
        if (at(TokenKind::KwIf)) {
            Enumerator g;
            g.kind = Enumerator::Kind::Guard;
            g.pos = advance().pos;
            g.expr = parseInfix(0);
            f.enums.push_back(std::move(g));
            continue;
        }
        if (at(TokenKind::Semicolon) || at(TokenKind::Newline)) {
            advance();
            while (at(TokenKind::Newline) || at(TokenKind::Semicolon)) advance();
            if (at(terminator)) return;
            if (at(TokenKind::KwIf)) continue;
            f.enums.push_back(parseGeneratorOrValue());
            continue;
        }
        return;
    }
}

// [case] Pattern1 `<-` Expr | Pattern1 `=` Expr
Enumerator Parser::parseGeneratorOrValue() {
    Enumerator en;
    en.pos = peek().pos;
    if (at(TokenKind::KwCase)) advance();
    en.pattern = parsePattern1();
    if (at(TokenKind::LeftArrow)) {
        advance();
        en.kind = Enumerator::Kind::Generator;
    } else if (at(TokenKind::Equals)) {
        advance();
        en.kind = Enumerator::Kind::Value;
    } else {
        fail("'<-' or '=' expected in a for-comprehension", peek());
    }
    en.expr = parseExprOrIndented();
    return en;
}
```

- [ ] **Step 10: Run the tests, then the suite**

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Parser' --output-on-failure`
Expected: PASS.

Run: `ctest --test-dir build_release`
Expected: 100% pass. (No conformance fixture uses `match`, `for` or `_` yet; a program that does now reaches the compiler, which Task 3 makes reject the new nodes with a clean `CompileError` until the task that implements each one.)

- [ ] **Step 11: Commit**

```bash
git add src/frontend tests/unit/test_parser.cpp
git commit -m "parser: match, patterns, case lambdas, for-comprehensions, placeholders

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 3: Desugar — `for`, case-class companions, pattern `val`s, member assignment

**Files:**
- Modify: `src/frontend/Desugar.cpp`, `src/frontend/AST.h` (export `cloneType`), `src/compiler/Compiler.cpp` (interim rejection and capture-analysis cases for the new nodes)
- Test: `tests/unit/test_desugar.cpp`, `tests/unit/test_compiler.cpp`

**Interfaces:**
- Consumes: Tasks 1–2 (`TemplateDef`, `New`, `Match`, `For`, `Pattern`, `clonePattern`, `cloneSimpleExpr`).
- Produces (DESIGN §3.4 rows of Phase 2):
  - `for` → `map`/`flatMap`/`withFilter`/`foreach` per the Scala 3 reference; refutable generator patterns are filtered with `withFilter { case p => true; case _ => false }`; value definitions are tupled through `map`.
  - Every `case class C(ps)` of a compilation unit has a companion `object C` (created with `synthetic = true` when absent) containing `def apply(ps) = new C(ps)` and `def unapply(<u>) = <u>` (both `synthetic = true`) unless the companion defines them. Scala 3's `unapply` returns its argument (verified with `scalac`).
  - `val p = e` with a pattern → `val <tN> = e match { case p => (v1, ..., vn) }` + `val vi = <tN>._i` (one variable: `val v1 = e match { case p => v1 }`; none: the match as a statement), in units, blocks and template bodies.
  - `obj.x = v` → `obj.x_=(v)`; `f(args) = v` → `f.update(args, v)`; `obj.x op= v` (simple `obj`) → `obj.x_=(obj.x op v)`.
  - Generated names are not identifiers (`<pN>`, `<bN>`, `<tN>`, `<u>`), so they never collide with user names.
  - `TypePtr cloneType(const TypeTree&)` is exported from `AST.cpp`.

- [ ] **Step 1: Failing desugar tests**

In `tests/unit/test_desugar.cpp`, change the second line of `Desugar.AssignmentOperators` to the Phase 2 rewrite and append the new tests:

```cpp
TEST(Desugar, AssignmentOperators) {
    EXPECT_EQ(d("x += 1"), "(= x (apply (. x +) (int 1)))");
    EXPECT_EQ(d("a.b += 1"), "(apply (. a b_=) (apply (. (. a b) +) (int 1)))");
    // A receiver that is not a simple path keeps the Phase 1 `op=` send (Q19 of Phase 1).
    EXPECT_EQ(d("f(a).b += 1"), "(apply (. (. (apply f a) b) +=) (int 1))");
}

TEST(Desugar, SettersAndUpdate) {
    EXPECT_EQ(d("p.x = 3"), "(apply (. p x_=) (int 3))");
    EXPECT_EQ(d("this.x = 3"), "(apply (. this x_=) (int 3))");
    EXPECT_EQ(d("a(1) = 2"), "(apply (. a update) (int 1) (int 2))");
    EXPECT_EQ(d("m(1, 2) = 3"), "(apply (. m update) (int 1) (int 2) (int 3))");
}

TEST(Desugar, ForComprehensions) {
    EXPECT_EQ(d("for (x <- xs) yield x * 2"),
              "(apply (. xs map) (lambda (x) (apply (. x *) (int 2))))");
    EXPECT_EQ(d("for (x <- xs) println(x)"), "(apply (. xs foreach) (lambda (x) (apply println x)))");
    EXPECT_EQ(d("for (x <- xs if x > 1) yield x"),
              "(apply (. (apply (. xs withFilter) (lambda (x) (apply (. x >) (int 1)))) map) "
              "(lambda (x) x))");
    EXPECT_EQ(d("for (x <- xs; y <- ys) yield (x, y)"),
              "(apply (. xs flatMap) (lambda (x) (apply (. ys map) (lambda (y) (tuple x y)))))");
    EXPECT_EQ(d("for (x <- xs; y <- ys) println(y)"),
              "(apply (. xs foreach) (lambda (x) (apply (. ys foreach) (lambda (y) "
              "(apply println y)))))");
    EXPECT_EQ(d("for ((a, b) <- ps) yield a"),
              "(apply (. ps map) (lambda (<p0>) (match <p0> (case (tuple-pat a b) a))))");
    EXPECT_EQ(d("for (Some(v) <- os) yield v"),
              "(apply (. (apply (. os withFilter) (lambda (<p0>) (match <p0> "
              "(case (unapply Some v) true) (case _ false)))) map) "
              "(lambda (<p1>) (match <p1> (case (unapply Some v) v))))");
    EXPECT_EQ(d("for (x <- xs; y = x * 2) yield y"),
              "(apply (. (apply (. xs map) (lambda (x) (block (val y (apply (. x *) (int 2))) "
              "(tuple x y)))) map) (lambda (<p0>) (match <p0> (case (tuple-pat x y) y))))");
}

TEST(Desugar, CaseClassCompanions) {
    EXPECT_EQ(du("case class P(x: Int, y: Int)"),
              "(unit (case-class P (x:Int y:Int) (body)) (object P (body "
              "(def apply ((x:Int y:Int)) (new P x y)) (def unapply ((<u>)) <u>))))");
    EXPECT_EQ(du("case class V(xs: Int*)"),
              "(unit (case-class V (xs:Int*) (body)) (object V (body "
              "(def apply ((xs:Int*)) (new V (splice xs))) (def unapply ((<u>)) <u>))))");
    // A user companion keeps its members and gains only what it lacks.
    EXPECT_EQ(du("case class P(x: Int)\nobject P:\n  def apply(s: String) = new P(s.length)\n"),
              "(unit (case-class P (x:Int) (body)) (object P (body (def apply ((s:String)) "
              "(new P (. s length))) (def unapply ((<u>)) <u>))))");
}

TEST(Desugar, PatternValues) {
    EXPECT_EQ(du("val (a, b) = pair"),
              "(unit (val <t0> (match pair (case (tuple-pat a b) (tuple a b)))) "
              "(val a (. <t0> _1)) (val b (. <t0> _2)))");
    EXPECT_EQ(du("val Some(v) = o"), "(unit (val v (match o (case (unapply Some v) v))))");
    EXPECT_EQ(du("val List() = xs"), "(unit (match xs (case (unapply List) ())))");
    EXPECT_EQ(du("def f = {\n  val (a, b) = p\n  a\n}"),
              "(unit (def f (block (val <t0> (match p (case (tuple-pat a b) (tuple a b)))) "
              "(val a (. <t0> _1)) (val b (. <t0> _2)) a)))");
}
```

In `tests/unit/test_compiler.cpp`, `Compiler.AssignmentTargetsAndVarWrites`: replace the first expectation by

```cpp
    EXPECT_TRUE(has(listing("val o = 1\no.x = 2"), "; x_=/1"));  // o.x_=(2)
```

and add

```cpp
TEST(Compiler, Phase2NodesAreRejectedUntilImplemented) {
    EXPECT_TRUE(has(compileError("val r = 1 match { case 1 => 2 }"), "match is not implemented yet"));
    EXPECT_TRUE(has(compileError("class A"), "classes, traits and objects are not implemented yet"));
    EXPECT_TRUE(has(compileError("val a = new A"), "'new' is not implemented yet"));
}
```

(The three rejections are temporary: Tasks 7 and 12 replace them with the implementation and delete the test.)

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Desugar|Compiler' --output-on-failure`
Expected: FAIL.

- [ ] **Step 2: Implement**

Export in `src/frontend/AST.h`: `TypePtr cloneType(const TypeTree& t);` (the Task 2 helper, made public).

In `src/frontend/Desugar.cpp`, `Desugarer::expr` gains these cases (and the `Block` case calls `stats`):

```cpp
            case NodeKind::Block: {
                stats(as<Block>(*n).stats);
                return n;
            }
            case NodeKind::Assign: {
                auto& a = as<Assign>(*n);
                if (a.target->kind == NodeKind::Select) {  // obj.x = v  →  obj.x_=(v)
                    auto& s = as<Select>(*a.target);
                    return expr(call(a.pos, std::move(s.qualifier), s.name + "_=", std::move(a.value)));
                }
                if (a.target->kind == NodeKind::Apply) {   // f(args) = v  →  f.update(args, v)
                    auto& target = as<Apply>(*a.target);
                    auto up = std::make_unique<Apply>(
                        a.pos, std::make_unique<Select>(a.pos, std::move(target.fn), "update"));
                    up->args = std::move(target.args);
                    up->args.push_back(std::move(a.value));
                    return expr(std::move(up));
                }
                a.target = expr(std::move(a.target));
                a.value = expr(std::move(a.value));
                return n;
            }
            case NodeKind::TemplateDef: {
                auto& t = as<TemplateDef>(*n);
                params(t.ctorParams);
                for (auto& p : t.parents)
                    for (auto& arg : p.args) arg = expr(std::move(arg));
                stats(t.body);
                return n;
            }
            case NodeKind::New: {
                for (auto& arg : as<New>(*n).args) arg = expr(std::move(arg));
                return n;
            }
            case NodeKind::Match: {
                auto& m = as<Match>(*n);
                m.scrutinee = expr(std::move(m.scrutinee));
                for (CaseDef& c : m.cases) {
                    if (c.guard) c.guard = expr(std::move(c.guard));
                    c.body = expr(std::move(c.body));
                }
                return n;
            }
            case NodeKind::For: return expr(forExpr(as<For>(*n)));  // rewrite, then desugar the result
```

(The `Assign` case replaces the Phase 1 one.) Public entry points:

```cpp
    // A statement list: pattern vals are expanded, then every statement is desugared.
    void stats(std::vector<NodePtr>& ss) {
        expandPatternVals(ss);
        for (auto& s : ss) s = expr(std::move(s));
    }
```

and `desugar(CompilationUnit&)` becomes

```cpp
void desugar(CompilationUnit& unit) {
    Desugarer ds;
    ds.synthesizeCompanions(unit.stats);
    ds.stats(unit.stats);
}
```

The `infix` rewrite of `x op= y` (lines 228-237) gains the selection case:

```cpp
        if (isAssignmentOperator(op)) {
            const std::string base = op.substr(0, op.size() - 1);
            if (lhs->kind == NodeKind::Ident) {
                const auto& id = as<Ident>(*lhs);
                auto target = std::make_unique<Ident>(id.pos, id.name);
                auto value = call(pos, std::move(lhs), base, std::move(rhs));
                return std::make_unique<Assign>(pos, std::move(target), std::move(value));
            }
            if (lhs->kind == NodeKind::Select && isSimplePath(*as<Select>(*lhs).qualifier)) {
                // obj.x op= v  →  obj.x_=(obj.x op v) (Q19 of Phase 1: no op= member lookup)
                auto& s = as<Select>(*lhs);
                NodePtr receiver = cloneSimpleExpr(*s.qualifier);
                const std::string setter = s.name + "_=";
                NodePtr value = call(pos, std::move(lhs), base, std::move(rhs));
                return call(pos, std::move(receiver), setter, std::move(value));
            }
            return call(pos, std::move(lhs), op, std::move(rhs));
        }
```

with `static bool isSimplePath(const Node& n) { return n.kind == NodeKind::Ident || (n.kind == NodeKind::Select && isSimplePath(*as<Select>(n).qualifier)); }`.

The new members of `Desugarer` (private unless noted):

```cpp
public:
    // Every case class of `ss` gets a companion object with the synthesised
    // apply and unapply (DESIGN §4.5), unless the companion defines them.
    void synthesizeCompanions(std::vector<NodePtr>& ss) {
        for (std::size_t k = 0; k < ss.size(); ++k) {
            if (ss[k]->kind != NodeKind::TemplateDef) continue;
            const auto& c = as<TemplateDef>(*ss[k]);
            if (!c.isCase || c.kind != TemplateKind::Class) continue;
            TemplateDef* companion = nullptr;
            for (auto& s : ss)
                if (s->kind == NodeKind::TemplateDef && as<TemplateDef>(*s).kind == TemplateKind::Object &&
                    as<TemplateDef>(*s).name == c.name)
                    companion = &as<TemplateDef>(*s);
            if (!companion) {
                auto obj = std::make_unique<TemplateDef>(c.pos);
                obj->kind = TemplateKind::Object;
                obj->name = c.name;
                obj->synthetic = true;
                companion = obj.get();
                ss.insert(ss.begin() + static_cast<std::ptrdiff_t>(k + 1), std::move(obj));
            }
            auto defines = [&](const char* name) {
                for (const auto& m : companion->body)
                    if (m->kind == NodeKind::DefDef && as<DefDef>(*m).name == name) return true;
                return false;
            };
            if (!defines("apply")) companion->body.push_back(syntheticApply(c));
            if (!defines("unapply")) companion->body.push_back(syntheticUnapply(c.pos));
        }
    }

private:
    int patternCounter_ = 0;  // <pN>, <bN>

    static NodePtr syntheticApply(const TemplateDef& c) {
        auto d = std::make_unique<DefDef>(c.pos);
        d->name = "apply";
        d->synthetic = true;
        auto made = std::make_unique<New>(c.pos);
        made->type = std::make_unique<TypeTree>();
        made->type->kind = TypeTree::Kind::Name;
        made->type->name = c.name;
        made->type->pos = c.pos;
        made->hasArgs = true;
        std::vector<Param> ps;
        for (const Param& p : c.ctorParams) {
            Param q;
            q.name = p.name;
            q.pos = p.pos;
            q.repeated = p.repeated;
            if (p.type) q.type = cloneType(*p.type);
            ps.push_back(std::move(q));
            NodePtr arg = std::make_unique<Ident>(p.pos, p.name);
            if (p.repeated) arg = std::make_unique<Splice>(p.pos, std::move(arg));
            made->args.push_back(std::move(arg));
        }
        d->paramLists.push_back(std::move(ps));
        d->body = std::move(made);
        return d;
    }

    static NodePtr syntheticUnapply(SourcePos pos) {  // Scala 3: unapply(x) returns x
        auto d = std::make_unique<DefDef>(pos);
        d->name = "unapply";
        d->synthetic = true;
        Param p;
        p.name = "<u>";
        p.pos = pos;
        std::vector<Param> ps;
        ps.push_back(std::move(p));
        d->paramLists.push_back(std::move(ps));
        d->body = std::make_unique<Ident>(pos, "<u>");
        return d;
    }

    static PatternPtr makePattern(Pattern::Kind k, SourcePos pos) {
        auto p = std::make_unique<Pattern>();
        p->kind = k;
        p->pos = pos;
        return p;
    }

    // The variables a pattern binds, in source order (Alt contributes none:
    // the compiler rejects variables in alternatives).
    static void patternVariables(const Pattern& p, std::vector<std::string>& out) {
        switch (p.kind) {
            case Pattern::Kind::Var: if (p.name != "_") out.push_back(p.name); return;
            case Pattern::Kind::Bind: out.push_back(p.name); patternVariables(*p.args[0], out); return;
            case Pattern::Kind::SeqWildcard: if (!p.name.empty()) out.push_back(p.name); return;
            case Pattern::Kind::Alt: return;
            default:
                for (const auto& a : p.args) patternVariables(*a, out);
                return;
        }
    }

    // Patterns that always match (types are erased, so a tuple pattern is
    // trusted to meet tuples; a non-tuple element raises MatchError).
    static bool irrefutable(const Pattern& p) {
        switch (p.kind) {
            case Pattern::Kind::Wildcard: case Pattern::Kind::Var: return true;
            case Pattern::Kind::Bind: return irrefutable(*p.args[0]);
            case Pattern::Kind::Tuple:
                for (const auto& a : p.args) if (!irrefutable(*a)) return false;
                return true;
            default: return false;
        }
    }

    std::string fresh(const char* prefix) {
        return std::string("<") + prefix + std::to_string(patternCounter_++) + ">";
    }

    // p => body, as a lambda: `x => body` for a variable, `_ => body` for a
    // wildcard, otherwise `<pN> => <pN> match { case p => body }`.
    NodePtr lambdaFor(PatternPtr p, NodePtr body, SourcePos pos) {
        auto lambda = std::make_unique<Lambda>(pos);
        Param param;
        param.pos = pos;
        if (p->kind == Pattern::Kind::Var || p->kind == Pattern::Kind::Wildcard) {
            param.name = p->kind == Pattern::Kind::Var ? p->name : "_";
            lambda->params.push_back(std::move(param));
            lambda->body = std::move(body);
            return lambda;
        }
        param.name = fresh("p");
        auto m = std::make_unique<Match>(pos);
        m->scrutinee = std::make_unique<Ident>(pos, param.name);
        CaseDef c;
        c.pos = pos;
        c.pattern = std::move(p);
        c.body = std::move(body);
        m->cases.push_back(std::move(c));
        lambda->params.push_back(std::move(param));
        lambda->body = std::move(m);
        return lambda;
    }

    // <pN> => <pN> match { case p => true; case _ => false }
    NodePtr matchesLambda(const Pattern& p, SourcePos pos) {
        auto lambda = std::make_unique<Lambda>(pos);
        Param param;
        param.pos = pos;
        param.name = fresh("p");
        auto m = std::make_unique<Match>(pos);
        m->scrutinee = std::make_unique<Ident>(pos, param.name);
        CaseDef yes;
        yes.pos = pos;
        yes.pattern = clonePattern(p);
        yes.body = std::make_unique<BoolLit>(pos, true);
        CaseDef no;
        no.pos = pos;
        no.pattern = makePattern(Pattern::Kind::Wildcard, pos);
        no.body = std::make_unique<BoolLit>(pos, false);
        m->cases.push_back(std::move(yes));
        m->cases.push_back(std::move(no));
        lambda->params.push_back(std::move(param));
        lambda->body = std::move(m);
        return lambda;
    }

    // The name bound to the whole value matched by `p`, adding `<bN> @` when
    // `p` binds none.
    std::string boundName(PatternPtr& p) {
        if (p->kind == Pattern::Kind::Var && p->name != "_") return p->name;
        if (p->kind == Pattern::Kind::Bind) return p->name;
        auto bind = makePattern(Pattern::Kind::Bind, p->pos);
        bind->name = fresh("b");
        bind->args.push_back(std::move(p));
        p = std::move(bind);
        return p->name;
    }

    struct Generator {
        PatternPtr pattern;
        NodePtr source;
        SourcePos pos;
    };

    // Scala 3 reference, "For-comprehensions": each guard and value definition
    // attaches to the generator before it; the generators then fold from the
    // right into flatMap ... map (yield) or foreach ... foreach.
    NodePtr forExpr(For& f) {
        std::vector<Generator> gens;
        for (Enumerator& en : f.enums) {
            switch (en.kind) {
                case Enumerator::Kind::Generator: {
                    Generator g{std::move(en.pattern), std::move(en.expr), en.pos};
                    if (!irrefutable(*g.pattern))
                        g.source = call(en.pos, std::move(g.source), "withFilter",
                                        matchesLambda(*g.pattern, en.pos));
                    gens.push_back(std::move(g));
                    break;
                }
                case Enumerator::Kind::Guard: {
                    Generator& g = gens.back();
                    g.source = call(en.pos, std::move(g.source), "withFilter",
                                    lambdaFor(clonePattern(*g.pattern), std::move(en.expr), en.pos));
                    break;
                }
                case Enumerator::Kind::Value: {
                    // p1 <- e; p2 = v  →  (x1 @ p1, x2 @ p2) <- e.map { case x1 @ p1 => val x2 @ p2 = v; (x1, x2) }
                    Generator& g = gens.back();
                    const std::string n1 = boundName(g.pattern);
                    const std::string n2 = boundName(en.pattern);
                    auto body = std::make_unique<Block>(en.pos);
                    auto vd = std::make_unique<ValDef>(en.pos);
                    if (en.pattern->kind == Pattern::Kind::Var) vd->name = en.pattern->name;
                    else vd->pattern = clonePattern(*en.pattern);
                    vd->rhs = std::move(en.expr);
                    body->stats.push_back(std::move(vd));
                    auto tuple = std::make_unique<Tuple>(en.pos);
                    tuple->elems.push_back(std::make_unique<Ident>(en.pos, n1));
                    tuple->elems.push_back(std::make_unique<Ident>(en.pos, n2));
                    body->stats.push_back(std::move(tuple));
                    g.source = call(en.pos, std::move(g.source), "map",
                                    lambdaFor(clonePattern(*g.pattern), std::move(body), en.pos));
                    auto both = makePattern(Pattern::Kind::Tuple, en.pos);
                    both->args.push_back(std::move(g.pattern));
                    both->args.push_back(std::move(en.pattern));
                    g.pattern = std::move(both);
                    break;
                }
            }
        }
        return foldGenerators(gens, 0, std::move(f.body), f.isYield);
    }

    NodePtr foldGenerators(std::vector<Generator>& gens, std::size_t i, NodePtr body, bool isYield) {
        Generator& g = gens[i];
        const bool last = i + 1 == gens.size();
        NodePtr inner = last ? std::move(body) : foldGenerators(gens, i + 1, std::move(body), isYield);
        const char* method = !isYield ? "foreach" : last ? "map" : "flatMap";
        return call(g.pos, std::move(g.source), method,
                    lambdaFor(std::move(g.pattern), std::move(inner), g.pos));
    }

    // val p = e  →  val <tN> = e match { case p => (v1, ..., vn) }; val vi = <tN>._i
    void expandPatternVals(std::vector<NodePtr>& ss) {
        std::vector<NodePtr> out;
        for (NodePtr& s : ss) {
            if (s->kind != NodeKind::ValDef || !as<ValDef>(*s).pattern) {
                out.push_back(std::move(s));
                continue;
            }
            auto& v = as<ValDef>(*s);
            const SourcePos pos = v.pos;
            std::vector<std::string> vars;
            patternVariables(*v.pattern, vars);
            auto m = std::make_unique<Match>(pos);
            m->scrutinee = std::move(v.rhs);
            CaseDef c;
            c.pos = pos;
            c.pattern = std::move(v.pattern);
            if (vars.empty()) {
                c.body = std::make_unique<UnitLit>(pos);
            } else if (vars.size() == 1) {
                c.body = std::make_unique<Ident>(pos, vars[0]);
            } else {
                auto t = std::make_unique<Tuple>(pos);
                for (const auto& name : vars) t->elems.push_back(std::make_unique<Ident>(pos, name));
                c.body = std::move(t);
            }
            m->cases.push_back(std::move(c));
            auto val = [&](std::string name, NodePtr rhs) {
                auto d = std::make_unique<ValDef>(pos);
                d->name = std::move(name);
                d->isVar = v.isVar;
                d->mods = v.mods;
                d->rhs = std::move(rhs);
                out.push_back(std::move(d));
            };
            if (vars.empty()) {
                out.push_back(std::move(m));
            } else if (vars.size() == 1) {
                val(vars[0], std::move(m));
            } else {
                const std::string temp = "<t" + std::to_string(tempCounter_++) + ">";
                val(temp, std::move(m));
                for (std::size_t k = 0; k < vars.size(); ++k)
                    val(vars[k], std::make_unique<Select>(pos, std::make_unique<Ident>(pos, temp),
                                                          "_" + std::to_string(k + 1)));
            }
        }
        ss = std::move(out);
    }
```

The temporary `<tN>` of a pattern val shares `tempCounter_` with the right-associative temporaries `<raN>`; the `PatternValues` expectations assume a fresh `Desugarer` per test (each `du` call creates one). A `<tN>` binding is never echoed by the REPL (Task 14 skips names that start with `<`).

`discardReturnValues` (lines 175-209) gains `case NodeKind::Match:` visiting the scrutinee, every guard and every body, so `return e` inside a `match` in a `: Unit` method discards `e`.

In `src/compiler/Compiler.cpp`:
- `CaptureAnalysis::walk` gains the cases (so the `switch` stays exhaustive under `-Wall`):

```cpp
            case NodeKind::New:
                for (const auto& a : as<New>(n).args) walk(a.get(), depth);
                return;
            case NodeKind::Match: {
                const auto& m = as<Match>(n);
                walk(m.scrutinee.get(), depth);
                for (const CaseDef& c : m.cases) {
                    walk(c.guard.get(), depth);
                    walk(c.body.get(), depth);
                }
                return;
            }
            case NodeKind::TemplateDef:
            case NodeKind::For:
                return;  // templates are compiled on their own; For never survives Desugar
```

- `Compiler::compileExpr` gains the interim cases:

```cpp
        case NodeKind::TemplateDef:
            throw CompileError("classes, traits and objects are not implemented yet", n.pos);
        case NodeKind::New: throw CompileError("'new' is not implemented yet", n.pos);
        case NodeKind::Match: throw CompileError("match is not implemented yet", n.pos);
        case NodeKind::For: throw std::logic_error("compiler: for-comprehension not desugared");
```

and `compileAssign` no longer sees `Select`/`Apply` targets (Desugar rewrote them): its Phase 1 error for them becomes `std::logic_error("compiler: assignment target not desugared")`.

- [ ] **Step 3: Run the tests**

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Desugar|Compiler' --output-on-failure`
Expected: PASS. Then `ctest --test-dir build_release`: 100% pass.

- [ ] **Step 4: Commit**

```bash
git add src/frontend src/compiler/Compiler.cpp tests/unit/test_desugar.cpp tests/unit/test_compiler.cpp
git commit -m "desugar: for-comprehensions, case-class companions, pattern vals, setters and update

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 4: Linearizer, `ClassInfo` and the type namespace

**Files:**
- Create: `src/frontend/Linearizer.h`, `src/frontend/Linearizer.cpp`, `src/compiler/ClassInfo.h`, `src/compiler/ClassInfo.cpp`, `tests/unit/test_linearizer.cpp`
- Modify: `src/compiler/GlobalTable.h`, `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/unit/test_compiler.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `std::vector<std::string> protoScala::linearize(const std::string& self, const std::vector<std::vector<std::string>>& parentLinearizations);` — pure.
  - `enum class ClassKind : uint8_t { Class, Trait, Object }`, `enum class MemberKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef }`, `struct MemberInfo { MemberKind kind; std::string key; bool concrete; }`, `struct ClassInfo` (below), key helpers `privateKey`, `setterName`, `auxCtorKey`, `tupleTypeKey`, constants `kAnyKey = "@Any"`, `kAnyRefKey = "@AnyRef"`, `kProductKey = "@Product"`, `kSerializableKey = "@Serializable"`, `kPrimaryCtorKey = "<init>"`, `kMaxTupleArity = 22`; `std::vector<ClassInfo> builtinTypes();`.
  - `GlobalTable`: `BindingKind::Object`; `declareType(name) -> const std::string&` (key `@name` or `@name#N`), `defineType(ClassInfo)`, `defineBuiltinType(ClassInfo)`, `findType(name)`, `findTypeByKey(key)`, `mutableTypeByKey(key)`; `beginUnit()` also starts a new type unit.

- [ ] **Step 1: Failing linearization tests** (the examples are the ones `scalac` users know: SLS 5.1.2, *Programming in Scala* ch. 12, and the DESIGN §4.3 example)

`tests/unit/test_linearizer.cpp`:

```cpp
#include "frontend/Linearizer.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using protoScala::linearize;

namespace {

// A tiny hierarchy database: every type's parents in declaration order
// (superclass first). Linearizations are computed bottom-up like the compiler does.
class Hierarchy {
public:
    void add(const std::string& name, std::vector<std::string> parents = {}) {
        parents_[name] = std::move(parents);
    }
    std::vector<std::string> lin(const std::string& name) {
        if (name == "AnyRef") return {"AnyRef", "Any"};
        std::vector<std::vector<std::string>> ps;
        for (const auto& p : parents_.at(name)) ps.push_back(lin(p));
        if (ps.empty()) ps.push_back({"AnyRef", "Any"});
        return linearize(name, ps);
    }
    std::string text(const std::string& name) {
        std::string out;
        for (const auto& n : lin(name)) out += (out.empty() ? "" : ", ") + n;
        return out;
    }

private:
    std::map<std::string, std::vector<std::string>> parents_;
};

} // namespace

TEST(Linearizer, ScalaSpecificationIterExample) {
    // SLS 5.1.2: class Iter extends StringIterator with RichIterator
    Hierarchy h;
    h.add("AbsIterator");
    h.add("RichIterator", {"AbsIterator"});
    h.add("StringIterator", {"AbsIterator"});
    h.add("Iter", {"StringIterator", "RichIterator"});
    EXPECT_EQ(h.text("Iter"), "Iter, RichIterator, StringIterator, AbsIterator, AnyRef, Any");
}

TEST(Linearizer, ProgrammingInScalaCat) {
    Hierarchy h;
    h.add("Animal");
    h.add("Furry", {"Animal"});
    h.add("HasLegs", {"Animal"});
    h.add("FourLegged", {"HasLegs"});
    h.add("Cat", {"Animal", "Furry", "FourLegged"});
    EXPECT_EQ(h.text("Cat"), "Cat, FourLegged, HasLegs, Furry, Animal, AnyRef, Any");
}

TEST(Linearizer, DesignDocumentExample) {
    // DESIGN §4.3: class C extends B with T1 with T2
    Hierarchy h;
    h.add("B");
    h.add("T1");
    h.add("T2");
    h.add("C", {"B", "T1", "T2"});
    EXPECT_EQ(h.text("C"), "C, T2, T1, B, AnyRef, Any");
}

TEST(Linearizer, DiamondKeepsTheLastOccurrence) {
    Hierarchy h;
    h.add("A");
    h.add("B", {"A"});
    h.add("C", {"A"});
    h.add("D", {"B", "C"});
    EXPECT_EQ(h.text("D"), "D, C, B, A, AnyRef, Any");
}

TEST(Linearizer, StackableTraitsQueue) {
    // Programming in Scala ch. 12: class MyQueue extends BasicIntQueue with Incrementing with Filtering
    Hierarchy h;
    h.add("IntQueue");
    h.add("BasicIntQueue", {"IntQueue"});
    h.add("Doubling", {"IntQueue"});
    h.add("Incrementing", {"IntQueue"});
    h.add("Filtering", {"IntQueue"});
    h.add("MyQueue", {"BasicIntQueue", "Incrementing", "Filtering"});
    EXPECT_EQ(h.text("MyQueue"), "MyQueue, Filtering, Incrementing, BasicIntQueue, IntQueue, AnyRef, Any");
    h.add("Q2", {"BasicIntQueue", "Filtering", "Incrementing"});
    EXPECT_EQ(h.text("Q2"), "Q2, Incrementing, Filtering, BasicIntQueue, IntQueue, AnyRef, Any");
}

TEST(Linearizer, TraitOnlyParentsAndDeepChains) {
    Hierarchy h;
    h.add("T");
    h.add("C", {"T"});  // class C extends T: the superclass is AnyRef
    EXPECT_EQ(h.text("C"), "C, T, AnyRef, Any");
    h.add("L0");
    for (int k = 1; k <= 12; ++k) h.add("L" + std::to_string(k), {"L" + std::to_string(k - 1)});
    const auto l = h.lin("L12");
    ASSERT_EQ(l.size(), 15u);
    EXPECT_EQ(l.front(), "L12");
    EXPECT_EQ(l[12], "L0");
}
```

Add `test_linearizer.cpp` to `tests/unit/CMakeLists.txt`. Run: `cmake --build build_release` → FAIL (header missing).

- [ ] **Step 2: The linearizer**

`src/frontend/Linearizer.h`:

```cpp
/*
 * Linearizer — Scala's class linearization (SLS 5.1.2), computed by the
 * frontend on type keys (DESIGN §4.3). Pure: no AST, no protoCore.
 *
 *   L(C) = C, L(Pn) +: ... +: L(P2) +: L(P1)
 *
 * where P1 is the first parent (the superclass) and X +: Y keeps the elements
 * of X that do not occur in Y, followed by Y (right-associative), so the last
 * occurrence of a shared ancestor wins. The caller passes L(AnyRef) as the
 * only parent linearization of a class without an extends clause and rejects
 * cyclic hierarchies before calling.
 */
#pragma once
#include <string>
#include <vector>

namespace protoScala {

std::vector<std::string> linearize(const std::string& self,
                                   const std::vector<std::vector<std::string>>& parentLinearizations);

} // namespace protoScala
```

`src/frontend/Linearizer.cpp`:

```cpp
#include "frontend/Linearizer.h"

#include <unordered_set>

namespace protoScala {

std::vector<std::string> linearize(const std::string& self,
                                   const std::vector<std::vector<std::string>>& parents) {
    std::vector<std::string> acc;
    if (!parents.empty()) acc = parents.front();
    for (std::size_t i = 1; i < parents.size(); ++i) {
        const std::unordered_set<std::string> right(acc.begin(), acc.end());
        std::vector<std::string> merged;
        for (const std::string& x : parents[i])
            if (!right.count(x)) merged.push_back(x);
        merged.insert(merged.end(), acc.begin(), acc.end());
        acc = std::move(merged);
    }
    std::vector<std::string> out;
    out.reserve(acc.size() + 1);
    out.push_back(self);
    for (std::string& x : acc)
        if (x != self) out.push_back(std::move(x));
    return out;
}

} // namespace protoScala
```

Add `src/frontend/Linearizer.cpp` to `protoscala_frontend` in `CMakeLists.txt`. Run: `ctest --test-dir build_release -R Linearizer --output-on-failure` → PASS.

- [ ] **Step 3: `ClassInfo` and the built-in types**

`src/compiler/ClassInfo.h`:

```cpp
/*
 * ClassInfo — the compile-time description of a class, trait or object
 * (the class of an object), kept in GlobalTable's type namespace.
 *
 * Keys: a type key starts with '@' (`@Point`, `@Point#1` for a REPL
 * redefinition, `@O.type` for the class of `object O`); the class prototype is
 * stored in the globals object under it, and it is also the marker attribute
 * every instance's chain answers (Design note 5 of the Phase 2 plan). A member
 * key is the member's name, or `<TypeKey-without-@>::<name>` for a private
 * member (D5). None of these can be a Scala identifier.
 */
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoScala {

enum class ClassKind : uint8_t { Class, Trait, Object };
enum class MemberKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef };

struct MemberInfo {
    MemberKind kind = MemberKind::Def;
    std::string key;        // attribute key on the instance or the prototype
    bool concrete = true;   // false: declared abstract (no body / no initialiser)
};

inline constexpr const char* kAnyKey = "@Any";
inline constexpr const char* kAnyRefKey = "@AnyRef";
inline constexpr const char* kProductKey = "@Product";
inline constexpr const char* kSerializableKey = "@Serializable";
inline constexpr const char* kPrimaryCtorKey = "<init>";
inline constexpr unsigned kMaxTupleArity = 22;

inline std::string tupleTypeKey(unsigned n) { return "@Tuple" + std::to_string(n); }
inline std::string auxCtorKey(std::size_t arity) { return "<init>" + std::to_string(arity); }
inline std::string setterName(const std::string& name) { return name + "_="; }
inline std::string privateKey(const std::string& typeKey, const std::string& name) {
    return typeKey.substr(1) + "::" + name;
}

struct ClassInfo {
    std::string name;                 // source name ("Point"; for an object, the object's name)
    std::string key;                  // type key
    ClassKind kind = ClassKind::Class;
    bool isCase = false;
    bool isAbstract = false;
    bool isFinal = false;
    bool isSealed = false;
    bool builtin = false;             // provided by the runtime (Any, AnyRef, Product, TupleN, ...)
    bool mutableInstances = false;    // it or an ancestor declares a var field (DESIGN §4.2)
    bool hasInit = true;              // traits: false when the trait has no fields, parameters or statements
    std::vector<std::string> linearization;  // type keys, the class first, ending with @AnyRef, @Any
    std::vector<std::string> fields;         // case classes: the product elements' attribute keys
    std::vector<std::string> ctorParams;     // primary constructor parameter names
    std::size_t primaryArity = 0;
    bool primaryVariadic = false;
    std::vector<std::size_t> auxArities;     // auxiliary constructors, by arity (D31)
    std::unordered_map<std::string, MemberInfo> members;  // public (own + inherited) and own private
    std::string companionTermKey;     // classes: the term key of the companion object, if any
    std::string companionTypeKey;     // classes: the companion object's type key; objects: their companion class's
    bool companionHasApply = false;   // the companion object defines apply / unapply itself
    bool companionHasUnapply = false;
};

// Any, AnyRef, Product, Serializable and Tuple2..Tuple22, with the keys the
// runtime binds (Runtime.cpp).
std::vector<ClassInfo> builtinTypes();

} // namespace protoScala
```

`src/compiler/ClassInfo.cpp`:

```cpp
#include "compiler/ClassInfo.h"

namespace protoScala {

std::vector<ClassInfo> builtinTypes() {
    std::vector<ClassInfo> out;
    auto member = [](ClassInfo& c, const std::string& name, MemberKind kind) {
        c.members[name] = MemberInfo{kind, name, true};
    };
    ClassInfo any;
    any.name = "Any";
    any.key = kAnyKey;
    any.isAbstract = true;
    any.builtin = true;
    any.linearization = {kAnyKey};
    for (const char* m : {"equals", "==", "!=", "eq", "ne"}) member(any, m, MemberKind::Def);
    for (const char* m : {"toString", "hashCode", "##"}) member(any, m, MemberKind::ParamlessDef);
    ClassInfo anyRef = any;
    anyRef.name = "AnyRef";
    anyRef.key = kAnyRefKey;
    anyRef.linearization = {kAnyRefKey, kAnyKey};
    ClassInfo product = anyRef;
    product.name = "Product";
    product.key = kProductKey;
    product.kind = ClassKind::Trait;
    product.hasInit = false;
    product.linearization = {kProductKey, kAnyRefKey, kAnyKey};
    member(product, "productArity", MemberKind::ParamlessDef);
    member(product, "productPrefix", MemberKind::ParamlessDef);
    member(product, "productElement", MemberKind::Def);
    ClassInfo serializable = anyRef;
    serializable.name = "Serializable";
    serializable.key = kSerializableKey;
    serializable.kind = ClassKind::Trait;
    serializable.hasInit = false;
    serializable.linearization = {kSerializableKey, kAnyRefKey, kAnyKey};
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        ClassInfo t = product;
        t.name = "Tuple" + std::to_string(n);
        t.key = tupleTypeKey(n);
        t.kind = ClassKind::Class;
        t.isAbstract = false;
        t.isCase = true;
        t.isFinal = true;
        t.hasInit = true;
        t.linearization = {t.key, kProductKey, kAnyRefKey, kAnyKey};
        t.primaryArity = n;
        for (unsigned k = 1; k <= n; ++k) {
            const std::string f = "_" + std::to_string(k);
            t.fields.push_back(f);
            t.ctorParams.push_back(f);
            member(t, f, MemberKind::Val);
        }
        member(t, "copy", MemberKind::Def);
        out.push_back(std::move(t));
    }
    out.push_back(std::move(any));
    out.push_back(std::move(anyRef));
    out.push_back(std::move(product));
    out.push_back(std::move(serializable));
    return out;
}

} // namespace protoScala
```

Add `src/compiler/ClassInfo.cpp` to `protoscala_compiler`.

- [ ] **Step 4: The type namespace in `GlobalTable`**

In `src/compiler/GlobalTable.h`: `#include "compiler/ClassInfo.h"`; `enum class BindingKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param, Object };` (an `Object` binding holds the lazy singleton holder of an `object`: reads FORCE it like a lazy val); `beginUnit()` becomes `{ declaredInUnit_.clear(); typesDeclaredInUnit_.clear(); }`; and add:

```cpp
    // --- Type namespace (classes, traits, the classes of objects) ---------
    // Declares (or, within one unit, redeclares) a type name; returns its key:
    // "@Name" for the first definition, "@Name#N" for a definition that
    // shadows one from an earlier unit (the REPL rule of term keys, D25).
    const std::string& declareType(const std::string& name) {
        if (!typesDeclaredInUnit_.count(name)) {
            typesDeclaredInUnit_.insert(name);
            auto [counter, fresh] = typeCounters_->try_emplace(name, 0);
            typeKeyOfName_[name] =
                fresh ? "@" + name : "@" + name + "#" + std::to_string(++counter->second);
        }
        return typeKeyOfName_.at(name);
    }
    // Records the description of a declared type (info.key from declareType).
    void defineType(ClassInfo info) {
        const std::string key = info.key;
        typesByKey_[key] = std::move(info);
    }
    // A runtime-provided type: its name resolves to its fixed key.
    void defineBuiltinType(ClassInfo info) {
        typeKeyOfName_[info.name] = info.key;
        defineType(std::move(info));
    }
    const ClassInfo* findType(const std::string& name) const {
        auto it = typeKeyOfName_.find(name);
        return it == typeKeyOfName_.end() ? nullptr : findTypeByKey(it->second);
    }
    // Every type ever defined stays reachable by key: the linearizations of
    // classes compiled earlier name the keys of their (possibly shadowed) parents.
    const ClassInfo* findTypeByKey(const std::string& key) const {
        auto it = typesByKey_.find(key);
        return it == typesByKey_.end() ? nullptr : &it->second;
    }
    ClassInfo* mutableTypeByKey(const std::string& key) {
        auto it = typesByKey_.find(key);
        return it == typesByKey_.end() ? nullptr : &it->second;
    }
```

with the private members

```cpp
    std::unordered_map<std::string, std::string> typeKeyOfName_;  // name -> current type key
    std::unordered_map<std::string, ClassInfo> typesByKey_;
    std::unordered_set<std::string> typesDeclaredInUnit_;
    std::shared_ptr<std::unordered_map<std::string, int>> typeCounters_ =
        std::make_shared<std::unordered_map<std::string, int>>();
```

- [ ] **Step 5: A unit test of the type namespace**

Append to `tests/unit/test_compiler.cpp`:

```cpp
TEST(GlobalTableTypes, KeysShadowAcrossUnitsAndStayFindable) {
    GlobalTable g;
    for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));
    g.beginUnit();
    const std::string k1 = g.declareType("Point");
    EXPECT_EQ(k1, "@Point");
    ClassInfo p;
    p.name = "Point";
    p.key = k1;
    g.defineType(p);
    g.beginUnit();
    const std::string k2 = g.declareType("Point");
    EXPECT_EQ(k2, "@Point#1");
    ClassInfo p2 = p;
    p2.key = k2;
    g.defineType(p2);
    EXPECT_EQ(g.findType("Point")->key, "@Point#1");
    ASSERT_NE(g.findTypeByKey("@Point"), nullptr);
    EXPECT_EQ(g.findType("Tuple3")->fields.size(), 3u);
    EXPECT_EQ(g.findType("Product")->kind, ClassKind::Trait);
    const GlobalTable copy = g;  // a REPL trial copy shares the counters
    GlobalTable g2 = copy;
    g2.beginUnit();
    EXPECT_EQ(g2.declareType("Point"), "@Point#2");
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Linearizer|GlobalTableTypes' --output-on-failure` → PASS; then the full suite → 100%.

- [ ] **Step 6: Commit**

```bash
git add src/frontend/Linearizer.* src/compiler/ClassInfo.* src/compiler/GlobalTable.h CMakeLists.txt tests/unit
git commit -m "Scala linearization, ClassInfo and the compile-time type namespace

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 5: Opcodes, bytecode constants, runtime roots, protoCore facts and Scala hashing

**Files:**
- Create: `src/runtime/Hashing.h`, `tests/unit/test_objectmodel.cpp`, `tests/unit/test_hashing.cpp`
- Modify: `src/compiler/Opcodes.h`, `src/compiler/BytecodeModule.h`, `src/compiler/BytecodeModule.cpp`, `src/runtime/Runtime.h`, `src/runtime/Runtime.cpp`, `tests/unit/test_bytecode.cpp`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 (`ClassInfo.h` key constants).
- Produces:
  - `Op::MAKE_CLASS = 64` … `Op::SEND_KW = 77` (table in "File Structure"); `enum class TypeCode : uint8_t { Integer, Double, Boolean, Char, String, Unit, List, ConsList, Function, AnyRef, AnyVal, Null, NonNull, Nothing }`; `const char* typeCodeName(TypeCode)`.
  - `BytecodeModule::ConstKind::{Names, ClassSpec, SuperSite, KwSendSite}`; `Const` members `names`, `nameSymbols`, `fields`, `fieldSymbols`, `key`, `keySymbol`, `flags`; `enum ClassFlag : std::uint32_t { kClassCase = 1, kClassCaseObject = 2, kClassMutableInstances = 4, kClassTrait = 8, kClassObject = 16 }`; `struct ClassSpecData`; `addNames`, `addClassSpec`, `addSuperSite`, `addKwSendSite`; `isMethod()`/`setMethod(bool)`.
  - `RuntimeLayout`: `anyRefProto`, `productProto`, `serializableProto`, `withFilterProto`, `listCompanion`, `tupleProto[2..22]`, keys `nameKey`, `prefixKey`, `fieldsKey`, `tupleKey`, `mutableKey`, `selfKey`, `listKey`, `predsKey`, `initKey`, `toStringName`, `equalsName`, `hashCodeName`, `tupleFieldKey[1..22]`; the globals object binds `@Any`, `@AnyRef`, `@Product`, `@Serializable`, `@Tuple2`..`@Tuple22` to their prototypes, and each of those prototypes carries its own marker attribute.
  - `namespace protoScala::hashing`: `mix`, `mixLast`, `avalanche`, `finalizeHash`, `javaStringHash`, `productHash`, `seqHash`, `longHash`, `javaDoubleHash`, `javaFloatHash`, `doubleHash`.

- [ ] **Step 1: Failing tests — protoCore facts the class model relies on**

`tests/unit/test_objectmodel.cpp`:

```cpp
// Facts about protoCore's object model that protoScala's classes rely on
// (Phase 2 plan, Design notes 1, 2 and 5). They exercise protoCore only.
// If one of them fails after a protoCore change, revisit the class model
// (MAKE_CLASS in ExecutionEngine.cpp) before anything else.
#include "protoCore.h"

#include <gtest/gtest.h>

namespace {
const proto::ProtoString* sym(proto::ProtoContext* c, const char* s) {
    return proto::ProtoString::createSymbol(c, s);
}
long long intAttr(proto::ProtoContext* c, const proto::ProtoObject* o, const char* name) {
    return proto::asSmallInt(o->getAttribute(c, sym(c, name)));
}
} // namespace

TEST(ObjectModel, InstancesOfAnImmutableShapeWalkItsWholeChain) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    const proto::ProtoObject* root = space.objectPrototype->newChild(c, false);
    const proto::ProtoObject* b = root->newChild(c)
                                      ->setAttribute(c, sym(c, "who"), c->fromInteger(1))
                                      ->setAttribute(c, sym(c, "onlyB"), c->fromInteger(10));
    const proto::ProtoObject* t1 = root->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(2));
    const proto::ProtoObject* t2 = root->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(3));
    // class C extends B with T1 with T2: its chain is L(C) without C = [T2, T1, B].
    const proto::ProtoObject* items[] = {t2, t1, b};
    const proto::ProtoObject* cls = root->newChild(c, false)
                                        ->setParents(c, c->newList(3, items))
                                        ->setAttribute(c, sym(c, "own"), c->fromInteger(4))
                                        ->setAttribute(c, sym(c, "@C"), PROTO_TRUE);
    const proto::ProtoObject* inst = cls->newChild(c, false)->setAttribute(c, sym(c, "field"), c->fromInteger(5));
    EXPECT_EQ(intAttr(c, inst, "field"), 5);   // own attribute (a field)
    EXPECT_EQ(intAttr(c, inst, "own"), 4);     // the class
    EXPECT_EQ(intAttr(c, inst, "who"), 3);     // T2 comes first in the linearization
    EXPECT_EQ(intAttr(c, inst, "onlyB"), 10);  // the last parent is reached
    EXPECT_EQ(inst->getAttribute(c, sym(c, "@C")), PROTO_TRUE);  // the membership marker
    EXPECT_EQ(b->newChild(c)->getAttribute(c, sym(c, "@C")), PROTO_NONE);
    const proto::ProtoList* parents = inst->getParents(c);
    ASSERT_EQ(parents->getSize(c), 4u);  // [C, T2, T1, B]
    EXPECT_EQ(parents->getAt(c, 0), cls);
    EXPECT_EQ(parents->getAt(c, 1), t2);
    EXPECT_EQ(parents->getAt(c, 3), b);
    EXPECT_EQ(inst->getPrototype(c), cls);
    // A mutable instance of the same class walks the same chain and keeps its identity.
    const proto::ProtoObject* m = cls->newChild(c, true);
    EXPECT_EQ(m->setAttribute(c, sym(c, "field"), c->fromInteger(6)), m);
    EXPECT_EQ(intAttr(c, m, "field"), 6);
    EXPECT_EQ(intAttr(c, m, "who"), 3);
    EXPECT_EQ(m->getPrototype(c), cls);
    // An immutable instance changes identity on every field write (Design note 10).
    EXPECT_NE(inst->setAttribute(c, sym(c, "field"), c->fromInteger(7)), inst);
}

TEST(ObjectModel, SetParentsOnAMutableObjectIsInvisibleToItsChildren) {
    // The reason class prototypes are immutable shapes (Design note 2; protoST
    // STATUS D21). If this starts failing, protoCore made parent chains live:
    // update Design note 2 (the immutable-shape construction stays correct).
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    const proto::ProtoObject* t = space.objectPrototype->newChild(c)->setAttribute(c, sym(c, "who"), c->fromInteger(1));
    const proto::ProtoObject* items[] = {t};
    const proto::ProtoObject* mutableClass = space.objectPrototype->newChild(c, true);
    mutableClass->setParents(c, c->newList(1, items));
    EXPECT_EQ(intAttr(c, mutableClass, "who"), 1);  // the class itself sees the new chain
    const proto::ProtoObject* child = mutableClass->newChild(c, false);
    EXPECT_EQ(child->getAttribute(c, sym(c, "who")), PROTO_NONE);  // its children do not
}

TEST(ObjectModel, DeepChainsResolveThroughTheMarker) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space, space.rootContext);
    proto::ProtoContext* c = &ctx;
    ctx.resizeAutomaticLocals(1);
    // L0 <- L1 <- ... <- L30, each an immutable shape whose chain is its full linearization.
    const proto::ProtoObject* chain = c->newList()->asObject(c);
    const proto::ProtoObject* cls = nullptr;
    for (int k = 0; k <= 30; ++k) {
        const std::string marker = "@L" + std::to_string(k);
        cls = space.objectPrototype->newChild(c, false)
                  ->setParents(c, chain->asList(c))
                  ->setAttribute(c, sym(c, marker.c_str()), PROTO_TRUE);
        chain = chain->asList(c)->insertAt(c, 0, cls)->asObject(c);
        ctx.setAutomaticLocal(0, chain);
    }
    const proto::ProtoObject* inst = cls->newChild(c, false);
    for (int k = 0; k <= 30; ++k) {
        const std::string marker = "@L" + std::to_string(k);
        EXPECT_EQ(inst->getAttribute(c, sym(c, marker.c_str())), PROTO_TRUE) << marker;
    }
}
```

`tests/unit/test_hashing.cpp` (every value confirmed with `scalac` 3.9.0, Design note 11):

```cpp
#include "runtime/Hashing.h"

#include <gtest/gtest.h>

using namespace protoScala::hashing;

TEST(Hashing, JavaStringHashCode) {
    EXPECT_EQ(javaStringHash("abc"), 96354);
    EXPECT_EQ(javaStringHash("Unique"), -1756661775);
    EXPECT_EQ(javaStringHash("Empty"), 67081517);
    EXPECT_EQ(javaStringHash(""), 0);
    EXPECT_EQ(javaStringHash("\xF0\x9F\x98\x80"), 1772899);  // U+1F600: two UTF-16 units
}

TEST(Hashing, CaseClassAndTupleHashCodesMatchScala) {
    EXPECT_EQ(productHash(javaStringHash("Point"), {1, 2}), -694993394);
    EXPECT_EQ(productHash(javaStringHash("Box"), {javaStringHash("abc")}), -1480185351);
    EXPECT_EQ(productHash(javaStringHash("Tuple2"), {1, javaStringHash("a")}), 1971805870);
    EXPECT_EQ(productHash(javaStringHash("Tuple2"), {1, 2}), 1316541600);
    EXPECT_EQ(productHash(javaStringHash("Empty"), {}), 67081517);  // arity 0: the prefix's hash
}

TEST(Hashing, NumbersFollowStatics) {
    EXPECT_EQ(longHash(42), 42);
    EXPECT_EQ(longHash(-1), -1);
    EXPECT_EQ(longHash(1LL << 40), 256);          // (int)(v ^ (v >>> 32))
    EXPECT_EQ(javaDoubleHash(1.5), 1073217536);   // 1.5.hashCode
    EXPECT_EQ(doubleHash(2.0), 2);                // a whole Double hashes as the Int
    EXPECT_EQ(doubleHash(1.5), javaFloatHash(1.5f));
    EXPECT_EQ(doubleHash(-0.0), 0);
}
```

Add both files to `tests/unit/CMakeLists.txt`. Run: `cmake --build build_release` → FAIL (`runtime/Hashing.h` missing). (`ObjectModel.*` needs nothing new: build it alone first if desired — it must PASS against the current protoCore; if it does not, **stop and report to the maintainer**: the class model of this plan depends on those facts.)

- [ ] **Step 2: `Hashing.h`**

```cpp
/*
 * Hashing — Scala's hash functions, bit for bit: MurmurHash3 as in
 * scala.util.hashing.MurmurHash3 / scala.runtime.Statics, and
 * java.lang.String/Double/Float.hashCode. The case-class and tuple values were
 * confirmed with scalac 3.9.0 (Phase 2 plan, Design note 11). Pure functions.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace protoScala::hashing {

inline constexpr std::uint32_t kProductSeed = 0xcafebabeu;  // MurmurHash3.productSeed

inline std::uint32_t rotl(std::uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

inline std::uint32_t mixLast(std::uint32_t h, std::uint32_t k) {
    k *= 0xcc9e2d51u;
    k = rotl(k, 15);
    k *= 0x1b873593u;
    return h ^ k;
}

inline std::uint32_t mix(std::uint32_t h, std::uint32_t data) {
    h = mixLast(h, data);
    h = rotl(h, 13);
    return h * 5u + 0xe6546b64u;
}

inline std::uint32_t avalanche(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

inline std::uint32_t finalizeHash(std::uint32_t h, std::uint32_t length) {
    return avalanche(h ^ length);
}

// java.lang.String.hashCode over the UTF-16 code units of a UTF-8 string.
inline std::int32_t javaStringHash(const std::string& utf8) {
    std::uint32_t h = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp;
        std::size_t len;
        if (b < 0x80)      { cp = b;        len = 1; }
        else if (b < 0xE0) { cp = b & 0x1F; len = 2; }
        else if (b < 0xF0) { cp = b & 0x0F; len = 3; }
        else               { cp = b & 0x07; len = 4; }
        for (std::size_t k = 1; k < len && i + k < utf8.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            h = 31u * h + (0xD800u + (cp >> 10));
            h = 31u * h + (0xDC00u + (cp & 0x3FFu));
        } else {
            h = 31u * h + cp;
        }
    }
    return static_cast<std::int32_t>(h);
}

// Scala 3's synthesised hashCode of a case class or tuple: seed 0xcafebabe,
// the productPrefix's hash, each element's ##, finalizeHash(h, arity).
// Arity 0 (case objects, `Empty()`): the prefix's hash.
inline std::int32_t productHash(std::int32_t prefixHash, const std::vector<std::int32_t>& elements) {
    if (elements.empty()) return prefixHash;
    std::uint32_t h = mix(kProductSeed, static_cast<std::uint32_t>(prefixHash));
    for (std::int32_t e : elements) h = mix(h, static_cast<std::uint32_t>(e));
    return static_cast<std::int32_t>(finalizeHash(h, static_cast<std::uint32_t>(elements.size())));
}

// MurmurHash3.orderedHash with seqSeed ("Seq".hashCode). Scala's List uses a
// specialised linear-sequence hash, so List hash codes are consistent with ==
// but not equal to the JVM's (STATUS, Phase 2 notes).
inline std::int32_t seqHash(const std::vector<std::int32_t>& elements) {
    std::uint32_t h = static_cast<std::uint32_t>(javaStringHash("Seq"));
    for (std::int32_t e : elements) h = mix(h, static_cast<std::uint32_t>(e));
    return static_cast<std::int32_t>(finalizeHash(h, static_cast<std::uint32_t>(elements.size())));
}

// Statics.longHash: an Int-range value hashes to itself, otherwise Long.hashCode.
inline std::int32_t longHash(long long v) {
    const auto i = static_cast<std::int32_t>(v);
    if (static_cast<long long>(i) == v) return i;
    const auto u = static_cast<unsigned long long>(v);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(u ^ (u >> 32)));
}

inline std::int32_t javaDoubleHash(double d) {
    std::uint64_t bits;
    if (std::isnan(d)) bits = 0x7ff8000000000000ULL;  // doubleToLongBits canonical NaN
    else std::memcpy(&bits, &d, sizeof bits);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(bits ^ (bits >> 32)));
}

inline std::int32_t javaFloatHash(float f) {
    std::uint32_t bits;
    if (std::isnan(f)) bits = 0x7fc00000u;
    else std::memcpy(&bits, &f, sizeof bits);
    return static_cast<std::int32_t>(bits);
}

// Statics.doubleHash: the `##` of a Double.
inline std::int32_t doubleHash(double d) {
    if (std::isnan(d)) return javaDoubleHash(d);
    if (d >= -2147483648.0 && d <= 2147483647.0 && d == std::trunc(d))
        return static_cast<std::int32_t>(d);
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0 && d == std::trunc(d))
        return longHash(static_cast<long long>(d));
    const float f = static_cast<float>(d);
    if (static_cast<double>(f) == d) return javaFloatHash(f);
    return javaDoubleHash(d);
}

} // namespace protoScala::hashing
```

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Hashing|ObjectModel' --output-on-failure` → PASS.

- [ ] **Step 3: Opcodes**

In `src/compiler/Opcodes.h`, replace the "64..95" comment line by:

```cpp
    // Object model and pattern matching, Phase 2 (64..95); stack effects in
    // the Phase 2 plan's opcode table and in docs/STATUS.md.
    MAKE_CLASS     = 64,  // [p1..pk m1..mn] -> [cls]    operand: ClassSpec constant
    NEW            = 65,  // [cls a1..an] -> [obj]        operand: SendSite (constructor key, n)
    INVOKE_INIT    = 66,  // [cls this a1..an] -> [this'] operand: SendSite (constructor key, n)
    STORE_FIELD    = 67,  // [v] -> []   slot[0] = slot[0].setAttribute(key, v); operand: Symbol
    SET_FIELD      = 68,  // [obj v] -> [] obj must be mutable;              operand: Symbol
    SEND_SUPER     = 69,  // [this a1..an] -> [r]         operand: SuperSite constant
    TEST_TYPE      = 70,  // [v] -> [Boolean]             operand: TypeCode
    TEST_PROTO     = 71,  // [v] -> [Boolean]             operand: Symbol (type key: the class marker)
    UNAPPLY_FIELDS = 72,  // [v] -> [f1..fn]              operand: Names constant (attribute keys)
    UNCONS         = 73,  // [list] -> [head tail]        list must be non-empty
    MATCH_ERROR    = 74,  // [v] -> throws MatchError
    CAST_FAIL      = 75,  // [v] -> throws ClassCastException; operand: String constant (type name)
    MAKE_TUPLE     = 76,  // [a1..an] -> [tuple]          operand: n (2..22)
    SEND_KW        = 77,  // [recv a1..an v1..vm] -> [r]  operand: KwSendSite constant
    // 78..95   reserved (object model)
```

and after `using Instr`:

```cpp
// Operand of TEST_TYPE: the built-in types a type pattern or isInstanceOf can
// name (D29: Int = Long = Short = Byte = BigInt, Float = Double).
enum class TypeCode : uint8_t {
    Integer, Double, Boolean, Char, String, Unit, List, ConsList, Function,
    AnyRef, AnyVal, Null, NonNull, Nothing,
};
const char* typeCodeName(TypeCode code);
```

In `BytecodeModule.cpp` extend `opName` with the fourteen names and add

```cpp
const char* typeCodeName(TypeCode code) {
    switch (code) {
        case TypeCode::Integer: return "Int";      case TypeCode::Double: return "Double";
        case TypeCode::Boolean: return "Boolean";  case TypeCode::Char: return "Char";
        case TypeCode::String: return "String";    case TypeCode::Unit: return "Unit";
        case TypeCode::List: return "List";        case TypeCode::ConsList: return "::";
        case TypeCode::Function: return "Function"; case TypeCode::AnyRef: return "AnyRef";
        case TypeCode::AnyVal: return "AnyVal";    case TypeCode::Null: return "Null";
        case TypeCode::NonNull: return "Any";      case TypeCode::Nothing: return "Nothing";
    }
    return "?";
}
```

- [ ] **Step 4: Bytecode constants and the method flag**

In `src/compiler/BytecodeModule.h`:

```cpp
    enum class ConstKind : uint8_t {
        Int, BigInt, Double, String, Char, Symbol, SendSite,
        Names, ClassSpec, SuperSite, KwSendSite,   // Phase 2
    };

    // ClassSpec flag bits.
    enum ClassFlag : std::uint32_t {
        kClassCase = 1, kClassCaseObject = 2, kClassMutableInstances = 4, kClassTrait = 8,
        kClassObject = 16,
    };

    struct Const {
        ConstKind kind;
        long long ival = 0;         // Int; Char (code point)
        double dval = 0.0;          // Double
        std::string sval;           // String bytes; BigInt digits; Symbol/SendSite/SuperSite/
                                    // KwSendSite name; ClassSpec display name
        int base = 10;              // BigInt
        std::uint32_t argc = 0;     // SendSite, SuperSite; KwSendSite: positional count;
                                    // ClassSpec: number of parents pushed
        const proto::ProtoString* symbol = nullptr;  // the name, after linkSymbols
        std::vector<std::string> names;              // Names; ClassSpec: member keys;
                                                     // KwSendSite: keyword names
        std::vector<const proto::ProtoString*> nameSymbols;   // after linkSymbols
        std::vector<std::string> fields;             // ClassSpec: product element keys
        std::vector<const proto::ProtoString*> fieldSymbols;  // after linkSymbols
        std::string key;                             // ClassSpec: type key; SuperSite: the
                                                     // type key of the defining template
        const proto::ProtoString* keySymbol = nullptr;        // after linkSymbols
        std::uint32_t flags = 0;                     // ClassSpec: ClassFlag bits
    };

    struct ClassSpecData {
        std::string displayName;          // __name__, and __prefix__ of a case class
        std::string key;                  // type key (the membership marker)
        std::uint32_t parentCount = 0;    // values pushed before the members
        std::vector<std::string> memberKeys;
        std::vector<std::string> fields;  // case classes: product elements, in order
        std::uint32_t flags = 0;
    };

    std::size_t addNames(const std::vector<std::string>& names);      // de-duplicated by content
    std::size_t addClassSpec(const ClassSpecData& spec);              // never de-duplicated
    std::size_t addSuperSite(const std::string& name, std::uint32_t argc, const std::string& ownerKey);
    std::size_t addKwSendSite(const std::string& name, std::uint32_t positional,
                              const std::vector<std::string>& keywords);

    // A method: the receiver is argument 0 (`this`); arity() counts it.
    bool isMethod() const { return method_; }
    void setMethod(bool m) { method_ = m; }
```

with the private members `std::unordered_map<std::string, std::size_t> namesIndex_, superIndex_, kwIndex_; bool method_ = false;`. In `BytecodeModule.cpp`:

```cpp
namespace {
std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
}
} // namespace

std::size_t BytecodeModule::addNames(const std::vector<std::string>& names) {
    Const c{ConstKind::Names, 0, 0.0, {}};
    c.names = names;
    return findOrAdd(namesIndex_, joined(names), consts_, std::move(c));
}

std::size_t BytecodeModule::addClassSpec(const ClassSpecData& spec) {
    Const c{ConstKind::ClassSpec, 0, 0.0, spec.displayName};
    c.argc = spec.parentCount;
    c.names = spec.memberKeys;
    c.fields = spec.fields;
    c.key = spec.key;
    c.flags = spec.flags;
    consts_.push_back(std::move(c));
    return consts_.size() - 1;
}

std::size_t BytecodeModule::addSuperSite(const std::string& name, std::uint32_t argc,
                                         const std::string& ownerKey) {
    Const c{ConstKind::SuperSite, 0, 0.0, name};
    c.argc = argc;
    c.key = ownerKey;
    return findOrAdd(superIndex_, ownerKey + "/" + name + "/" + std::to_string(argc), consts_,
                     std::move(c));
}

std::size_t BytecodeModule::addKwSendSite(const std::string& name, std::uint32_t positional,
                                          const std::vector<std::string>& keywords) {
    Const c{ConstKind::KwSendSite, 0, 0.0, name};
    c.argc = positional;
    c.names = keywords;
    return findOrAdd(kwIndex_, name + "/" + std::to_string(positional) + "/" + joined(keywords),
                     consts_, std::move(c));
}
```

`linkSymbols` interns every name that can reach the VM:

```cpp
void BytecodeModule::linkSymbols(proto::ProtoContext* ctx) {
    auto intern = [ctx](const std::string& s) { return proto::ProtoString::createSymbol(ctx, s.c_str()); };
    for (Const& c : consts_) {
        switch (c.kind) {
            case ConstKind::Symbol: case ConstKind::SendSite: case ConstKind::SuperSite:
            case ConstKind::KwSendSite: case ConstKind::ClassSpec:
                c.symbol = intern(c.sval);
                break;
            default:
                break;
        }
        c.nameSymbols.clear();
        for (const auto& n : c.names) c.nameSymbols.push_back(intern(n));
        c.fieldSymbols.clear();
        for (const auto& f : c.fields) c.fieldSymbols.push_back(intern(f));
        if (!c.key.empty()) c.keySymbol = intern(c.key);
    }
    for (auto& b : blocks_) b->linkSymbols(ctx);
}
```

(`createSymbol` takes a C string: a name with an embedded NUL cannot occur — identifiers never contain one.)

`formatConst` renders `Names` as `[a,b]`, `ClassSpec` as `class <name> <key> parents=<k> members=[...]` (plus ` fields=[...]` for case classes), `SuperSite` as `super.<name>/<n> in <key>`, `KwSendSite` as `<name>/<n>(<k1>=,<k2>=)`. `commentFor` annotates `MAKE_CLASS`, `NEW`, `INVOKE_INIT`, `SEND_SUPER`, `UNAPPLY_FIELDS`, `SEND_KW` with `formatConst`, `STORE_FIELD`/`SET_FIELD`/`TEST_PROTO`/`CAST_FAIL` with the constant's `sval`, `TEST_TYPE` with `typeCodeName`. The listing header of a method prints ` method` after the arity (`function get arity=1 method locals=0 ...`).

Append to `tests/unit/test_bytecode.cpp`:

```cpp
TEST(Bytecode, Phase2ConstantsAndDisassembly) {
    BytecodeModule m;
    EXPECT_EQ(m.addNames({"x", "y"}), m.addNames({"x", "y"}));
    EXPECT_NE(m.addNames({"x", "y"}), m.addNames({"x"}));
    EXPECT_EQ(m.addSuperSite("f", 1, "@T"), m.addSuperSite("f", 1, "@T"));
    EXPECT_NE(m.addSuperSite("f", 1, "@T"), m.addSuperSite("f", 1, "@U"));
    BytecodeModule::ClassSpecData spec{"Point", "@Point", 2, {"<init>"}, {"x", "y"},
                                       BytecodeModule::kClassCase};
    const auto k = m.addClassSpec(spec);
    EXPECT_NE(m.addClassSpec(spec), k);  // never shared
    m.emit(Op::MAKE_CLASS, k, 1);
    m.emit(Op::TEST_TYPE, static_cast<unsigned>(protoScala::TypeCode::String), 2);
    m.emit(Op::SEND_KW, m.addKwSendSite("copy", 0, {"y"}), 3);
    m.setMethod(true);
    const std::string text = m.disassemble();
    EXPECT_NE(text.find("MAKE_CLASS"), std::string::npos);
    EXPECT_NE(text.find("class Point @Point parents=2"), std::string::npos);
    EXPECT_NE(text.find("TEST_TYPE 4 ; String"), std::string::npos);
    EXPECT_NE(text.find("copy/0(y=)"), std::string::npos);
    EXPECT_NE(text.find(" method "), std::string::npos);
    proto::ProtoSpace space;
    m.linkSymbols(space.rootContext);
    EXPECT_EQ(m.constAt(k).keySymbol, proto::ProtoString::createSymbol(space.rootContext, "@Point"));
    EXPECT_EQ(m.constAt(k).fieldSymbols.size(), 2u);
}
```

- [ ] **Step 5: Runtime roots**

In `src/runtime/Runtime.h` add `#include "compiler/ClassInfo.h"` (constants only) and extend `RuntimeLayout`:

```cpp
    // Phase 2 (pinned in root-context slots like the rest):
    proto::ProtoObject* anyRefProto = nullptr;        // AnyRef: the root of every class chain
    proto::ProtoObject* productProto = nullptr;       // Product: case-class members (natives)
    proto::ProtoObject* serializableProto = nullptr;  // Serializable: a marker trait
    proto::ProtoObject* withFilterProto = nullptr;    // the lazy result of List.withFilter
    proto::ProtoObject* listCompanion = nullptr;      // the value of the global `List`
    proto::ProtoObject* tupleProto[kMaxTupleArity + 1] = {};  // [2..22]: Tuple2..Tuple22
    const proto::ProtoString* nameKey = nullptr;      // "__name__": a class's display name
    const proto::ProtoString* prefixKey = nullptr;    // "__prefix__": a case class's productPrefix
    const proto::ProtoString* fieldsKey = nullptr;    // "__fields__": ProtoList of element keys
    const proto::ProtoString* tupleKey = nullptr;     // "__tuple__": PROTO_TRUE on Tuple2..22
    const proto::ProtoString* mutableKey = nullptr;   // "__mutable__": instances are mutable
    const proto::ProtoString* selfKey = nullptr;      // "__self__": receiver of a bound method
    const proto::ProtoString* listKey = nullptr;      // "__list__": the source of a WithFilter
    const proto::ProtoString* predsKey = nullptr;     // "__preds__": its predicates (ProtoList)
    const proto::ProtoString* initKey = nullptr;      // "<init>"
    const proto::ProtoString* toStringName = nullptr; // "toString"
    const proto::ProtoString* equalsName = nullptr;   // "equals"
    const proto::ProtoString* hashCodeName = nullptr; // "hashCode"
    const proto::ProtoString* tupleFieldKey[kMaxTupleArity + 1] = {};  // [1..22]: "_1".."_22"
```

In `Runtime.cpp`, extend `RootSlot` with `kAnyRef, kProduct, kSerializable, kWithFilter, kListCompanion, kTuples` (before `kRootSlotCount`) and, after the Phase 1 prototypes:

```cpp
    L.anyRefProto       = pin(kAnyRef, L.anyProto->newChild(ctx, true));
    L.productProto      = pin(kProduct, L.anyRefProto->newChild(ctx, true));
    L.serializableProto = pin(kSerializable, L.anyRefProto->newChild(ctx, true));
    L.withFilterProto   = pin(kWithFilter, L.anyRefProto->newChild(ctx, true));
    L.listCompanion     = pin(kListCompanion, L.anyRefProto->newChild(ctx, true));
    const proto::ProtoObject* tuples[kMaxTupleArity + 1];
    for (unsigned n = 2; n <= kMaxTupleArity; ++n)
        tuples[n] = L.productProto->newChild(ctx, true);
    ctx->setAutomaticLocal(kTuples, ctx->newList(kMaxTupleArity - 1, tuples + 2)->asObject(ctx));
    for (unsigned n = 2; n <= kMaxTupleArity; ++n)
        L.tupleProto[n] = const_cast<proto::ProtoObject*>(tuples[n]);

    auto key = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };
    L.nameKey = key("__name__");
    L.prefixKey = key("__prefix__");
    L.fieldsKey = key("__fields__");
    L.tupleKey = key("__tuple__");
    L.mutableKey = key("__mutable__");
    L.selfKey = key("__self__");
    L.listKey = key("__list__");
    L.predsKey = key("__preds__");
    L.initKey = key(kPrimaryCtorKey);
    L.toStringName = key("toString");
    L.equalsName = key("equals");
    L.hashCodeName = key("hashCode");
    for (unsigned k = 1; k <= kMaxTupleArity; ++k)
        L.tupleFieldKey[k] = key(("_" + std::to_string(k)).c_str());

    // The built-in types: bound in the globals under their type keys (MAKE_CLASS
    // pushes them as parents) and marked on themselves (TEST_PROTO, Design note 5).
    // Everything below AnyRef has a __name__ (a class name, Task 6's
    // isScalaInstance); Any does not, so Cells, lazy holders, function objects
    // and the () singleton - children of Any - never pass for instances.
    auto bindType = [&](proto::ProtoObject* proto, const std::string& typeKey, const char* name) {
        const auto* k = key(typeKey.c_str());
        L.globals->setAttribute(ctx, k, proto);   // mutable: in place
        proto->setAttribute(ctx, k, PROTO_TRUE);
        if (name) proto->setAttribute(ctx, L.nameKey, makeString(ctx, name));
    };
    bindType(L.anyProto, kAnyKey, nullptr);
    bindType(L.anyRefProto, kAnyRefKey, "AnyRef");
    bindType(L.productProto, kProductKey, "Product");
    bindType(L.serializableProto, kSerializableKey, "Serializable");
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) {
        const std::string name = "Tuple" + std::to_string(n);
        proto::ProtoObject* t = L.tupleProto[n];
        bindType(t, tupleTypeKey(n), name.c_str());
        t->setAttribute(ctx, L.prefixKey, makeString(ctx, name));
        t->setAttribute(ctx, L.tupleKey, PROTO_TRUE);
        const proto::ProtoObject* keys[kMaxTupleArity];
        for (unsigned k = 1; k <= n; ++k) keys[k - 1] = L.tupleFieldKey[k]->asObject(ctx);
        t->setAttribute(ctx, L.fieldsKey, ctx->newList(n, keys)->asObject(ctx));
    }
    L.withFilterProto->setAttribute(ctx, L.nameKey, makeString(ctx, "WithFilter"));
    L.listCompanion->setAttribute(ctx, L.nameKey, makeString(ctx, "List"));
```

(`Runtime.cpp` includes `runtime/Values.h` for `makeString`; both files are in `protoscala_runtime`. The runtime prototypes are mutable, as in Phase 1, so these writes happen in place; they are never re-parented, so instances created with `newChild` see their full chains — Design note 2 applies to the chain only.)

- [ ] **Step 6: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release`
Expected: 100% pass (new: `ObjectModel.*`, `Hashing.*`, `Bytecode.Phase2ConstantsAndDisassembly`).

```bash
git add src/compiler/Opcodes.h src/compiler/BytecodeModule.* src/runtime/Runtime.* src/runtime/Hashing.h tests/unit
git commit -m "object-model opcodes, bytecode constants, runtime roots, Scala hashing

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 6: The engine's object model — dispatch, construction, `super`, type tests; Scala `toString`/`equals`/`hashCode` for instances

**Files:**
- Modify: `src/runtime/ExecutionEngine.h`, `src/runtime/ExecutionEngine.cpp`, `src/runtime/Values.h`, `src/runtime/Values.cpp`, `src/runtime/Primitives.cpp`, `tests/unit/EvalHarness.h`
- Test: `tests/unit/test_engine.cpp`

**Interfaces:**
- Consumes: Task 5.
- Produces:
  - `ExecutionEngine::construct(ctx, cls, args, argc)` (public): `new cls(args)` through `<init>`.
  - `ExecutionEngine::showTopLevel(ctx, v) -> std::string` (public): `show` with this engine active.
  - Private: `dispatch`, `callMember`, `callWithReceiver`, `bindMethod`, `forceMember`, `instantiate`, `makeClass`, `makeTuple`, `superSend`, `sendKeywords`, `testType`, `throwMissingMember`.
  - `Values.h`: `isObjectCellFast(v)`, `isScalaInstance(ctx, L, v)`, `defaultToString(ctx, L, v)`, `identityHash(ctx, v)`, `scalaHash(ctx, L, v)` (`##`); `show`, `valuesEqual` and `typeName` dispatch to the instance's class.
  - `Any` natives: `hashCode`, `##`; `toString`/`equals` are instance-aware.
  - `EvalHarness::runModule(std::unique_ptr<BytecodeModule>)`.

- [ ] **Step 1: A failing engine test with a hand-assembled class**

In `tests/unit/EvalHarness.h`, add (and make `eval` show its result with `engine_.showTopLevel(&ctx, v ? v : PROTO_NONE)` instead of `show`):

```cpp
    // Runs a hand-assembled top-level module (engine tests of opcodes the
    // compiler does not emit yet). The value of its RETURN is shown.
    std::string runModule(std::unique_ptr<BytecodeModule> mod) {
        proto::ProtoContext ctx(&space_, runtime_.rootContext());
        mod->linkSymbols(&ctx);
        const BytecodeModule& m = *mod;
        modules_.push_back(std::move(mod));
        try {
            const proto::ProtoObject* v = engine_.run(&ctx, m);
            return engine_.showTopLevel(&ctx, v);
        } catch (const ScalaError& e) {
            return std::string("error: ") + e.what();
        }
    }
```

Append to `tests/unit/test_engine.cpp`:

```cpp
#include "compiler/BytecodeModule.h"

namespace {
using protoScala::BytecodeModule;
using protoScala::Op;

// class Box(v) { def twice = v * 2 }, assembled by hand.
std::unique_ptr<BytecodeModule> boxProgram(const char* member, bool mutableInstances) {
    auto init = std::make_unique<BytecodeModule>();  // <init>(this, v): this.v = v; this
    init->setName("<init>");
    init->setMethod(true);
    init->setArity(2);
    init->setMaxStack(1);
    init->emit(Op::PUSH_LOCAL, 1, 1);
    init->emit(Op::STORE_FIELD, init->addSymbol("v"), 1);
    init->emit(Op::PUSH_LOCAL, 0, 1);
    init->emit(Op::RETURN, 0, 1);
    auto twice = std::make_unique<BytecodeModule>();  // twice(this) = this.v * 2
    twice->setName("twice");
    twice->setMethod(true);
    twice->setArity(1);
    twice->setMaxStack(2);
    twice->emit(Op::PUSH_LOCAL, 0, 2);
    twice->emit(Op::SEND, twice->addSendSite("v", 0), 2);
    twice->emit(Op::PUSH_CONST, twice->addInt(2), 2);
    twice->emit(Op::MUL, 0, 2);
    twice->emit(Op::RETURN, 0, 2);
    auto top = std::make_unique<BytecodeModule>();
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@AnyRef"), 3);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Any"), 3);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(twice)), 3);
    top->emit(Op::MAKE_FN, top->addBlock(std::move(init)), 3);
    BytecodeModule::ClassSpecData spec{"Box", "@Box", 2, {"twice", "<init>"}, {},
                                       mutableInstances ? BytecodeModule::kClassMutableInstances : 0u};
    top->emit(Op::MAKE_CLASS, top->addClassSpec(spec), 3);
    top->emit(Op::STORE_GLOBAL, top->addSymbol("@Box"), 3);
    top->emit(Op::PUSH_GLOBAL, top->addSymbol("@Box"), 4);
    top->emit(Op::PUSH_CONST, top->addInt(21), 4);
    top->emit(Op::NEW, top->addSendSite("<init>", 1), 4);
    top->emit(Op::SEND, top->addSendSite(member, 0), 4);
    top->emit(Op::RETURN, 0, 4);
    top->setMaxStack(4);
    return top;
}
} // namespace

TEST(EngineObjectModel, HandAssembledClassDispatchesMethodsAndFields) {
    EvalHarness h;
    EXPECT_EQ(h.runModule(boxProgram("twice", false)), "42");
    EXPECT_EQ(h.runModule(boxProgram("v", true)), "21");
    EXPECT_EQ(h.runModule(boxProgram("nope", false)),
              "error: NoSuchMethodError: value nope is not a member of Box");
    const std::string shown = h.runModule(boxProgram("toString", false));
    EXPECT_EQ(shown.rfind("Box@", 0), 0u) << shown;  // default toString: Name@hex
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R EngineObjectModel --output-on-failure`
Expected: FAIL (`unknown opcode 64`).

- [ ] **Step 2: Declarations**

In `src/runtime/ExecutionEngine.h`: `#include "compiler/BytecodeModule.h"` and

```cpp
public:
    // new cls(args) through the class's primary constructor (Product.copy).
    // `args` must be rooted.
    const proto::ProtoObject* construct(proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                                        const proto::ProtoObject* const* args, unsigned argc);
    // show() with this engine active, so Scala toString methods run (REPL
    // echo, unit tests). Throws ScalaError.
    std::string showTopLevel(proto::ProtoContext* ctx, const proto::ProtoObject* v);

private:
    // base[0] is the receiver, base[1..argc] the arguments; all rooted.
    const proto::ProtoObject* dispatch(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                       const proto::ProtoString* name, unsigned argc);
    const proto::ProtoObject* callMember(proto::ProtoContext* ctx, const proto::ProtoObject* member,
                                         const proto::ProtoObject** base, unsigned argc);
    const proto::ProtoObject* callWithReceiver(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                               const proto::ProtoObject** base, unsigned argc);
    const proto::ProtoObject* bindMethod(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                         const proto::ProtoObject* receiver);
    const proto::ProtoObject* forceMember(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                          const proto::ProtoObject* receiver);
    const proto::ProtoObject* instantiate(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                          const proto::ProtoString* ctorKey, unsigned argc);
    [[gnu::noinline]] const proto::ProtoObject* makeClass(proto::ProtoContext* ctx,
                                                          const BytecodeModule::Const& spec,
                                                          const proto::ProtoObject* const* base);
    const proto::ProtoObject* makeTuple(proto::ProtoContext* ctx, const proto::ProtoObject* const* elems,
                                        unsigned n);
    const proto::ProtoObject* superSend(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                        const BytecodeModule::Const& site);
    const proto::ProtoObject* sendKeywords(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                           const BytecodeModule::Const& site);
    bool testType(proto::ProtoContext* ctx, TypeCode code, const proto::ProtoObject* v) const;
    [[noreturn]] void throwMissingMember(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                         const proto::ProtoString* name) const;
```

- [ ] **Step 3: Values — instances answer through their class**

In `src/runtime/Values.h`:

```cpp
// POINTER_TAG_OBJECT is 0 (protoCore core/ProtoObject.cpp:560-575): an object cell.
inline bool isObjectCellFast(const proto::ProtoObject* v) {
    return v && (reinterpret_cast<unsigned long>(v) & 0x3FUL) == 0;
}

// An instance of a Scala class (or a runtime object with a class name, e.g.
// WithFilter): an object cell whose chain has a __name__. Function objects,
// Cells and lazy holders have none.
bool isScalaInstance(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// AnyRef.toString: "<class name>@<identity hash in hex>".
std::string defaultToString(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);

// System.identityHashCode: stable for the object's lifetime (cells never move).
std::int32_t identityHash(proto::ProtoContext* ctx, const proto::ProtoObject* v);

// Scala's `##`: numbers by Statics (an Int-range integer hashes to itself, a
// whole Double as the integer), Char: its code point, Boolean: 1231/1237,
// String: String.hashCode, () and null: 0, List: seqHash of the elements'
// `##` (provisional), instances: their hashCode method.
std::int32_t scalaHash(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v);
```

In `Values.cpp` (`#include "runtime/ExecutionEngine.h"`, `"runtime/Errors.h"`, `"runtime/Hashing.h"`):

```cpp
bool isScalaInstance(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!isObjectCellFast(v) || v == PROTO_NONE) return false;
    if (compiledModuleOf(ctx, L, v)) return false;  // function objects
    return v->getAttribute(ctx, L.nameKey) != PROTO_NONE;
}

namespace {
std::string classNameOf(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    const proto::ProtoObject* n = v->getAttribute(ctx, L.nameKey);
    return proto::ProtoObject::isStringTagFast(n)
               ? reinterpret_cast<const proto::ProtoString*>(n)->toStdString(ctx) : "Object";
}
} // namespace

std::int32_t identityHash(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const unsigned long h = v->getHash(ctx);
    return static_cast<std::int32_t>((h ^ (h >> 32)) & 0x7FFFFFFFUL);
}

std::string defaultToString(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "%x", static_cast<unsigned>(identityHash(ctx, v)));
    return classNameOf(ctx, L, v) + "@" + hex;
}
```

`show` (lines 147-150) becomes:

```cpp
    if (const BytecodeModule* m = compiledModuleOf(ctx, L, v))
        return "<function" + std::to_string(m->isMethod() ? m->arity() - 1 : m->arity()) + ">";
    if (v->isMethod(ctx)) return "<function>";
    if (isScalaInstance(ctx, L, v)) {
        const ActiveCallContext* active = activeCallContext();
        if (!active) return defaultToString(ctx, L, v);
        checkNativeStack();  // a toString may print nested instances
        const proto::ProtoObject* s = active->engine->send(ctx, v, L.toStringName, nullptr, 0);
        if (!proto::ProtoObject::isStringTagFast(s))
            throw ScalaError("ClassCastException",
                             "toString returned " + typeName(ctx, L, s) + ", not String");
        return reinterpret_cast<const proto::ProtoString*>(s)->toStdString(ctx);
    }
    return "<object>";
```

`valuesEqual` (lines 178-179): before `return false;`

```cpp
    if (isScalaInstance(ctx, L, a)) {  // a == b is a.equals(b) (null was handled above)
        const ActiveCallContext* active = activeCallContext();
        if (!active) return false;
        checkNativeStack();
        const proto::ProtoObject* argv[1] = {b};
        return active->engine->send(ctx, a, L.equalsName, argv, 1) == PROTO_TRUE;
    }
```

`typeName` (before the `Function` line): `if (isScalaInstance(ctx, L, v)) return classNameOf(ctx, L, v);`

```cpp
std::int32_t scalaHash(proto::ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == L.unit) return 0;
    if (v == PROTO_TRUE) return 1231;
    if (v == PROTO_FALSE) return 1237;
    if (proto::isSmallInt(v)) return hashing::longHash(proto::asSmallInt(v));
    if (isLargeIntFast(v)) return hashing::javaStringHash(show(ctx, L, v));  // provisional (BigInt)
    if (isDoubleFast(v)) return hashing::doubleHash(v->asDouble(ctx));
    if (isCharFast(v)) return static_cast<std::int32_t>(charValueFast(v));
    if (proto::ProtoObject::isStringTagFast(v))
        return hashing::javaStringHash(reinterpret_cast<const proto::ProtoString*>(v)->toStdString(ctx));
    if (isListFast(v)) {
        checkNativeStack();
        const proto::ProtoList* list = v->asList(ctx);
        std::vector<std::int32_t> hs;
        for (unsigned long k = 0, n = list->getSize(ctx); k < n; ++k)
            hs.push_back(scalaHash(ctx, L, list->getAt(ctx, static_cast<int>(k))));
        return hashing::seqHash(hs);
    }
    if (isScalaInstance(ctx, L, v)) {
        const ActiveCallContext* active = activeCallContext();
        if (!active) return identityHash(ctx, v);
        checkNativeStack();
        const proto::ProtoObject* h = active->engine->send(ctx, v, L.hashCodeName, nullptr, 0);
        if (!proto::isSmallInt(h))
            throw ScalaError("ClassCastException", "hashCode returned " + typeName(ctx, L, h) + ", not Int");
        return static_cast<std::int32_t>(proto::asSmallInt(h));
    }
    return identityHash(ctx, v);
}
```

(`std::vector<std::int32_t>` holds integers, not `ProtoObject*`: P1 is respected.)

- [ ] **Step 4: The engine**

`send` becomes a thin wrapper over `dispatch` (native callers pass the receiver and the arguments apart):

```cpp
const proto::ProtoObject* ExecutionEngine::send(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                                const proto::ProtoString* name,
                                                const proto::ProtoObject* const* args, unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(argc + 1);
    const proto::ProtoObject** base = scope.getAutomaticLocals();
    base[0] = receiver;
    for (unsigned k = 0; k < argc; ++k) base[k + 1] = args[k];
    const proto::ProtoObject* r = dispatch(&scope, base, name, argc);
    scope.returnValue = r;
    return r;
}

const proto::ProtoObject* ExecutionEngine::dispatch(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                                    const proto::ProtoString* name, unsigned argc) {
    const proto::ProtoObject* receiver = base[0];
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException",
                         "cannot invoke '" + name->toStdString(ctx) + "' on null");
    const proto::ProtoObject* m = receiver->getAttribute(ctx, name);
    if (!m || m == PROTO_NONE) {
        // PROTO_NONE is also a stored null: probe presence (DESIGN §4.1).
        if (receiver->hasAttribute(ctx, name) != PROTO_TRUE) throwMissingMember(ctx, receiver, name);
        if (argc == 0) return PROTO_NONE;
        throw ScalaError("NullPointerException", "cannot call null");
    }
    return callMember(ctx, m, base, argc);
}

// Calls the value `m` found for a member of the receiver base[0]: a native
// method, a Scala method (receiver in slot 0), a lazy val holder, a
// function-valued field, a plain field, or an object with `apply`.
const proto::ProtoObject* ExecutionEngine::callMember(proto::ProtoContext* ctx, const proto::ProtoObject* m,
                                                      const proto::ProtoObject** base, unsigned argc) {
    const RuntimeLayout& L = layout_;
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), base[0], base + 1, argc);
    if (const BytecodeModule* mod = compiledModuleOf(ctx, L, m)) {
        if (mod->isMethod()) {
            // `obj.m` for a method with parameters: eta-expansion (D10).
            if (argc == 0 && mod->arity() > 1 && !mod->isVariadic()) return bindMethod(ctx, m, base[0]);
            return execute(ctx, *mod, base, argc + 1, nullptr);
        }
        if (argc == 0) return m;                     // a function-valued field
        return invoke(ctx, m, base + 1, argc);       // obj.f(args) = obj.f.apply(args)
    }
    if (isObjectCellFast(m) && m->getPrototype(ctx) == L.lazyProto) {  // a lazy val member
        const proto::ProtoObject* v = forceMember(ctx, m, base[0]);
        if (argc == 0) return v;
        base[0] = v;  // the receiver is no longer needed; keep v rooted
        return invoke(ctx, v, base + 1, argc);
    }
    if (argc == 0) return m;                         // a field
    return send(ctx, m, L.applyName, base + 1, argc);  // obj.x(args) with x an object: x.apply(args)
}

const proto::ProtoObject* ExecutionEngine::callWithReceiver(proto::ProtoContext* ctx, const proto::ProtoObject* m,
                                                            const proto::ProtoObject** base, unsigned argc) {
    if (m->isMethod(ctx)) return callNative(ctx, m->asMethod(ctx), base[0], base + 1, argc);
    const BytecodeModule* mod = compiledModuleOf(ctx, layout_, m);
    if (!mod || !mod->isMethod()) throw std::logic_error("callWithReceiver: not a method");
    return execute(ctx, *mod, base, argc + 1, nullptr);
}

// A function value for `receiver.m` (eta-expansion): a Function<N> object
// sharing the method's code, with the receiver in __self__.
const proto::ProtoObject* ExecutionEngine::bindMethod(proto::ProtoContext* ctx, const proto::ProtoObject* method,
                                                      const proto::ProtoObject* receiver) {
    const RuntimeLayout& L = layout_;
    const BytecodeModule* mod = compiledModuleOf(ctx, L, method);
    return L.functionProtoFor(static_cast<unsigned>(mod->arity() - 1))
        ->newChild(ctx)
        ->setAttribute(ctx, L.codeKey, method->getOwnAttributeDirect(ctx, L.codeKey))
        ->setAttribute(ctx, L.selfKey, receiver);
}

// A lazy val member: the holder's thunk is a method, run once with the
// receiver of the access (Open question Q12); the holder is mutable.
const proto::ProtoObject* ExecutionEngine::forceMember(proto::ProtoContext* ctx, const proto::ProtoObject* holder,
                                                       const proto::ProtoObject* receiver) {
    const RuntimeLayout& L = layout_;
    if (holder->hasOwnAttribute(ctx, L.valueKey) == PROTO_TRUE)
        return holder->getOwnAttributeDirect(ctx, L.valueKey);
    const BytecodeModule* mod =
        compiledModuleOf(ctx, L, holder->getOwnAttributeDirect(ctx, L.thunkKey));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    scope.setAutomaticLocal(0, receiver);
    const proto::ProtoObject* v = execute(&scope, *mod, scope.getAutomaticLocals(), 1, nullptr);
    holder->setAttribute(&scope, L.valueKey, v);  // mutable holder: in place
    scope.returnValue = v;
    return v;
}
```

`invoke` (lines 72-85) gains the bound-method case at the top of its compiled branch:

```cpp
    if (const BytecodeModule* m = compiledModuleOf(ctx, layout_, callee)) {
        if (m->isMethod()) {  // a bound method: its receiver travels in slot 0
            const proto::ProtoObject* self = callee->getOwnAttributeDirect(ctx, layout_.selfKey);
            if (!self) throw std::logic_error("invoke: unbound method");
            proto::ProtoContext scope(ctx->space, ctx);
            scope.resizeAutomaticLocals(argc + 1);
            const proto::ProtoObject** a = scope.getAutomaticLocals();
            a[0] = self;
            for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
            const proto::ProtoObject* r = execute(&scope, *m, a, argc + 1, nullptr);
            scope.returnValue = r;
            return r;
        }
        // ... Phase 1 code (captures, execute)
    }
```

`execute`'s arity message counts the user's parameters of a method:

```cpp
    if (mod.isVariadic() ? argc < fixed : argc != arity) {
        const unsigned self = mod.isMethod() ? 1 : 0;
        throw ScalaError("IllegalArgumentException",
                         "wrong number of arguments for " + mod.name() + ": expected " +
                         std::to_string(fixed - self) + (mod.isVariadic() ? " or more" : "") +
                         ", got " + std::to_string(argc - self));
    }
```

Construction, classes and tuples:

```cpp
// new cls(args): base[0] = cls, base[1..argc] the arguments.
const proto::ProtoObject* ExecutionEngine::instantiate(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                                       const proto::ProtoString* ctorKey, unsigned argc) {
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* cls = base[0];
    const bool mutableInstances = cls->getOwnAttributeDirect(ctx, L.mutableKey) == PROTO_TRUE;
    const proto::ProtoObject* init = cls->getOwnAttributeDirect(ctx, ctorKey);
    base[0] = cls->newChild(ctx, mutableInstances);  // the instance's chain keeps cls alive
    if (!init)
        throw ScalaError("IllegalArgumentException",
                         typeName(ctx, L, base[0]) + " has no constructor taking " +
                             std::to_string(argc) + " arguments");
    return callWithReceiver(ctx, init, base, argc);
}

const proto::ProtoObject* ExecutionEngine::construct(proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                                                     const proto::ProtoObject* const* args, unsigned argc) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(argc + 1);
    const proto::ProtoObject** a = scope.getAutomaticLocals();
    a[0] = cls;
    for (unsigned k = 0; k < argc; ++k) a[k + 1] = args[k];
    const proto::ProtoObject* r = instantiate(&scope, a, layout_.initKey, argc);
    scope.returnValue = r;
    return r;
}

// MAKE_CLASS: an immutable prototype whose chain is exactly the pushed
// parents (the linearization without the class, Design notes 1-2), carrying
// the members, the class metadata and its membership marker.
const proto::ProtoObject* ExecutionEngine::makeClass(proto::ProtoContext* ctx, const BytecodeModule::Const& spec,
                                                     const proto::ProtoObject* const* base) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);  // every intermediate shape is young in `scope`
    const proto::ProtoList* chain = scope.newList(spec.argc, base);
    const proto::ProtoObject* shape = L.anyProto->newChild(&scope, false)->setParents(&scope, chain);
    for (std::size_t k = 0; k < spec.nameSymbols.size(); ++k)
        shape = shape->setAttribute(&scope, spec.nameSymbols[k], base[spec.argc + k]);
    shape = shape->setAttribute(&scope, spec.keySymbol, PROTO_TRUE);
    shape = shape->setAttribute(&scope, L.nameKey, makeString(&scope, spec.sval));
    if (spec.flags & BytecodeModule::kClassMutableInstances)
        shape = shape->setAttribute(&scope, L.mutableKey, PROTO_TRUE);
    if (spec.flags & BytecodeModule::kClassCase) {
        shape = shape->setAttribute(&scope, L.prefixKey, makeString(&scope, spec.sval));
        if (!(spec.flags & BytecodeModule::kClassCaseObject)) {
            const proto::ProtoList* fields = scope.newList();
            for (const proto::ProtoString* f : spec.fieldSymbols)
                fields = fields->appendLast(&scope, f->asObject(&scope));
            shape = shape->setAttribute(&scope, L.fieldsKey, fields->asObject(&scope));
        }
    }
    scope.returnValue = shape;
    return shape;
}

const proto::ProtoObject* ExecutionEngine::makeTuple(proto::ProtoContext* ctx, const proto::ProtoObject* const* elems,
                                                     unsigned n) {
    const RuntimeLayout& L = layout_;
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoObject* t = L.tupleProto[n]->newChild(&scope, false);
    for (unsigned k = 0; k < n; ++k) t = t->setAttribute(&scope, L.tupleFieldKey[k + 1], elems[k]);
    scope.returnValue = t;
    return t;
}

// super.m(args) in a method defined by the template site.key (DESIGN §4.4):
// the first definition of m after that template in the receiver's
// linearization, found by pointer identity (O(n), R6).
const proto::ProtoObject* ExecutionEngine::superSend(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                                     const BytecodeModule::Const& site) {
    const RuntimeLayout& L = layout_;
    const proto::ProtoObject* owner = L.globals->getOwnAttributeDirect(ctx, site.keySymbol);
    const proto::ProtoList* chain = base[0]->getParents(ctx);  // young in ctx
    const unsigned long n = chain->getSize(ctx);
    unsigned long k = 0;
    while (k < n && chain->getAt(ctx, static_cast<int>(k)) != owner) ++k;
    for (++k; k < n; ++k) {
        const proto::ProtoObject* m =
            chain->getAt(ctx, static_cast<int>(k))->getOwnAttributeDirect(ctx, site.symbol);
        if (m) return callMember(ctx, m, base, site.argc);
    }
    throw ScalaError("NoSuchMethodError", "super." + site.sval + " has no implementation after " +
                                              GlobalTable::nameOfKey(site.key.substr(1)));
}

// Named arguments reach native methods (Product.copy) through protoCore's
// keyword ProtoSparseList, keyed by the interned name (DESIGN §5.2); Scala
// methods take them in a later phase (Open question Q9).
const proto::ProtoObject* ExecutionEngine::sendKeywords(proto::ProtoContext* ctx, const proto::ProtoObject** base,
                                                        const BytecodeModule::Const& site) {
    const proto::ProtoObject* receiver = base[0];
    if (receiver == PROTO_NONE)
        throw ScalaError("NullPointerException", "cannot invoke '" + site.sval + "' on null");
    const proto::ProtoObject* m = receiver->getAttribute(ctx, site.symbol);
    if (!m || m == PROTO_NONE) throwMissingMember(ctx, receiver, site.symbol);
    if (!m->isMethod(ctx))
        throw ScalaError("UnsupportedOperationException",
                         "named arguments are not supported yet for methods written in Scala (" +
                             site.sval + ")");
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoList* positional = scope.newList(site.argc, base + 1);
    const proto::ProtoSparseList* keywords = scope.newSparseList();
    for (std::size_t k = 0; k < site.nameSymbols.size(); ++k)
        keywords = keywords->setAt(&scope, reinterpret_cast<unsigned long>(site.nameSymbols[k]),
                                   base[1 + site.argc + k]);
    const proto::ProtoObject* r = m->asMethod(ctx)(&scope, receiver, nullptr, positional, keywords);
    if (!r) r = PROTO_NONE;
    scope.returnValue = r;
    return r;
}

bool ExecutionEngine::testType(proto::ProtoContext* ctx, TypeCode code, const proto::ProtoObject* v) const {
    const RuntimeLayout& L = layout_;
    const bool isBool = v == PROTO_TRUE || v == PROTO_FALSE;
    const bool anyVal = isNumberFast(v) || isCharFast(v) || isBool || v == L.unit;
    switch (code) {
        case TypeCode::Integer:  return isIntegerFast(v);
        case TypeCode::Double:   return isDoubleFast(v);
        case TypeCode::Boolean:  return isBool;
        case TypeCode::Char:     return isCharFast(v);
        case TypeCode::String:   return proto::ProtoObject::isStringTagFast(v);
        case TypeCode::Unit:     return v == L.unit;
        case TypeCode::List:     return isListFast(v);
        case TypeCode::ConsList: return isListFast(v) && v->asList(ctx)->getSize(ctx) > 0;
        case TypeCode::Function: return v != PROTO_NONE && (compiledModuleOf(ctx, L, v) || v->isMethod(ctx));
        case TypeCode::AnyRef:   return v != PROTO_NONE && !anyVal;
        case TypeCode::AnyVal:   return anyVal;
        case TypeCode::Null:     return v == PROTO_NONE;
        case TypeCode::NonNull:  return v != PROTO_NONE;
        case TypeCode::Nothing:  return false;
    }
    return false;
}

void ExecutionEngine::throwMissingMember(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                         const proto::ProtoString* name) const {
    std::string n = name->toStdString(ctx);
    // A private member's key is "<Class>::<name>": report the name.
    const auto sep = n.rfind("::");
    if (sep != std::string::npos && sep > 0 && sep + 2 < n.size()) n = n.substr(sep + 2);
    // `x_=` on an object that has `x`: an assignment to a val.
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "_=") == 0) {
        const std::string field = n.substr(0, n.size() - 2);
        if (receiver->hasAttribute(ctx, proto::ProtoString::createSymbol(ctx, field.c_str())) == PROTO_TRUE)
            throw ScalaError("NoSuchMethodError", "Reassignment to val " + field);
    }
    throw ScalaError("NoSuchMethodError",
                     "value " + n + " is not a member of " + typeName(ctx, layout_, receiver));
}

std::string ExecutionEngine::showTopLevel(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    ActiveGuard guard(this, &layout_);
    return show(ctx, layout_, v);
}
```

The dispatch loop gains (and `SEND` now calls `dispatch`):

```cpp
                case Op::SEND: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // receiver
                    base[0] = dispatch(&frame, base, site.symbol, site.argc);
                    sp = base + 1;
                    continue;
                }
                case Op::MAKE_CLASS: {
                    const auto& spec = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - spec.argc - static_cast<unsigned>(spec.nameSymbols.size());
                    base[0] = makeClass(&frame, spec, base);
                    sp = base + 1;
                    continue;
                }
                case Op::NEW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // [cls a1..an]
                    base[0] = instantiate(&frame, base, site.symbol, site.argc);
                    sp = base + 1;
                    continue;
                }
                case Op::INVOKE_INIT: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 2;  // [cls this a1..an]
                    const proto::ProtoObject* init = base[0]->getOwnAttributeDirect(&frame, site.symbol);
                    if (!init) throw std::logic_error("INVOKE_INIT: no initialiser " + site.sval);
                    base[0] = callWithReceiver(&frame, init, base + 1, site.argc);
                    sp = base + 1;
                    continue;
                }
                case Op::STORE_FIELD:  // constructors: `this` is slot 0 (Design note 10)
                    slots[0] = slots[0]->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]);
                    --sp;
                    continue;
                case Op::SET_FIELD: {  // setters of var fields: the instance is mutable
                    const proto::ProtoObject* obj = sp[-2];
                    if (obj->setAttribute(&frame, mod.constAt(operand).symbol, sp[-1]) != obj)
                        throw ScalaError("UnsupportedOperationException",
                                         "cannot assign a field of an immutable object");
                    sp -= 2;
                    continue;
                }
                case Op::SEND_SUPER: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base = sp - site.argc - 1;  // [this a1..an]
                    base[0] = superSend(&frame, base, site);
                    sp = base + 1;
                    continue;
                }
                case Op::TEST_TYPE:
                    sp[-1] = testType(&frame, static_cast<TypeCode>(operand), sp[-1]) ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                case Op::TEST_PROTO: {  // class membership by marker (Design note 5)
                    const proto::ProtoObject* v = sp[-1];
                    sp[-1] = v != PROTO_NONE &&
                                     v->getAttribute(&frame, mod.constAt(operand).symbol) == PROTO_TRUE
                                 ? PROTO_TRUE : PROTO_FALSE;
                    continue;
                }
                case Op::UNAPPLY_FIELDS: {
                    const auto& names = mod.constAt(operand);
                    const proto::ProtoObject* v = *--sp;  // also held by the match's scrutinee slot
                    for (const proto::ProtoString* key : names.nameSymbols) {
                        const proto::ProtoObject* f = v->getOwnAttributeDirect(&frame, key);
                        *sp++ = f ? f : PROTO_NONE;
                    }
                    continue;
                }
                case Op::UNCONS: {
                    const proto::ProtoList* list = sp[-1]->asList(&frame);
                    sp[-1] = list->getAt(&frame, 0);
                    *sp++ = list->removeFirst(&frame)->asObject(&frame);
                    continue;
                }
                case Op::MATCH_ERROR: {
                    const proto::ProtoObject* v = sp[-1];
                    throw ScalaError("MatchError", show(&frame, L, v) + " (of class " + typeName(&frame, L, v) + ")");
                }
                case Op::CAST_FAIL:
                    throw ScalaError("ClassCastException", typeName(&frame, L, sp[-1]) +
                                                               " cannot be cast to " + mod.constAt(operand).sval);
                case Op::MAKE_TUPLE: {
                    const unsigned n = static_cast<unsigned>(operand);
                    const proto::ProtoObject** base = sp - n;
                    base[0] = makeTuple(&frame, base, n);
                    sp = base + 1;
                    continue;
                }
                case Op::SEND_KW: {
                    const auto& site = mod.constAt(operand);
                    const proto::ProtoObject** base =
                        sp - site.argc - static_cast<unsigned>(site.nameSymbols.size()) - 1;
                    base[0] = sendKeywords(&frame, base, site);
                    sp = base + 1;
                    continue;
                }
```

(`#include "compiler/GlobalTable.h"` is already present for `nameOfKey`.)

- [ ] **Step 5: `Any` members**

In `src/runtime/Primitives.cpp` (lines 179-188):

```cpp
PRIM(any_toString) {
    expectArgs(ctx, args, "toString", 0);
    if (proto::ProtoObject::isStringTagFast(self)) return self;
    const RuntimeLayout& L = layoutOf();
    if (isScalaInstance(ctx, L, self)) return str(ctx, defaultToString(ctx, L, self));  // AnyRef.toString
    return str(ctx, show(ctx, L, self));
}
PRIM(any_equals) {  // AnyRef.equals is identity; values compare as Scala ==
    const ProtoObject* other = arg(ctx, args, 0, "equals", 1);
    if (isScalaInstance(ctx, layoutOf(), self)) return boolean(self == other);
    return boolean(valuesEqual(ctx, layoutOf(), self, other));
}
PRIM(any_hashCode) {
    expectArgs(ctx, args, "hashCode", 0);
    const RuntimeLayout& L = layoutOf();
    if (isScalaInstance(ctx, L, self)) return ctx->fromInteger(identityHash(ctx, self));
    if (isDoubleFast(self)) return ctx->fromInteger(hashing::javaDoubleHash(self->asDouble(ctx)));
    return ctx->fromInteger(scalaHash(ctx, L, self));
}
PRIM(any_hashHash) { expectArgs(ctx, args, "##", 0); return ctx->fromInteger(scalaHash(ctx, layoutOf(), self)); }
```

and install `{"hashCode", &any_hashCode}, {"##", &any_hashHash}` in the `any` table.

- [ ] **Step 6: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release -R 'Engine' --output-on-failure` → PASS; `ctest --test-dir build_release` → 100%.

```bash
git add src/runtime tests/unit/EvalHarness.h tests/unit/test_engine.cpp
git commit -m "engine: Scala methods, construction, super, type tests; instances answer toString/equals/hashCode

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 7: Compiling classes, traits and objects

**Files:**
- Create: `src/compiler/CompileTemplates.cpp`, `tests/conformance/07-classes/*.scala` (listed in Step 1)
- Modify: `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `src/compiler/Opcodes.h`, `src/compiler/BytecodeModule.cpp` (`NEW_SPREAD`), `src/runtime/ExecutionEngine.cpp` (`NEW_SPREAD`), `src/repl/Session.cpp` and `tests/unit/EvalHarness.h` (built-in types), `CMakeLists.txt`, `tests/unit/test_compiler.cpp`

**Interfaces:**
- Consumes: Tasks 1–6 (`TemplateDef`, `New`, `linearize`, `ClassInfo`, `GlobalTable` types, the opcodes, the engine).
- Produces:
  - Templates at the top level of a unit are hoisted like `def`s: in step 2 of `compileUnit`, one `MAKE_CLASS` per template, parents first (`sortTemplates`), stored under its type key; an `object` also gets a lazy singleton holder under its term key (`BindingKind::Object`, forced on access).
  - Inside a template, a bare name resolves: local (including `this`, the self alias and constructor parameters inside the constructor) → member of the template (own, inherited, `Any`'s; `this.name` through `SEND`) → global. `e.name` inside a template or its companion uses the private key when `name` is a private member (D5).
  - Constructor `<init>(this, params)` (Design note 10): superclass initialiser (`INVOKE_INIT`), trait initialisers in Scala's order, parameter fields, lazy holders, template statements; returns `this`. Auxiliary constructors `<init>N` start with `this(...)`. Setters `x_=` for `var` fields. A class has mutable instances iff a `var` field is declared in it or in an ancestor.
  - Static checks: instantiating a trait / abstract class / object type; missing abstract members; `final` parents; non-trait mixins; illegal mixins (SLS 5.1.2); cyclic inheritance; case-to-case inheritance; reassignment of a `val` member; duplicate members (no overloading, D31); constructor arities (D31); nested templates (Open question Q6); `@main` in a template.
  - `Op::NEW_SPREAD = 78` (`new C(a, xs*)`, used by synthesised `apply` of varargs case classes).

- [ ] **Step 1: Failing fixtures and compiler tests**

Create `tests/conformance/07-classes/` with these fixtures (the `-braces`/`-indent` pairs differ only in syntax):

`class-basics-indent.scala`
```scala
// EXPECT: 3 4 7 Point(3, 4) Point(5, 4)
class Point(val x: Int, var y: Int):
  def sum = x + y
  def moved(dx: Int): Point = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"

@main def run(): Unit =
  val p = new Point(3, 4)
  println(p.x.toString + " " + p.y + " " + p.sum + " " + p + " " + p.moved(2))
```

`class-basics-braces.scala`
```scala
// EXPECT: 3 4 7 Point(3, 4) Point(5, 4)
class Point(val x: Int, var y: Int) {
  def sum = x + y
  def moved(dx: Int): Point = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"
}

@main def run(): Unit = {
  val p = new Point(3, 4)
  println(p.x.toString + " " + p.y + " " + p.sum + " " + p + " " + p.moved(2))
}
```

`class-var-field.scala`
```scala
// EXPECT: 3 13
class Counter:
  var count = 0
  def inc(): Unit = count += 1

@main def run(): Unit =
  val c = new Counter
  c.inc(); c.inc(); c.inc()
  val first = c.count
  c.count = 10
  c.count += 3
  println(first.toString + " " + c.count)
```

`class-reassign-val.scala`
```scala
// EXPECT-ERROR: Reassignment to val x
class P(val x: Int)
@main def run(): Unit =
  val p = new P(1)
  p.x = 2
```

`class-private-member.scala`
```scala
// EXPECT-ERROR: value balance is not a member of Account
class Account(private val balance: Int):
  def canPay(amount: Int) = amount <= balance
@main def run(): Unit =
  val a = new Account(100)
  println(a.canPay(50))
  println(a.balance)
```

`class-private-companion-access.scala`
```scala
// EXPECT: 42 true
class Secret(private val code: Int):
  def same(other: Secret) = code == other.code
object Secret:
  def reveal(s: Secret) = s.code

@main def run(): Unit =
  val s = new Secret(42)
  println(Secret.reveal(s).toString + " " + s.same(new Secret(42)))
```

`trait-linearization-indent.scala` (the `scalac` result of Design note 11)
```scala
// EXPECT: T2>T1>B>A
abstract class A:
  def who: String = "A"
trait T1 extends A:
  override def who = "T1>" + super.who
trait T2 extends A:
  override def who = "T2>" + super.who
class B extends A:
  override def who = "B>" + super.who
class C extends B with T1 with T2

@main def run(): Unit = println(new C().who)
```

`trait-linearization-braces.scala`
```scala
// EXPECT: T2>T1>B>A
abstract class A { def who: String = "A" }
trait T1 extends A { override def who = "T1>" + super.who }
trait T2 extends A { override def who = "T2>" + super.who }
class B extends A { override def who = "B>" + super.who }
class C extends B with T1 with T2

@main def run(): Unit = { println(new C().who) }
```

`trait-init-order.scala` (Scala's order, confirmed with `scalac`)
```scala
// EXPECT: A B T1 T2 C
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
class A:
  note("A")
trait T1 extends A:
  note("T1")
trait T2 extends A:
  note("T2")
class B extends A:
  note("B")
class C extends B with T1 with T2:
  note("C")

@main def run(): Unit =
  new C
  println(log)
```

`trait-abstract-members.scala`
```scala
// EXPECT: circle 12.56 square 4.0
trait Shape:
  def area: Double
  def name: String
  def describe = name + " " + area
class Circle(r: Double) extends Shape:
  def area = 3.14 * r * r
  def name = "circle"
class Square(side: Double) extends Shape:
  def area = side * side
  def name = "square"

@main def run(): Unit =
  println(new Circle(2.0).describe + " " + new Square(2.0).describe)
```

`trait-parameters.scala`
```scala
// EXPECT: Hello, Ada!
trait Greeter(val greeting: String):
  def greet(name: String) = greeting + ", " + name + "!"
class English extends Greeter("Hello")

@main def run(): Unit = println(new English().greet("Ada"))
```

`stackable-traits.scala` (*Programming in Scala* ch. 12 shape; ROADMAP Phase 4's stackable fixture, passing early — Open question Q5)
```scala
// EXPECT: 21 22
abstract class IntQueue:
  def put(x: Int): Unit
  def get(): Int
class BasicIntQueue extends IntQueue:
  private var last = 0
  def put(x: Int): Unit = last = x
  def get(): Int = last
trait Doubling extends IntQueue:
  abstract override def put(x: Int): Unit = super.put(2 * x)
trait Incrementing extends IntQueue:
  abstract override def put(x: Int): Unit = super.put(x + 1)
class Q1 extends BasicIntQueue with Incrementing with Doubling
class Q2 extends BasicIntQueue with Doubling with Incrementing

@main def run(): Unit =
  val a = new Q1
  a.put(10)
  val b = new Q2
  b.put(10)
  println(a.get().toString + " " + b.get())
```

`aux-constructor.scala`
```scala
// EXPECT: 3/1 1/2
class Ratio(val n: Int, val d: Int):
  def this(n: Int) = this(n, 1)
  override def toString = n.toString + "/" + d

@main def run(): Unit =
  println(new Ratio(3).toString + " " + new Ratio(1, 2))
```

`object-singleton-indent.scala` (lazy initialisation on first access, DESIGN §4.2)
```scala
// EXPECT: log=start,init count=2
var log = "start"
object Registry:
  log = log + ",init"
  var count = 0
  def register(): Int =
    count += 1
    count

@main def run(): Unit =
  Registry.register()
  Registry.register()
  println("log=" + log + " count=" + Registry.count)
```

`object-singleton-braces.scala`
```scala
// EXPECT: log=start,init count=2
var log = "start"
object Registry {
  log = log + ",init"
  var count = 0
  def register(): Int = {
    count += 1
    count
  }
}

@main def run(): Unit = {
  Registry.register()
  Registry.register()
  println("log=" + log + " count=" + Registry.count)
}
```

`companion-apply.scala`
```scala
// EXPECT: Temp(21.0) Temp(100.0)
class Temp(val celsius: Double):
  override def toString = "Temp(" + celsius + ")"
object Temp:
  def apply(c: Double): Temp = new Temp(c)
  def fromF(f: Double): Temp = new Temp((f - 32) * 5 / 9)

@main def run(): Unit =
  println(Temp(21.0).toString + " " + Temp.fromF(212.0))
```

`lazy-val-member.scala`
```scala
// EXPECT: 49 49 1
var computed = 0
class Sq(n: Int):
  lazy val value =
    computed += 1
    n * n

@main def run(): Unit =
  val s = new Sq(7)
  println(s.value.toString + " " + s.value + " " + computed)
```

`this-and-self-alias.scala`
```scala
// EXPECT: 3 true
class Builder:
  self =>
  var n = 0
  def add(): Builder =
    n += 1
    this
  def me: Builder = self

@main def run(): Unit =
  val b = new Builder
  b.add().add().add()
  println(b.n.toString + " " + (b.me eq b))
```

`default-tostring.scala`
```scala
// EXPECT: true false
class Plain(val a: Int)
@main def run(): Unit =
  val p = new Plain(1)
  println(p.toString.startsWith("Plain@").toString + " " + (p == new Plain(1)))
```

`abstract-instantiation.scala`
```scala
// EXPECT-ERROR: A is abstract; it cannot be instantiated
abstract class A
@main def run(): Unit = println(new A)
```

`missing-abstract-member.scala`
```scala
// EXPECT-ERROR: needs to be abstract, since def area is not defined
trait Shape:
  def area: Double
class Blob extends Shape
```

`final-class.scala`
```scala
// EXPECT-ERROR: cannot extend final class F
final class F
class G extends F
```

`illegal-mixin.scala`
```scala
// EXPECT-ERROR: illegal inheritance
class A
class B
trait T extends A
class C extends B with T
```

`nested-class.scala`
```scala
// EXPECT-ERROR: must be defined at the top level
@main def run(): Unit =
  class Local
  println(1)
```

In `tests/unit/test_compiler.cpp`, make the `listing`/`compileError` helpers define the built-in types (`for (ClassInfo& t : builtinTypes()) g.defineBuiltinType(std::move(t));` after declaring `println`), delete the `class`/`new` lines of `Compiler.Phase2NodesAreRejectedUntilImplemented` (keep the `match` one until Task 12), and add:

```cpp
TEST(CompilerTemplates, ClassesCompileToMakeClassNewAndMethods) {
    const auto l = listing("class P(val x: Int) { def twice = x * 2 }\nval p = new P(3)");
    EXPECT_TRUE(has(l, "class P @P parents=2 members=[twice,<init>]"));
    EXPECT_TRUE(has(l, "; @P"));
    EXPECT_TRUE(has(l, "NEW"));
    EXPECT_TRUE(has(l, "function twice arity=1 method"));
    EXPECT_TRUE(has(l, "function P.<init> arity=2 method"));
    EXPECT_TRUE(has(l, "STORE_FIELD"));
    const auto t = listing("trait T\nclass C extends T\nobject O extends C");
    EXPECT_TRUE(has(t, "class C @C parents=3"));        // [T, AnyRef, Any]
    EXPECT_TRUE(has(t, "class O @O.type parents=4"));   // [C, T, AnyRef, Any]
    EXPECT_TRUE(has(t, "MAKE_LAZY"));                   // the singleton holder
}

TEST(CompilerTemplates, StaticErrors) {
    EXPECT_TRUE(has(compileError("trait T\nval t = new T"), "T is a trait; it cannot be instantiated"));
    EXPECT_TRUE(has(compileError("abstract class A\nval a = new A"), "A is abstract; it cannot be instantiated"));
    EXPECT_TRUE(has(compileError("trait S { def area: Double }\nclass C extends S"),
                    "class C needs to be abstract, since def area is not defined"));
    EXPECT_TRUE(has(compileError("final class F\nclass G extends F"), "cannot extend final class F"));
    EXPECT_TRUE(has(compileError("class A\nclass B\nclass C extends A with B"), "class B is not a trait"));
    EXPECT_TRUE(has(compileError("class A\nclass B\ntrait T extends A\nclass C extends B with T"),
                    "illegal inheritance: superclass B is not a subclass of the superclass A "
                    "of the mixin trait T"));
    EXPECT_TRUE(has(compileError("class A extends B\nclass B extends A"), "cyclic inheritance"));
    EXPECT_TRUE(has(compileError("case class A(x: Int)\ncase class B(y: Int) extends A(y)"),
                    "case-to-case inheritance is prohibited"));
    EXPECT_TRUE(has(compileError("class A(val x: Int) { def f = { x = 1 } }"), "Reassignment to val x"));
    EXPECT_TRUE(has(compileError("class A { def f = 1; def f = 2 }"), "f is already defined in A"));
    EXPECT_TRUE(has(compileError("class A(x: Int) { def this(y: Int) = this(y) }"),
                    "differ in their number of parameters"));
    EXPECT_TRUE(has(compileError("class A(x: Int) { def this() = println(1) }"),
                    "must begin with a call to another constructor"));
    EXPECT_TRUE(has(compileError("def f = { class Local; 1 }"), "must be defined at the top level"));
    EXPECT_TRUE(has(compileError("val x = this"), "this can be used only inside"));
    EXPECT_TRUE(has(compileError("class A(x: Int)\nval a = new A(1, 2)"),
                    "wrong number of arguments for the constructor of A"));
    EXPECT_TRUE(has(compileError("class A extends Nope"), "Not found: type Nope"));
    EXPECT_TRUE(has(compileError("object O { @main def m() = 1 }"), "@main methods must be top-level"));
    EXPECT_TRUE(has(compileError("trait G(val g: Int)\ntrait H extends G\nclass C extends H"),
                    "parameterized trait G"));
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R '07-classes|CompilerTemplates' --output-on-failure`
Expected: FAIL (`classes, traits and objects are not implemented yet`).

- [ ] **Step 2: Compiler declarations**

In `src/compiler/Compiler.h`: `#include "compiler/ClassInfo.h"` and change/add (private):

```cpp
    enum class FnShape { Lambda, Def, Method };  // Method: `this` in slot 0, no enclosing function
    enum class RefKind { Local, Member, Global };
    struct Resolution {
        RefKind ref;
        LocalInfo local;                     // RefKind::Local
        BindingKind kind;                    // Local / Global
        std::string key;                     // Global: the global's key; Member: the attribute key
        const MemberInfo* member = nullptr;  // RefKind::Member
    };

    // The template whose members are being compiled (CompileTemplates.cpp).
    struct TemplateScope {
        const ClassInfo* info;       // the class, trait or the class of an object
        const ClassInfo* companion;  // its companion (private access, D5), or nullptr
        std::string selfName;        // `self =>` alias of `this`, or empty
    };
    const TemplateScope* tmpl_ = nullptr;

    const MemberInfo* memberOf(const std::string& name) const;
    std::string selectKey(const std::string& name) const;
    void loadThis(SourcePos pos);
    std::vector<const TemplateDef*> sortTemplates(const std::vector<const TemplateDef*>& ts) const;
    ClassInfo buildClassInfo(const TemplateDef& t, const std::string& typeKey) const;
    void linkCompanions(const std::vector<const TemplateDef*>& ts);
    const ClassInfo& resolveType(const TypeTree& t, SourcePos pos) const;
    const ClassInfo* superclassOf(const ClassInfo& info) const;  // nullptr: AnyRef
    std::vector<std::string> runtimeChain(const ClassInfo& info) const;
    std::string ctorKeyFor(const ClassInfo& info, std::size_t argc, SourcePos pos) const;
    void compileTemplate(const TemplateDef& t, const ClassInfo& info);
    void compileConstructor(const TemplateDef& t, const ClassInfo& info);
    void compileAuxConstructor(const DefDef& d, const ClassInfo& info);
    void compileSetter(const std::string& fieldKey, SourcePos pos);
    void compileObjectHolder(const ClassInfo& info, const std::string& termKey, SourcePos pos);
    void compileInitCall(const ClassInfo& target, const std::vector<NodePtr>& args, SourcePos pos);
    void compileNew(const New& n);
    void compileNewOf(const ClassInfo& info, const std::vector<NodePtr>& args, SourcePos pos);
    void compileTuple(const Tuple& t);
    void compileSuperSend(const std::string& name, const std::vector<NodePtr>& args, SourcePos pos);
    void compileNamedSend(const Select& sel, const std::vector<NodePtr>& args, SourcePos pos);
    void compileStats(const std::vector<NodePtr>& stats, std::size_t from, SourcePos pos);  // a block's body
```

`compileFunction` takes `FnShape shape` instead of `bool isDef` (`Lambda` with `ownsReturn` → `Def`; local and top-level defs → `Def`; lazy thunks and lambdas → `Lambda`).

- [ ] **Step 3: Functions, methods and name resolution (`Compiler.cpp`)**

`compileFunction`:

```cpp
void Compiler::compileFunction(const std::string& name, const std::vector<Param>& params,
                               const Node& body, FnShape shape, SourcePos pos) {
    for (const Param& p : params) {
        if (p.defaultValue)
            throw CompileError("default parameter values are not implemented yet", p.pos);
        if (p.byName) throw CompileError("by-name parameters are not supported yet", p.pos);
    }
    const bool method = shape == FnShape::Method;
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(name);
    mod->setMethod(method);
    FunctionState fs;
    fs.mod = mod.get();
    fs.parent = method ? nullptr : fn_;  // methods capture nothing (Design note 9)
    fs.allowsReturn = shape != FnShape::Lambda;
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    if (method) {
        const LocalInfo self{newSlot(), BindingKind::Param, false, false};
        fs.scopes.back()["this"] = self;
        if (tmpl_ && !tmpl_->selfName.empty()) fs.scopes.back()[tmpl_->selfName] = self;
    }
    for (const Param& p : params) {
        const int slot = newSlot();
        if (p.name != "_") fs.scopes.back()[p.name] = LocalInfo{slot, BindingKind::Param, false, false};
    }
    const int arity = static_cast<int>(params.size()) + (method ? 1 : 0);
    mod->setArity(arity);
    mod->setVariadic(!params.empty() && params.back().repeated);
    analyseCaptures(params, body);
    compileExpr(body);
    emit(Op::RETURN, 0, pos, -1);
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    for (const auto& spec : mod->captureSpecs())
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(spec.parentSlot), pos, +1);
    const int nCaps = mod->captureCount();
    const std::size_t index = fn_->mod->addBlock(std::move(mod));
    emit(Op::MAKE_FN, index, pos, 1 - nCaps);
}
```

`resolve` (lines 321-328) inserts the member step:

```cpp
Compiler::Resolution Compiler::resolve(const std::string& name, SourcePos pos) {
    bool found = false;
    LocalInfo info = captureInto(fn_, name, pos, &found);
    if (found) return Resolution{RefKind::Local, info, info.kind, {}, nullptr};
    if (const MemberInfo* m = memberOf(name))
        return Resolution{RefKind::Member, {}, BindingKind::Val, m->key, m};
    if (const GlobalBinding* g = globals_.binding(name))
        return Resolution{RefKind::Global, {}, g->kind, g->key, nullptr};
    if (name == "this") throw CompileError("this can be used only inside a class, trait or object", pos);
    if (name == "super") throw CompileError("'super' must be followed by a member selection", pos);
    throw CompileError("Not found: " + name, pos);
}
```

`compileIdent`:

```cpp
void Compiler::compileIdent(const Ident& id) {
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Member) {  // this.name (virtual: an override in a subclass wins)
        loadThis(id.pos);
        emit(Op::SEND, fn_->mod->addSendSite(r.key, 0), id.pos, 0);
        return;
    }
    if (r.ref == RefKind::Local) loadLocal(r.local, id.pos);
    else emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(r.key), id.pos, +1);
    if (r.kind == BindingKind::ParamlessDef) emit(Op::CALL, 0, id.pos, 0);
    else if (r.kind == BindingKind::LazyVal || r.kind == BindingKind::Object) emit(Op::FORCE, 0, id.pos, 0);
}
```

`compileSelect`:

```cpp
void Compiler::compileSelect(const Select& s) {
    if (s.qualifier->kind == NodeKind::Ident && as<Ident>(*s.qualifier).name == "super") {
        compileSuperSend(s.name, {}, s.pos);
        return;
    }
    compileExpr(*s.qualifier);
    if (s.name == "unary_-") { emit(Op::NEG, 0, s.pos, 0); return; }
    if (s.name == "unary_!") { emit(Op::NOT, 0, s.pos, 0); return; }
    emit(Op::SEND, fn_->mod->addSendSite(selectKey(s.name), 0), s.pos, 0);
}
```

`compileApply` (lines 419-453):

```cpp
void Compiler::compileApply(const Apply& a) {
    const Node* fn = a.fn.get();
    while (fn->kind == NodeKind::TypeApply) fn = as<TypeApply>(*fn).fn.get();  // erased
    bool named = false;
    for (const auto& arg : a.args) named = named || arg->kind == NodeKind::NamedArg;
    if (named) {
        if (fn->kind != NodeKind::Select)
            throw CompileError("named arguments are not implemented yet", a.pos);
        compileNamedSend(as<Select>(*fn), a.args, a.pos);
        return;
    }
    if (fn->kind == NodeKind::Select) {
        const auto& sel = as<Select>(*fn);
        if (sel.qualifier->kind == NodeKind::Ident && as<Ident>(*sel.qualifier).name == "super") {
            compileSuperSend(sel.name, a.args, a.pos);
            return;
        }
        // (Phase 1: && / || short-circuit and the fast binary operators, unchanged)
        compileExpr(*sel.qualifier);
        for (const auto& arg : a.args) {
            if (arg->kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg->pos);
            compileExpr(*arg);
        }
        const auto n = static_cast<std::uint32_t>(a.args.size());
        emit(Op::SEND, fn_->mod->addSendSite(selectKey(sel.name), n), a.pos, -static_cast<int>(n));
        return;
    }
    if (fn->kind == NodeKind::Ident) {
        const auto& id = as<Ident>(*fn);
        const Resolution r = resolve(id.name, id.pos);
        if (r.ref == RefKind::Member) {  // f(args) inside a template: this.f(args)
            loadThis(a.pos);
            for (const auto& arg : a.args) {
                if (arg->kind == NodeKind::Splice)
                    throw CompileError("splices are only supported in function calls", arg->pos);
                compileExpr(*arg);
            }
            const auto n = static_cast<std::uint32_t>(a.args.size());
            emit(Op::SEND, fn_->mod->addSendSite(r.key, n), a.pos, -static_cast<int>(n));
            return;
        }
        if (r.ref == RefKind::Global && r.kind == BindingKind::Object) {
            // C(args) where C's companion apply is the synthesised one: new C(args).
            const ClassInfo* cls = globals_.findType(id.name);
            if (cls && cls->isCase && cls->kind == ClassKind::Class &&
                cls->companionTermKey == r.key && !cls->companionHasApply) {
                compileNewOf(*cls, a.args, a.pos);
                return;
            }
        }
    }
    compileExpr(*fn);
    compileArgsAndCall(a.args, a.pos);
}
```

`compileAssign` (targets are identifiers after Desugar):

```cpp
void Compiler::compileAssign(const Assign& a) {
    if (a.target->kind != NodeKind::Ident)
        throw std::logic_error("compiler: assignment target not desugared");
    const auto& id = as<Ident>(*a.target);
    const Resolution r = resolve(id.name, id.pos);
    if (r.ref == RefKind::Member) {  // a var field: this.x_=(v), which yields ()
        if (r.member->kind != MemberKind::Var) throw CompileError("Reassignment to val " + id.name, a.pos);
        loadThis(a.pos);
        compileExpr(*a.value);
        emit(Op::SEND, fn_->mod->addSendSite(memberOf(setterName(id.name))->key, 1), a.pos, -1);
        return;
    }
    if (r.kind != BindingKind::Var) throw CompileError("Reassignment to val " + id.name, a.pos);
    // (Phase 1 local / global store, then PUSH_UNIT, unchanged)
}
```

`compileExpr` gains `case NodeKind::New: compileNew(as<New>(n)); return;`, `case NodeKind::Tuple: compileTuple(as<Tuple>(n)); return;` (Task 8 implements `compileTuple`; until then it throws the Phase 1 "tuples are not implemented yet") and changes the `TemplateDef` case to `throw CompileError("classes, traits and objects must be defined at the top level of a file", n.pos);`.

`compileBlock` delegates to `compileStats(b.stats, 0, b.pos)` (the Phase 1 body with the loop bounds starting at `from`), so an auxiliary constructor can compile the statements after its `this(...)` call.

- [ ] **Step 4: Templates (`src/compiler/CompileTemplates.cpp`)**

```cpp
/*
 * Compiler members for classes, traits and objects (Phase 2 plan, Task 7):
 * class descriptions (ClassInfo), MAKE_CLASS, methods, constructors, setters,
 * singleton holders, `new`, `super`, named-argument sends.
 */
#include "compiler/Compiler.h"
#include "frontend/Linearizer.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace protoScala {

namespace {

std::string typeNameOf(const TypeTree& t) {
    if (t.kind == TypeTree::Kind::Name || t.kind == TypeTree::Kind::Applied) return t.name;
    if (t.kind == TypeTree::Kind::Tuple) return "Tuple" + std::to_string(t.args.size());
    return "";
}

const char* kindWord(ClassKind k) {
    return k == ClassKind::Trait ? "trait" : k == ClassKind::Object ? "object" : "class";
}

const std::vector<Param>& paramsOfDef(const DefDef& d) {
    static const std::vector<Param> none;
    return d.paramLists.empty() ? none : d.paramLists[0];
}

} // namespace

const MemberInfo* Compiler::memberOf(const std::string& name) const {
    if (!tmpl_) return nullptr;
    auto it = tmpl_->info->members.find(name);
    return it == tmpl_->info->members.end() ? nullptr : &it->second;
}

// `e.name` inside a template or its companion: a private member of either is
// reached through its class-qualified key (D5, Design note 8).
std::string Compiler::selectKey(const std::string& name) const {
    if (!tmpl_) return name;
    for (const ClassInfo* c : {tmpl_->info, tmpl_->companion}) {
        if (!c) continue;
        auto it = c->members.find(name);
        if (it != c->members.end() && it->second.key != name) return it->second.key;
    }
    return name;
}

void Compiler::loadThis(SourcePos pos) {
    bool found = false;
    const LocalInfo info = captureInto(fn_, "this", pos, &found);
    if (!found) throw CompileError("this can be used only inside a class, trait or object", pos);
    loadLocal(info, pos);
}

const ClassInfo& Compiler::resolveType(const TypeTree& t, SourcePos pos) const {
    std::string name = typeNameOf(t);
    if (name.empty()) throw CompileError("a class or trait name is expected here", pos);
    for (const char* prefix : {"scala.", "java.lang."})
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
    const ClassInfo* c = globals_.findType(name);
    if (!c) throw CompileError("Not found: type " + name, pos);
    return *c;
}

// Parents first; a template of this unit may extend another one defined below it.
std::vector<const TemplateDef*> Compiler::sortTemplates(const std::vector<const TemplateDef*>& ts) const {
    std::unordered_map<std::string, const TemplateDef*> byName;
    for (const TemplateDef* t : ts)
        if (t->kind != TemplateKind::Object) byName[t->name] = t;
    std::vector<const TemplateDef*> out;
    std::unordered_map<const TemplateDef*, int> state;  // 1: visiting, 2: done
    std::function<void(const TemplateDef*)> visit = [&](const TemplateDef* t) {
        if (state[t] == 2) return;
        if (state[t] == 1) throw CompileError("cyclic inheritance: " + t->name + " extends itself", t->pos);
        state[t] = 1;
        for (const ParentRef& p : t->parents) {
            auto it = byName.find(typeNameOf(*p.type));
            if (it != byName.end()) visit(it->second);
        }
        state[t] = 2;
        out.push_back(t);
    };
    for (const TemplateDef* t : ts) visit(t);
    return out;
}

const ClassInfo* Compiler::superclassOf(const ClassInfo& info) const {
    for (std::size_t k = 1; k < info.linearization.size(); ++k) {
        const ClassInfo* c = globals_.findTypeByKey(info.linearization[k]);
        if (c && c->kind == ClassKind::Class && !c->builtin) return c;
    }
    return nullptr;
}

ClassInfo Compiler::buildClassInfo(const TemplateDef& t, const std::string& typeKey) const {
    ClassInfo c;
    c.name = t.name;
    c.key = typeKey;
    c.kind = t.kind == TemplateKind::Trait ? ClassKind::Trait
           : t.kind == TemplateKind::Object ? ClassKind::Object : ClassKind::Class;
    c.isCase = t.isCase;
    c.isAbstract = t.mods.isAbstract || c.kind == ClassKind::Trait;
    c.isFinal = t.mods.isFinal || c.kind == ClassKind::Object;
    c.isSealed = t.mods.isSealed;

    // Parents.
    std::vector<const ClassInfo*> parents;
    for (std::size_t k = 0; k < t.parents.size(); ++k) {
        const ParentRef& p = t.parents[k];
        const ClassInfo& pi = resolveType(*p.type, p.pos);
        if (pi.kind == ClassKind::Object) throw CompileError("an object cannot be extended: " + pi.name, p.pos);
        if (pi.isFinal) throw CompileError("cannot extend final class " + pi.name, p.pos);
        if (k > 0 && pi.kind != ClassKind::Trait)
            throw CompileError("class " + pi.name + " is not a trait; only the first parent may be a class", p.pos);
        if (c.isCase && pi.isCase && pi.kind == ClassKind::Class)
            throw CompileError("case-to-case inheritance is prohibited: case class " + t.name +
                                   " extends case class " + pi.name, p.pos);
        if (p.hasArgs && c.kind == ClassKind::Trait)
            throw CompileError("a trait may not pass arguments to its parents", p.pos);
        if (p.hasArgs && pi.kind == ClassKind::Trait && pi.primaryArity == 0)
            throw CompileError("trait " + pi.name + " takes no arguments", p.pos);
        parents.push_back(&pi);
    }
    std::vector<std::vector<std::string>> lins;
    for (const ClassInfo* p : parents) lins.push_back(p->linearization);
    if (lins.empty()) lins.push_back({kAnyRefKey, kAnyKey});
    c.linearization = linearize(c.key, lins);

    // A trait that extends a class may only be mixed into its subclasses (SLS 5.1.2).
    if (!parents.empty()) {
        const std::vector<std::string>& firstLin = parents[0]->linearization;
        for (std::size_t k = 1; k < parents.size(); ++k) {
            const ClassInfo* s = superclassOf(*parents[k]);
            if (s && std::find(firstLin.begin(), firstLin.end(), s->key) == firstLin.end())
                throw CompileError("illegal inheritance: superclass " + parents[0]->name +
                                       " is not a subclass of the superclass " + s->name +
                                       " of the mixin trait " + parents[k]->name, t.parents[k].pos);
        }
    }

    // Inherited public members, the more specific ancestors last; a member is
    // concrete when any ancestor defines it.
    for (auto it = c.linearization.rbegin(); it != c.linearization.rend(); ++it) {
        if (*it == c.key) continue;
        const ClassInfo* a = globals_.findTypeByKey(*it);
        if (!a) continue;
        c.mutableInstances = c.mutableInstances || a->mutableInstances;
        for (const auto& [name, m] : a->members) {
            if (m.key != name) continue;  // an ancestor's private member is not inherited
            MemberInfo merged = m;
            auto found = c.members.find(name);
            if (found != c.members.end() && found->second.concrete) merged.concrete = true;
            c.members[name] = merged;
        }
    }

    // Own members.
    std::unordered_set<std::string> own;
    auto addOwn = [&](const std::string& name, MemberKind kind, bool isPublic, bool concrete, SourcePos pos) {
        if (!own.insert(name).second) throw CompileError(name + " is already defined in " + t.name, pos);
        auto inherited = c.members.find(name);
        const bool inheritedConcrete =
            inherited != c.members.end() && inherited->second.concrete && inherited->second.key == name;
        c.members[name] = MemberInfo{kind, isPublic ? name : privateKey(c.key, name), concrete || inheritedConcrete};
    };
    bool hasStatements = !t.ctorParams.empty();
    bool ownVar = false;
    for (const Param& p : t.ctorParams) {
        if (p.byName) throw CompileError("by-name parameters are not supported yet", p.pos);
        if (p.defaultValue) throw CompileError("default parameter values are not implemented yet", p.pos);
        // Plain parameters are private fields (reachable from the methods).
        const bool isPublic = (p.isVal || p.isVar || c.isCase) && !p.mods.isPrivate;
        addOwn(p.name, p.isVar ? MemberKind::Var : MemberKind::Val, isPublic, true, p.pos);
        if (p.isVar) {
            addOwn(setterName(p.name), MemberKind::Def, isPublic, true, p.pos);
            ownVar = true;
        }
        c.ctorParams.push_back(p.name);
        if (c.isCase) c.fields.push_back(c.members.at(p.name).key);
    }
    c.primaryArity = t.ctorParams.size();
    c.primaryVariadic = !t.ctorParams.empty() && t.ctorParams.back().repeated;
    if (c.isCase && c.kind == ClassKind::Class)
        for (std::size_t k = 1; k <= c.fields.size(); ++k)
            c.members["_" + std::to_string(k)] = MemberInfo{MemberKind::ParamlessDef, "_" + std::to_string(k), true};
    for (const NodePtr& s : t.body) {
        switch (s->kind) {
            case NodeKind::ValDef: {
                const auto& v = as<ValDef>(*s);
                addOwn(v.name, v.isLazy ? MemberKind::LazyVal : v.isVar ? MemberKind::Var : MemberKind::Val,
                       !v.mods.isPrivate, v.rhs != nullptr, v.pos);
                if (v.isVar) {
                    addOwn(setterName(v.name), MemberKind::Def, !v.mods.isPrivate, true, v.pos);
                    ownVar = true;
                }
                if (v.rhs) hasStatements = true;
                break;
            }
            case NodeKind::DefDef: {
                const auto& d = as<DefDef>(*s);
                if (d.isMain()) throw CompileError("@main methods must be top-level definitions", d.pos);
                if (d.name == "this") {
                    if (c.kind != ClassKind::Class)
                        throw CompileError("auxiliary constructors are only allowed in classes", d.pos);
                    const std::size_t arity = paramsOfDef(d).size();
                    if (arity == c.primaryArity ||
                        std::find(c.auxArities.begin(), c.auxArities.end(), arity) != c.auxArities.end())
                        throw CompileError("constructors of " + t.name +
                                               " must differ in their number of parameters (D31)", d.pos);
                    c.auxArities.push_back(arity);
                    break;
                }
                addOwn(d.name, d.paramLists.empty() ? MemberKind::ParamlessDef : MemberKind::Def,
                       !d.mods.isPrivate, d.body != nullptr, d.pos);
                break;
            }
            case NodeKind::TemplateDef:
                throw CompileError("classes, traits and objects must be defined at the top level of a file",
                                   s->pos);
            case NodeKind::Import:
                break;
            default:
                hasStatements = true;  // an expression statement runs in the constructor
                break;
        }
    }
    c.mutableInstances = c.mutableInstances || ownVar;
    c.hasInit = c.kind != ClassKind::Trait || hasStatements;

    // Every member of a concrete class must be defined.
    if (!c.isAbstract) {
        std::vector<std::string> missing;
        for (const auto& [name, m] : c.members)
            if (!m.concrete) missing.push_back(name);
        if (!missing.empty()) {
            std::sort(missing.begin(), missing.end());
            const MemberInfo& m = c.members.at(missing.front());
            const char* what = m.kind == MemberKind::Def || m.kind == MemberKind::ParamlessDef ? "def" : "val";
            throw CompileError(std::string(kindWord(c.kind)) + " " + t.name + " needs to be abstract, since " +
                                   what + " " + missing.front() + " is not defined", t.pos);
        }
    }
    return c;
}

// A class and an object of the same name in one unit are companions.
void Compiler::linkCompanions(const std::vector<const TemplateDef*>& ts) {
    for (const TemplateDef* o : ts) {
        if (o->kind != TemplateKind::Object) continue;
        for (const TemplateDef* k : ts) {
            if (k->kind == TemplateKind::Object || k->name != o->name) continue;
            ClassInfo* cls = globals_.mutableTypeByKey(globals_.findType(k->name)->key);
            ClassInfo* obj = globals_.mutableTypeByKey(globals_.findType(o->name + ".type")->key);
            cls->companionTermKey = globals_.binding(o->name)->key;
            cls->companionTypeKey = obj->key;
            obj->companionTypeKey = cls->key;
            for (const NodePtr& s : o->body) {
                if (s->kind != NodeKind::DefDef || as<DefDef>(*s).synthetic) continue;
                if (as<DefDef>(*s).name == "apply") cls->companionHasApply = true;
                if (as<DefDef>(*s).name == "unapply") cls->companionHasUnapply = true;
            }
        }
    }
}

// The prototype chain MAKE_CLASS installs: the linearization without the class
// itself. Product's natives stand in for the synthesised case-class members;
// they sit right before AnyRef, after every user parent, so a toString a case
// class inherits from a user class wins, as in Scala.
std::vector<std::string> Compiler::runtimeChain(const ClassInfo& info) const {
    std::vector<std::string> chain(info.linearization.begin() + 1, info.linearization.end());
    const bool product = std::any_of(info.linearization.begin(), info.linearization.end(),
                                     [&](const std::string& k) {
                                         const ClassInfo* c = globals_.findTypeByKey(k);
                                         return k == info.key ? info.isCase : c && c->isCase;
                                     });
    if (product && std::find(chain.begin(), chain.end(), kProductKey) == chain.end())
        chain.insert(std::find(chain.begin(), chain.end(), std::string(kAnyRefKey)), kProductKey);
    return chain;
}

std::string Compiler::ctorKeyFor(const ClassInfo& info, std::size_t argc, SourcePos pos) const {
    if (argc == info.primaryArity || (info.primaryVariadic && argc + 1 >= info.primaryArity))
        return kPrimaryCtorKey;
    if (std::find(info.auxArities.begin(), info.auxArities.end(), argc) != info.auxArities.end())
        return auxCtorKey(argc);
    throw CompileError("wrong number of arguments for the constructor of " + info.name + ": " +
                           std::to_string(argc), pos);
}

void Compiler::compileTemplate(const TemplateDef& t, const ClassInfo& info) {
    const ClassInfo* companion =
        info.companionTypeKey.empty() ? nullptr : globals_.findTypeByKey(info.companionTypeKey);
    const TemplateScope scope{&info, companion, t.selfName};
    const TemplateScope* saved = tmpl_;
    tmpl_ = &scope;
    const std::vector<std::string> chain = runtimeChain(info);
    for (const std::string& k : chain) emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(k), t.pos, +1);
    std::vector<std::string> keys;
    for (const NodePtr& s : t.body) {  // methods
        if (s->kind != NodeKind::DefDef) continue;
        const auto& d = as<DefDef>(*s);
        if (d.name == "this" || !d.body) continue;
        keys.push_back(info.members.at(d.name).key);
        compileFunction(d.name, paramsOfDef(d), *d.body, FnShape::Method, d.pos);
    }
    auto setterFor = [&](const std::string& name, SourcePos pos) {
        keys.push_back(info.members.at(setterName(name)).key);
        compileSetter(info.members.at(name).key, pos);
    };
    for (const Param& p : t.ctorParams)
        if (p.isVar) setterFor(p.name, p.pos);
    for (const NodePtr& s : t.body)
        if (s->kind == NodeKind::ValDef && as<ValDef>(*s).isVar) setterFor(as<ValDef>(*s).name, s->pos);
    if (info.hasInit) {
        keys.push_back(kPrimaryCtorKey);
        compileConstructor(t, info);
    }
    for (const NodePtr& s : t.body) {
        if (s->kind != NodeKind::DefDef || as<DefDef>(*s).name != "this") continue;
        keys.push_back(auxCtorKey(paramsOfDef(as<DefDef>(*s)).size()));
        compileAuxConstructor(as<DefDef>(*s), info);
    }
    BytecodeModule::ClassSpecData spec;
    spec.displayName = info.name;
    spec.key = info.key;
    spec.parentCount = static_cast<std::uint32_t>(chain.size());
    spec.memberKeys = keys;
    spec.fields = info.fields;
    spec.flags = (info.isCase ? BytecodeModule::kClassCase : 0u) |
                 (info.isCase && info.kind == ClassKind::Object ? BytecodeModule::kClassCaseObject : 0u) |
                 (info.mutableInstances ? BytecodeModule::kClassMutableInstances : 0u) |
                 (info.kind == ClassKind::Trait ? BytecodeModule::kClassTrait : 0u) |
                 (info.kind == ClassKind::Object ? BytecodeModule::kClassObject : 0u);
    emit(Op::MAKE_CLASS, fn_->mod->addClassSpec(spec), t.pos,
         1 - static_cast<int>(chain.size() + keys.size()));
    emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(info.key), t.pos, -1);
    tmpl_ = saved;
}

// Emits `target.<init>(this, args)` and stores the new `this` (Design note 10).
void Compiler::compileInitCall(const ClassInfo& target, const std::vector<NodePtr>& args, SourcePos pos) {
    emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(target.key), pos, +1);
    emit(Op::PUSH_LOCAL, 0, pos, +1);
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Splice || arg->kind == NodeKind::NamedArg)
            throw CompileError("constructor arguments must be plain expressions here", arg->pos);
        compileExpr(*arg);
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    const std::string key = target.kind == ClassKind::Trait ? kPrimaryCtorKey : ctorKeyFor(target, n, pos);
    emit(Op::INVOKE_INIT, fn_->mod->addSendSite(key, n), pos, -static_cast<int>(n) - 1);
    emit(Op::STORE_LOCAL, 0, pos, -1);
}

// <init>(this, params): superclass initialiser, trait initialisers in Scala's
// order, parameter fields, lazy holders, then the template statements; returns
// the final `this`.
void Compiler::compileConstructor(const TemplateDef& t, const ClassInfo& info) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(info.name + ".<init>");
    mod->setMethod(true);
    FunctionState fs;
    fs.mod = mod.get();
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    const LocalInfo self{newSlot(), BindingKind::Param, false, false};
    fs.scopes.back()["this"] = self;
    if (!t.selfName.empty()) fs.scopes.back()[t.selfName] = self;
    for (const Param& p : t.ctorParams)
        fs.scopes.back()[p.name] = LocalInfo{newSlot(), BindingKind::Param, false, false};
    const int arity = 1 + static_cast<int>(t.ctorParams.size());
    mod->setArity(arity);
    mod->setVariadic(info.primaryVariadic);
    static const std::vector<NodePtr> noArgs;
    for (const ParentRef& p : t.parents)
        for (const auto& arg : p.args) analyseCaptures(t.ctorParams, *arg);
    for (const NodePtr& s : t.body) {
        if (s->kind == NodeKind::ValDef && as<ValDef>(*s).rhs && !as<ValDef>(*s).isLazy)
            analyseCaptures(t.ctorParams, *as<ValDef>(*s).rhs);
        else if (s->kind != NodeKind::ValDef && s->kind != NodeKind::DefDef && s->kind != NodeKind::Import)
            analyseCaptures(t.ctorParams, *s);
    }
    if (info.kind != ClassKind::Trait) {  // a trait's initialiser runs only its own body
        const ClassInfo* super = superclassOf(info);
        if (super) {
            const bool direct = !t.parents.empty() && &resolveType(*t.parents[0].type, t.pos) == super;
            compileInitCall(*super, direct ? t.parents[0].args : noArgs, t.pos);
        }
        std::unordered_set<std::string> bySuper;
        if (super) bySuper.insert(super->linearization.begin(), super->linearization.end());
        for (auto it = info.linearization.rbegin(); it != info.linearization.rend(); ++it) {
            if (*it == info.key || bySuper.count(*it)) continue;
            const ClassInfo* tr = globals_.findTypeByKey(*it);
            if (!tr || tr->kind != ClassKind::Trait || tr->builtin || !tr->hasInit) continue;
            const ParentRef* ref = nullptr;
            for (const ParentRef& p : t.parents)
                if (&resolveType(*p.type, p.pos) == tr) ref = &p;
            const std::size_t n = ref && ref->hasArgs ? ref->args.size() : 0;
            if (n != tr->primaryArity)
                throw CompileError(ref ? "wrong number of arguments for trait " + tr->name
                                       : "parameterized trait " + tr->name +
                                             " is indirectly implemented; it must be extended directly so "
                                             "that arguments can be passed", t.pos);
            compileInitCall(*tr, ref && ref->hasArgs ? ref->args : noArgs, t.pos);
        }
    }
    for (std::size_t k = 0; k < t.ctorParams.size(); ++k) {  // parameter fields
        emit(Op::PUSH_LOCAL, 1 + k, t.pos, +1);
        emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(t.ctorParams[k].name).key), t.pos, -1);
    }
    for (const NodePtr& s : t.body) {  // lazy holders: a thunk method per member (Q12)
        if (s->kind != NodeKind::ValDef || !as<ValDef>(*s).isLazy) continue;
        const auto& v = as<ValDef>(*s);
        compileFunction("<lazy " + v.name + ">", {}, *v.rhs, FnShape::Method, v.pos);
        emit(Op::MAKE_LAZY, 0, v.pos, 0);
        emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(v.name).key), v.pos, -1);
    }
    for (const NodePtr& s : t.body) {  // template statements, in order
        if (s->kind == NodeKind::ValDef) {
            const auto& v = as<ValDef>(*s);
            if (v.isLazy || !v.rhs) continue;
            compileExpr(*v.rhs);
            emit(Op::STORE_FIELD, fn_->mod->addSymbol(info.members.at(v.name).key), v.pos, -1);
        } else if (s->kind != NodeKind::DefDef && s->kind != NodeKind::Import) {
            compileExpr(*s);
            emit(Op::POP, 0, s->pos, -1);
        }
    }
    emit(Op::PUSH_LOCAL, 0, t.pos, +1);
    emit(Op::RETURN, 0, t.pos, -1);
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), t.pos, +1);
}

// def this(ps) = { this(args); stats }: `<initN>(this, ps)`.
void Compiler::compileAuxConstructor(const DefDef& d, const ClassInfo& info) {
    const auto& params = paramsOfDef(d);
    const Node& body = *d.body;
    const bool block = body.kind == NodeKind::Block;
    const Node* first = block ? (as<Block>(body).stats.empty() ? nullptr : as<Block>(body).stats[0].get())
                              : &body;
    const bool selfCall = first && first->kind == NodeKind::Apply &&
                          as<Apply>(*first).fn->kind == NodeKind::Ident &&
                          as<Ident>(*as<Apply>(*first).fn).name == "this";
    if (!selfCall)
        throw CompileError("an auxiliary constructor must begin with a call to another constructor: "
                           "this(...)", d.pos);
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(info.name + ".<init" + std::to_string(params.size()) + ">");
    mod->setMethod(true);
    FunctionState fs;
    fs.mod = mod.get();
    fs.scopes.emplace_back();
    FunctionState* saved = fn_;
    fn_ = &fs;
    fs.scopes.back()["this"] = LocalInfo{newSlot(), BindingKind::Param, false, false};
    for (const Param& p : params)
        fs.scopes.back()[p.name] = LocalInfo{newSlot(), BindingKind::Param, false, false};
    const int arity = 1 + static_cast<int>(params.size());
    mod->setArity(arity);
    analyseCaptures(params, body);
    compileInitCall(info, as<Apply>(*first).args, d.pos);
    if (block) {
        compileStats(as<Block>(body).stats, 1, body.pos);
        emit(Op::POP, 0, body.pos, -1);
    }
    emit(Op::PUSH_LOCAL, 0, d.pos, +1);
    emit(Op::RETURN, 0, d.pos, -1);
    mod->setLocalCount(fs.nextSlot - arity);
    mod->setMaxStack(fs.maxDepth);
    fn_ = saved;
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), d.pos, +1);
}

// x_=(this, v): the instance of a class with var fields is mutable.
void Compiler::compileSetter(const std::string& fieldKey, SourcePos pos) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName(setterName(fieldKey));
    mod->setMethod(true);
    mod->setArity(2);
    mod->setMaxStack(2);
    mod->emit(Op::PUSH_LOCAL, 0, pos.line);
    mod->emit(Op::PUSH_LOCAL, 1, pos.line);
    mod->emit(Op::SET_FIELD, mod->addSymbol(fieldKey), pos.line);
    mod->emit(Op::PUSH_UNIT, 0, pos.line);
    mod->emit(Op::RETURN, 0, pos.line);
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), pos, +1);
}

// object O: a lazily initialised singleton (DESIGN §4.2): a lazy holder whose
// thunk instantiates O's class once, on first access.
void Compiler::compileObjectHolder(const ClassInfo& info, const std::string& termKey, SourcePos pos) {
    auto mod = std::make_unique<BytecodeModule>();
    mod->setName("<object " + info.name + ">");
    mod->setMaxStack(1);
    mod->emit(Op::PUSH_GLOBAL, mod->addSymbol(info.key), pos.line);
    mod->emit(Op::NEW, mod->addSendSite(kPrimaryCtorKey, 0), pos.line);
    mod->emit(Op::RETURN, 0, pos.line);
    emit(Op::MAKE_FN, fn_->mod->addBlock(std::move(mod)), pos, +1);
    emit(Op::MAKE_LAZY, 0, pos, 0);
    emit(Op::STORE_GLOBAL, fn_->mod->addSymbol(termKey), pos, -1);
}

void Compiler::compileNew(const New& n) { compileNewOf(resolveType(*n.type, n.pos), n.args, n.pos); }

void Compiler::compileNewOf(const ClassInfo& info, const std::vector<NodePtr>& args, SourcePos pos) {
    if (info.kind == ClassKind::Trait) throw CompileError(info.name + " is a trait; it cannot be instantiated", pos);
    if (info.kind == ClassKind::Object) throw CompileError("Not found: type " + info.name, pos);
    if (info.isAbstract) throw CompileError(info.name + " is abstract; it cannot be instantiated", pos);
    if (info.builtin && info.key.rfind("@Tuple", 0) == 0) {  // new TupleN(...): the tuple itself
        if (args.size() != info.primaryArity)
            throw CompileError("wrong number of arguments for the constructor of " + info.name, pos);
        for (const auto& arg : args) compileExpr(*arg);
        emit(Op::MAKE_TUPLE, args.size(), pos, 1 - static_cast<int>(args.size()));
        return;
    }
    emit(Op::PUSH_GLOBAL, fn_->mod->addSymbol(info.key), pos, +1);
    const bool spread = !args.empty() && args.back()->kind == NodeKind::Splice;
    for (std::size_t k = 0; k < args.size(); ++k) {
        const Node& arg = *args[k];
        if (arg.kind == NodeKind::NamedArg) throw CompileError("named arguments are not implemented yet", arg.pos);
        if (arg.kind == NodeKind::Splice) {
            if (k + 1 != args.size()) throw CompileError("a splice must be the last argument", arg.pos);
            if (!info.primaryVariadic)
                throw CompileError("the constructor of " + info.name + " takes no repeated parameter", arg.pos);
            compileExpr(*as<Splice>(arg).expr);
        } else {
            compileExpr(arg);
        }
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    if (spread)
        emit(Op::NEW_SPREAD, fn_->mod->addSendSite(kPrimaryCtorKey, n - 1), pos, -static_cast<int>(n));
    else
        emit(Op::NEW, fn_->mod->addSendSite(ctorKeyFor(info, n, pos), n), pos, -static_cast<int>(n));
}

void Compiler::compileSuperSend(const std::string& name, const std::vector<NodePtr>& args, SourcePos pos) {
    if (!tmpl_) throw CompileError("super can be used only inside a class, trait or object", pos);
    loadThis(pos);
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Splice || arg->kind == NodeKind::NamedArg)
            throw CompileError("super calls take plain arguments", arg->pos);
        compileExpr(*arg);
    }
    const auto n = static_cast<std::uint32_t>(args.size());
    emit(Op::SEND_SUPER, fn_->mod->addSuperSite(name, n, tmpl_->info->key), pos, -static_cast<int>(n));
}

// recv.m(a, k = v): named arguments reach native methods (Product.copy)
// through SEND_KW (Open question Q9).
void Compiler::compileNamedSend(const Select& sel, const std::vector<NodePtr>& args, SourcePos pos) {
    compileExpr(*sel.qualifier);
    std::vector<std::string> keywords;
    std::size_t positional = 0;
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::NamedArg) {
            const std::string& name = as<NamedArg>(*arg).name;
            if (std::find(keywords.begin(), keywords.end(), name) != keywords.end())
                throw CompileError("parameter " + name + " is specified twice", arg->pos);
            keywords.push_back(name);
        } else {
            if (!keywords.empty())
                throw CompileError("positional after named argument", arg->pos);
            if (arg->kind == NodeKind::Splice)
                throw CompileError("splices are only supported in function calls", arg->pos);
            ++positional;
        }
    }
    for (const auto& arg : args)
        compileExpr(arg->kind == NodeKind::NamedArg ? *as<NamedArg>(*arg).value : *arg);
    emit(Op::SEND_KW, fn_->mod->addKwSendSite(selectKey(sel.name), static_cast<std::uint32_t>(positional), keywords),
         pos, -static_cast<int>(args.size()));
}

} // namespace protoScala
```

Add `src/compiler/CompileTemplates.cpp` to `protoscala_compiler`. `compileTuple` is defined by Task 8.

- [ ] **Step 5: Units hoist templates**

In `Compiler::compileUnit` (lines 653-769):

- Step 1 collects the templates and declares their names:

```cpp
        std::vector<const TemplateDef*> templates;
        std::unordered_map<const TemplateDef*, std::string> typeKeys;
        // ... inside the loop over unit.stats:
            } else if (s->kind == NodeKind::TemplateDef) {
                const auto& t = as<TemplateDef>(*s);
                templates.push_back(&t);
                if (t.kind == TemplateKind::Object) {
                    const std::string& termKey = globals_.declare(t.name, BindingKind::Object);
                    typeKeys[&t] = globals_.declareType(t.name + ".type");
                    if (mode == UnitMode::Repl && !t.synthetic)
                        out.definitions.push_back(
                            {std::string("// defined ") + (t.isCase ? "case object " : "object ") + t.name, termKey});
                } else {
                    typeKeys[&t] = globals_.declareType(t.name);
                    if (mode == UnitMode::Repl)
                        out.definitions.push_back(
                            {std::string("// defined ") +
                                 (t.kind == TemplateKind::Trait ? "trait " : t.isCase ? "case class " : "class ") + t.name,
                             typeKeys[&t]});
                }
            }
```

- After the `@main` validation, describe the templates:

```cpp
        const std::vector<const TemplateDef*> sorted = sortTemplates(templates);
        for (const TemplateDef* t : sorted) globals_.defineType(buildClassInfo(*t, typeKeys.at(t)));
        linkCompanions(sorted);
```

- Step 2 (hoisting) starts with the templates, then the object holders, then the Phase 1 defs and lazy vals:

```cpp
        for (const TemplateDef* t : sorted) compileTemplate(*t, *globals_.findTypeByKey(typeKeys.at(t)));
        for (const TemplateDef* t : sorted)
            if (t->kind == TemplateKind::Object)
                compileObjectHolder(*globals_.findTypeByKey(typeKeys.at(t)), globals_.binding(t->name)->key, t->pos);
```

- Steps 3 and 4 skip `NodeKind::TemplateDef` statements (like `DefDef` and `Import`).

`ReplDefinition` entries whose text starts with `// defined` are echoed verbatim (`Session::evaluate` already echoes a non-`val`/`var` definition's text as is).

The callers of `compileFunction` pass the new shape: lambdas `l.ownsReturn ? FnShape::Def : FnShape::Lambda`, local and top-level `def`s `FnShape::Def`, lazy thunks `FnShape::Lambda`.

`Session::Session` (`src/repl/Session.cpp:45-48`) and the `EvalHarness` constructor define the built-in types right after declaring the built-in globals:

```cpp
    for (ClassInfo& t : builtinTypes()) globals_.defineBuiltinType(std::move(t));
```

- [ ] **Step 6: `NEW_SPREAD`**

`Opcodes.h`: `NEW_SPREAD = 78,  // [cls a1..an list] -> [obj]   operand: SendSite (constructor key, n)`; `opName`/`commentFor` as for `NEW`. In the engine:

```cpp
                case Op::NEW_SPREAD: {
                    const auto& site = mod.constAt(operand);
                    const unsigned n = site.argc;
                    const proto::ProtoObject** base = sp - n - 2;  // [cls a1..an list]
                    const proto::ProtoObject* listObj = sp[-1];
                    if (!isListFast(listObj))
                        throw ScalaError("ClassCastException",
                                         typeName(&frame, L, listObj) + " cannot be spliced as arguments");
                    const proto::ProtoList* list = listObj->asList(&frame);
                    const unsigned extra = static_cast<unsigned>(list->getSize(&frame));
                    const proto::ProtoObject* r;
                    {
                        proto::ProtoContext argScope(frame.space, &frame);
                        argScope.resizeAutomaticLocals(n + extra + 1);
                        const proto::ProtoObject** a = argScope.getAutomaticLocals();
                        for (unsigned k = 0; k <= n; ++k) a[k] = base[k];
                        for (unsigned k = 0; k < extra; ++k)
                            a[n + 1 + k] = list->getAt(&argScope, static_cast<int>(k));
                        r = instantiate(&argScope, a, site.symbol, n + extra);
                        argScope.returnValue = r;
                    }
                    base[0] = r;
                    sp = base + 1;
                    continue;
                }
```

- [ ] **Step 7: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release -R '07-classes|Compiler' --output-on-failure`
Expected: PASS. Then `ctest --test-dir build_release` → 100% (and under `PROTOCORE_HEAP_LIMIT_CELLS=20000` for the `07-classes` fixtures: `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release -R 07-classes`).

```bash
git add src/compiler src/runtime/ExecutionEngine.cpp src/repl/Session.cpp CMakeLists.txt tests/unit/EvalHarness.h tests/unit/test_compiler.cpp tests/conformance/07-classes
git commit -m "classes, traits and objects: linearized prototypes, constructors, members, super

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 8: Case classes, case objects and tuples

**Files:**
- Create: `src/runtime/PrimitiveSupport.h`, `src/runtime/ProductPrimitives.cpp`, `tests/conformance/08-case-classes/*.scala`
- Modify: `src/runtime/Primitives.h`, `src/runtime/Primitives.cpp`, `src/compiler/Compiler.cpp` (`compileTuple`), `CMakeLists.txt`, `tests/unit/test_compiler.cpp`, `tests/unit/test_primitives.cpp`

**Interfaces:**
- Consumes: Tasks 3 (synthesised companions), 5 (tuple prototypes, `__fields__`/`__prefix__`/`__tuple__`), 6 (`construct`, `SEND_KW`, `scalaHash`), 7 (case-class `ClassInfo`, `Product` in the runtime chain, `C(args)` → `new C(args)`).
- Produces:
  - `src/runtime/PrimitiveSupport.h`: the `PRIM` macro and the argument helpers of `Primitives.cpp` (`layoutOf`, `argCount`, `wrongArgCount`, `expectArgs`, `arg`, `wrongType`, `intArg`, `asStr`, `stringArg`, `boolean`, `str`), moved unchanged into `namespace protoScala::prim` (inline functions) so several primitive files share them.
  - `void installProductPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L);` (declared in `Primitives.h`, called by `installPrimitives`): on `productProto` — `toString`, `equals`, `hashCode`, `productArity`, `productPrefix`, `productElement`, `copy` (positional and named arguments), `_1`..`_22`; on each `TupleN` prototype — a native `<init>`.
  - `(a, b, ...)` compiles to `MAKE_TUPLE n` (2 ≤ n ≤ 22; more is a compile error, D32).

- [ ] **Step 1: Failing fixtures**

`tests/conformance/08-case-classes/`:

`case-class-basics-indent.scala`
```scala
// EXPECT: Point(1,2) true false 1 2 Point(1,5) Point(3,2) 1 3
case class Point(x: Int, y: Int):
  def norm1 = x.abs + y.abs

@main def run(): Unit =
  val p = Point(1, 2)
  val q = Point(1, 2)
  println(p.toString + " " + (p == q) + " " + (p eq q) + " " + p.x + " " + p._2 + " " +
    p.copy(y = 5) + " " + p.copy(3) + " " + (if p.hashCode == q.hashCode then 1 else 0) + " " + p.norm1)
```

`case-class-basics-braces.scala`
```scala
// EXPECT: Point(1,2) true false 1 2 Point(1,5) Point(3,2) 1 3
case class Point(x: Int, y: Int) {
  def norm1 = x.abs + y.abs
}

@main def run(): Unit = {
  val p = Point(1, 2)
  val q = Point(1, 2)
  println(p.toString + " " + (p == q) + " " + (p eq q) + " " + p.x + " " + p._2 + " " +
    p.copy(y = 5) + " " + p.copy(3) + " " + (if (p.hashCode == q.hashCode) 1 else 0) + " " + p.norm1)
}
```

`case-class-hashcodes.scala` (the values `scalac` 3.9.0 prints, Design note 11)
```scala
// EXPECT: -694993394 -1480185351 1971805870 1316541600 -1756661775 67081517 96354
case class Point(x: Int, y: Int)
case class Box(s: String)
case object Unique
case class Empty()

@main def run(): Unit =
  println(Point(1, 2).hashCode.toString + " " + Box("abc").hashCode + " " + (1, "a").hashCode + " " +
    (1, 2).hashCode + " " + Unique.hashCode + " " + Empty().hashCode + " " + "abc".hashCode)
```

`case-object.scala`
```scala
// EXPECT: Unique true Unique Empty()
case object Unique
case class Empty()
@main def run(): Unit =
  val u = Unique
  println(u.toString + " " + (u == Unique) + " " + Unique.productPrefix + " " + Empty())
```

`tuples.scala`
```scala
// EXPECT: (1,a) 1 a true (1,(2,3)) 3 (1,a,true) 3
@main def run(): Unit =
  val t = (1, "a")
  val nested = (1, (2, 3))
  val t3 = (1, "a", true)
  println(t.toString + " " + t._1 + " " + t._2 + " " + (t == (1, "a")) + " " + nested + " " +
    nested._2._2 + " " + t3 + " " + t3.productArity)
```

`tuple-too-large.scala`
```scala
// EXPECT-ERROR: tuples of more than 22 elements are not supported
val t = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23)
```

`product-members.scala`
```scala
// EXPECT: 3 Person Ada 36 true
case class Person(name: String, age: Int, admin: Boolean)
@main def run(): Unit =
  val p = Person("Ada", 36, true)
  println(p.productArity.toString + " " + p.productPrefix + " " + p.productElement(0) + " " +
    p.productElement(1) + " " + p.productElement(2))
```

`product-element-out-of-range.scala` (Scala 3.9 message)
```scala
// EXPECT-ERROR: IndexOutOfBoundsException: Index out of range: 2
case class P(a: Int, b: Int)
@main def run(): Unit = println(P(1, 2).productElement(2))
```

`copy-unknown-name.scala`
```scala
// EXPECT-ERROR: copy has no parameter named z
case class P(a: Int, b: Int)
@main def run(): Unit = println(P(1, 2).copy(z = 3))
```

`companion-user-apply.scala`
```scala
// EXPECT: Money(250) Money(3)
case class Money(cents: Int)
object Money:
  def apply(s: String): Money = new Money((s.toDouble * 100).round.toInt)

@main def run(): Unit =
  println(Money("2.50").toString + " " + new Money(3))
```

`case-to-case.scala`
```scala
// EXPECT-ERROR: case-to-case inheritance is prohibited
case class A(x: Int)
case class B(y: Int) extends A(y)
```

`case-class-varargs.scala` (D12: varargs are a `List`)
```scala
// EXPECT: Poly(List(1, 2, 3)) 3
case class Poly(coeffs: Int*)
@main def run(): Unit =
  val p = Poly(1, 2, 3)
  println(p.toString + " " + p.coeffs.length)
```

`nested-equality.scala`
```scala
// EXPECT: true false true
case class Inner(v: Double)
case class Outer(i: Inner, tag: String)
@main def run(): Unit =
  val a = Outer(Inner(1.0), "a")
  println((a == Outer(Inner(1.0), "a")).toString + " " + (a == Outer(Inner(2.0), "a")) + " " +
    (a.hashCode == Outer(Inner(1.0), "a").hashCode))
```

`inherited-tostring-wins.scala` (Scala synthesises no `toString` when a non-`AnyRef` parent defines one)
```scala
// EXPECT: <shape 2.0> Sq(3.0)
trait Named:
  def size: Double
  override def toString = "<shape " + size + ">"
case class Circle(size: Double) extends Named
case class Sq(size: Double)

@main def run(): Unit = println(Circle(2.0).toString + " " + Sq(3.0))
```

Update `tests/unit/test_compiler.cpp` (`Compiler.SemanticErrors`): replace the "tuples are not implemented yet" expectation by `EXPECT_TRUE(has(listing("val t = (1, 2)"), "MAKE_TUPLE 2"));`.

Run: `ctest --test-dir build_release -R '08-case-classes' --output-on-failure` → FAIL.

- [ ] **Step 2: Shared primitive helpers**

Create `src/runtime/PrimitiveSupport.h` with the header comment "Helpers shared by the primitive files (moved from Primitives.cpp, unchanged)", `#include "runtime/Errors.h"`, `"runtime/ExecutionEngine.h"`, `"runtime/Values.h"`, `"protoCore.h"`, the `PRIM` macro, and — as `inline` functions in `namespace protoScala::prim` — `layoutOf`, `argCount`, `wrongArgCount`, `expectArgs`, `arg`, `wrongType`, `intArg`, `asStr`, `stringArg`, `boolean`, `str` (bodies from `src/runtime/Primitives.cpp:22-111`). `Primitives.cpp` includes it, drops its copies and adds `using namespace prim;` inside its anonymous namespace.

- [ ] **Step 3: Product members**

`src/runtime/ProductPrimitives.cpp`:

```cpp
/*
 * The members Scala synthesises for case classes and tuples (DESIGN §4.5),
 * implemented once as natives on the Product prototype and driven by each
 * class's metadata: __fields__ (the product elements' attribute keys),
 * __prefix__ (productPrefix) and, for tuples, __tuple__. Product sits after
 * every user parent in a case class's chain (Compiler::runtimeChain), so a
 * member the class or a user parent defines wins, as in Scala.
 */
#include "runtime/Hashing.h"
#include "runtime/PrimitiveSupport.h"
#include "runtime/Primitives.h"

#include <vector>

namespace protoScala {

namespace {

using namespace prim;
using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;
using proto::ProtoString;

// The element keys of `self`'s class; nullptr for a case object.
const ProtoList* fieldsOf(ProtoContext* ctx, const ProtoObject* self) {
    const ProtoObject* f = self->getAttribute(ctx, layoutOf().fieldsKey);
    return isListFast(f) ? f->asList(ctx) : nullptr;
}

const ProtoString* keyAt(ProtoContext* ctx, const ProtoList* fields, unsigned long i) {
    return reinterpret_cast<const ProtoString*>(fields->getAt(ctx, static_cast<int>(i)));
}

const ProtoObject* element(ProtoContext* ctx, const ProtoObject* self, const ProtoList* fields,
                           unsigned long i) {
    const ProtoObject* v = self->getOwnAttributeDirect(ctx, keyAt(ctx, fields, i));
    return v ? v : PROTO_NONE;
}

std::string prefixOf(ProtoContext* ctx, const ProtoObject* self) {
    const ProtoObject* p = self->getAttribute(ctx, layoutOf().prefixKey);
    return ProtoObject::isStringTagFast(p) ? asStr(p)->toStdString(ctx) : std::string();
}

// Point(1,2), (1,a), Unique: elements shown with Scala's toString, no blanks.
PRIM(product_toString) {
    expectArgs(ctx, args, "toString", 0);
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    const bool tuple = self->getAttribute(ctx, L.tupleKey) == PROTO_TRUE;
    std::string out = tuple ? "" : prefixOf(ctx, self);
    if (!fields) return str(ctx, out);  // a case object: its name
    out += '(';
    for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i) {
        if (i) out += ',';
        out += show(ctx, L, element(ctx, self, fields, i));
    }
    return str(ctx, out + ")");
}

// Structural equality: the same class and == elements (a case object: identity).
PRIM(product_equals) {
    const ProtoObject* other = arg(ctx, args, 0, "equals", 1);
    if (other == self) return PROTO_TRUE;
    if (other == PROTO_NONE || !isObjectCellFast(other) ||
        other->getPrototype(ctx) != self->getPrototype(ctx))
        return PROTO_FALSE;
    const ProtoList* fields = fieldsOf(ctx, self);
    if (!fields) return PROTO_FALSE;
    const RuntimeLayout& L = layoutOf();
    for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i)
        if (!valuesEqual(ctx, L, element(ctx, self, fields, i), element(ctx, other, fields, i)))
            return PROTO_FALSE;
    return PROTO_TRUE;
}

// Scala 3's synthesised hashCode (Hashing.h): verified against scalac.
PRIM(product_hashCode) {
    expectArgs(ctx, args, "hashCode", 0);
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    std::vector<std::int32_t> hs;
    if (fields)
        for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i)
            hs.push_back(scalaHash(ctx, L, element(ctx, self, fields, i)));
    return ctx->fromInteger(hashing::productHash(hashing::javaStringHash(prefixOf(ctx, self)), hs));
}

PRIM(product_productArity) {
    expectArgs(ctx, args, "productArity", 0);
    const ProtoList* fields = fieldsOf(ctx, self);
    return ctx->fromInteger(fields ? static_cast<long long>(fields->getSize(ctx)) : 0);
}

PRIM(product_productPrefix) {
    expectArgs(ctx, args, "productPrefix", 0);
    return str(ctx, prefixOf(ctx, self));
}

const ProtoObject* elementAt(ProtoContext* ctx, const ProtoObject* self, long long i) {
    const ProtoList* fields = fieldsOf(ctx, self);
    const long long n = fields ? static_cast<long long>(fields->getSize(ctx)) : 0;
    if (i < 0 || i >= n) throw ScalaError("IndexOutOfBoundsException", "Index out of range: " + std::to_string(i));
    return element(ctx, self, fields, static_cast<unsigned long>(i));
}

PRIM(product_productElement) {
    return elementAt(ctx, self, intArg(ctx, arg(ctx, args, 0, "productElement", 1), "productElement"));
}

// _1 .. _22 (Scala 3 case classes have them; a tuple's own fields shadow these).
#define PRODUCT_ELEMENT(k)                                                 \
    PRIM(product_##k) {                                                    \
        expectArgs(ctx, args, "_" #k, 0);                                  \
        return elementAt(ctx, self, (k) - 1);                              \
    }
PRODUCT_ELEMENT(1)  PRODUCT_ELEMENT(2)  PRODUCT_ELEMENT(3)  PRODUCT_ELEMENT(4)
PRODUCT_ELEMENT(5)  PRODUCT_ELEMENT(6)  PRODUCT_ELEMENT(7)  PRODUCT_ELEMENT(8)
PRODUCT_ELEMENT(9)  PRODUCT_ELEMENT(10) PRODUCT_ELEMENT(11) PRODUCT_ELEMENT(12)
PRODUCT_ELEMENT(13) PRODUCT_ELEMENT(14) PRODUCT_ELEMENT(15) PRODUCT_ELEMENT(16)
PRODUCT_ELEMENT(17) PRODUCT_ELEMENT(18) PRODUCT_ELEMENT(19) PRODUCT_ELEMENT(20)
PRODUCT_ELEMENT(21) PRODUCT_ELEMENT(22)
#undef PRODUCT_ELEMENT

// copy(positional..., name = value...): a new instance through the primary
// constructor (so its body runs, as in Scala); parameters not given keep
// this instance's values. Named arguments arrive in protoCore's keyword
// ProtoSparseList, keyed by the interned name (SEND_KW).
const ProtoObject* product_copy(ProtoContext* ctx, const ProtoObject* self, const proto::ParentLink*,
                                const ProtoList* args, const proto::ProtoSparseList* keywords) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    if (!fields)
        throw ScalaError("NoSuchMethodError", "value copy is not a member of " + typeName(ctx, L, self));
    const unsigned long n = fields->getSize(ctx);
    const unsigned long positional = argCount(ctx, args);
    if (positional > n) wrongArgCount("copy", "at most " + std::to_string(n), positional);
    ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(static_cast<unsigned>(n));
    unsigned long named = 0;
    for (unsigned long i = 0; i < n; ++i) {
        const auto key = reinterpret_cast<unsigned long>(keyAt(&scope, fields, i));
        const ProtoObject* v;
        if (i < positional) {
            v = args->getAt(&scope, static_cast<int>(i));
        } else if (keywords && keywords->has(&scope, key)) {
            v = keywords->getAt(&scope, key);
            ++named;
        } else {
            v = element(&scope, self, fields, i);
        }
        scope.setAutomaticLocal(static_cast<unsigned>(i), v);
    }
    if (keywords && named != keywords->getSize(&scope)) {
        // Report a keyword that is not a field; the keyword list's keys are
        // the interned names (ProtoSparseList::processElements, protoCore.h).
        struct Unknown {
            const ProtoList* fields;
            unsigned long n;
            std::string name;
        } unknown{fields, n, {}};
        keywords->processElements(&scope, &unknown,
            [](ProtoContext* c, void* self, unsigned long key, const ProtoObject*) {
                auto* u = static_cast<Unknown*>(self);
                if (!u->name.empty()) return;
                for (unsigned long i = 0; i < u->n; ++i)
                    if (reinterpret_cast<unsigned long>(keyAt(c, u->fields, i)) == key) return;
                u->name = reinterpret_cast<const ProtoString*>(key)->toStdString(c);
            });
        throw ScalaError("IllegalArgumentException", "copy has no parameter named " + unknown.name);
    }
    const ProtoObject* r = activeCallContext()->engine->construct(
        &scope, self->getPrototype(&scope), scope.getAutomaticLocals(), static_cast<unsigned>(n));
    scope.returnValue = r;
    return r;
}

// TupleN.<init>(this, a1..an): the fields _1.._n (tuples have no Scala body).
PRIM(tuple_init) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long n = argCount(ctx, args);
    const ProtoList* fields = fieldsOf(ctx, self);
    if (!fields || fields->getSize(ctx) != n)
        wrongArgCount("the tuple constructor", std::to_string(fields ? fields->getSize(ctx) : 0), n);
    const ProtoObject* t = self;
    for (unsigned long k = 0; k < n; ++k)
        t = t->setAttribute(ctx, L.tupleFieldKey[k + 1], args->getAt(ctx, static_cast<int>(k)));
    return t;
}

} // namespace

void installProductPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    auto put = [&](proto::ProtoObject* target, const char* name, proto::ProtoMethod fn) {
        target->setAttribute(ctx, ProtoString::createSymbol(ctx, name), ctx->fromMethod(nullptr, fn));
    };
    put(L.productProto, "toString", &product_toString);
    put(L.productProto, "equals", &product_equals);
    put(L.productProto, "hashCode", &product_hashCode);
    put(L.productProto, "productArity", &product_productArity);
    put(L.productProto, "productPrefix", &product_productPrefix);
    put(L.productProto, "productElement", &product_productElement);
    put(L.productProto, "copy", &product_copy);
    static constexpr proto::ProtoMethod elements[] = {
        &product_1, &product_2, &product_3, &product_4, &product_5, &product_6, &product_7,
        &product_8, &product_9, &product_10, &product_11, &product_12, &product_13, &product_14,
        &product_15, &product_16, &product_17, &product_18, &product_19, &product_20, &product_21,
        &product_22};
    for (unsigned k = 1; k <= kMaxTupleArity; ++k)
        put(L.productProto, ("_" + std::to_string(k)).c_str(), elements[k - 1]);
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) put(L.tupleProto[n], "<init>", &tuple_init);
}

} // namespace protoScala
```

Declare `void installProductPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& layout);` in `Primitives.h`; `installPrimitives` calls it last. Add `src/runtime/ProductPrimitives.cpp` to `protoscala_runtime`.

- [ ] **Step 4: Tuple literals**

In `Compiler.cpp` (Task 7 routed `NodeKind::Tuple` to it):

```cpp
void Compiler::compileTuple(const Tuple& t) {
    const std::size_t n = t.elems.size();
    if (n > kMaxTupleArity)
        throw CompileError("tuples of more than 22 elements are not supported (D32)", t.pos);
    for (const auto& e : t.elems) compileExpr(*e);
    emit(Op::MAKE_TUPLE, n, t.pos, 1 - static_cast<int>(n));
}
```

- [ ] **Step 5: A unit test for the tuple constructor path**

Append to `tests/unit/test_primitives.cpp`:

```cpp
TEST(Primitives, TuplesAreCaseClassesNeverProtoTuples) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(1, \"a\")"), "(1,a)");
    EXPECT_EQ(h.eval("new Tuple2(1, 2) == (1, 2)"), "true");
    EXPECT_EQ(h.eval("(1, 2).copy(_2 = 5)"), "(1,5)");
    EXPECT_EQ(h.eval("(1, 2).hashCode"), "1316541600");
}
```

- [ ] **Step 6: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release -R '08-case-classes|Primitives|Compiler' --output-on-failure` → PASS; full suite → 100%.

```bash
git add src/runtime src/compiler/Compiler.cpp CMakeLists.txt tests/unit tests/conformance/08-case-classes
git commit -m "case classes, case objects and tuples with Scala's synthesised members

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 9: The universal `apply` rule, `update`, setters and method values

**Files:**
- Create: `tests/conformance/09-apply/*.scala`
- Modify: only if a fixture fails (the mechanisms exist: `invoke` → `send(apply)` since Phase 1, `callMember` and `bindMethod` from Task 6, the setter and `update` rewrites from Task 3)

**Interfaces:**
- Consumes: Tasks 3, 6, 7.
- Produces: fixtures pinning DESIGN §5.1 (`expr(args)` on any value is `expr.apply(args)`), §3.4 (`a(i) = v` → `a.update(i, v)`), setters (`obj.x = v`, `obj.x op= v`), function-valued fields (`obj.f(x)` → `obj.f.apply(x)`) and method values (`obj.m` → a function, D10).

- [ ] **Step 1: Fixtures (they should pass; any failure is a defect to fix before continuing)**

`tests/conformance/09-apply/apply-on-objects.scala`
```scala
// EXPECT: 9 16 5
object Square:
  def apply(n: Int) = n * n
class Poly(val a: Int, val b: Int):
  def apply(x: Int) = a * x + b

@main def run(): Unit =
  val p = new Poly(2, 3)
  val f = Square
  println(Square(3).toString + " " + f(4) + " " + p(1))
```

`update-method.scala`
```scala
// EXPECT: 0 7 0
class Pair:
  var first = 0
  var second = 0
  def apply(i: Int): Int = if i == 0 then first else second
  def update(i: Int, v: Int): Unit = if i == 0 then first = v else second = v

@main def run(): Unit =
  val p = new Pair
  p(1) = 7
  println(p(0).toString + " " + p(1) + " " + p.first)
```

`method-values.scala` (D10: `s.scale` is a function)
```scala
// EXPECT: 10 12 true
class Scaler(k: Int):
  def scale(x: Int) = x * k

@main def run(): Unit =
  val s = new Scaler(2)
  val f = s.scale
  val g: Int => Int = s.scale
  println(f(5).toString + " " + g(6) + " " + (f(1) == s.scale(1)))
```

`function-valued-fields.scala`
```scala
// EXPECT: 6 7 <function1>
class Ops(val inc: Int => Int):
  val double = (x: Int) => x * 2

@main def run(): Unit =
  val o = new Ops(x => x + 1)
  println(o.double(3).toString + " " + o.inc(6) + " " + o.double)
```

`setters-from-outside.scala`
```scala
// EXPECT: 5 8 10
class Cell(var value: Int)

@main def run(): Unit =
  val c = new Cell(5)
  val before = c.value
  c.value += 3
  val mid = c.value
  c.value = 10
  println(before.toString + " " + mid + " " + c.value)
```

Run: `ctest --test-dir build_release -R 09-apply --output-on-failure`
Expected: PASS. A failure is a defect in Tasks 3, 6 or 7: fix it there (with a unit test that reproduces it) before committing.

- [ ] **Step 2: Commit**

```bash
git add tests/conformance/09-apply
git commit -m "tests: universal apply, update, setters and method values

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 10: `List` for Phase 2 — construction, `::`, `map`/`flatMap`/`filter`/lazy `withFilter`, placeholders

**Files:**
- Create: `tests/conformance/10-lists/*.scala`
- Modify: `src/runtime/Primitives.cpp`, `src/runtime/Primitives.h`, `tests/unit/test_primitives.cpp`

**Interfaces:**
- Consumes: Task 5 (`listCompanion`, `withFilterProto`, `listKey`, `predsKey`), Task 2 (placeholders).
- Produces (Open question Q8 — the minimum a for-comprehension over `List` and the `::` / `List(...)` patterns need; the rest of `List` is Phase 3):
  - Globals `List` (the companion: `apply(xs*)`, `empty`) and `Nil` (the empty list); `builtinGlobalNames()` also lists `__raise` (Task 11).
  - `List` methods `::`, `map`, `flatMap` (the function may return a `List` or an `Option`-like value answering `isEmpty`/`get`), `filter`, `withFilter` (a lazy `WithFilter` with `map`/`flatMap`/`foreach`/`withFilter`, so predicates and bodies interleave as in Scala), `tail`, `drop`.
  - A `ListBuilder` helper in `Primitives.cpp` (Design note 6).

- [ ] **Step 1: Failing tests**

Append to `tests/unit/test_primitives.cpp`:

```cpp
TEST(Primitives, Phase2Lists) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3)"), "List(1, 2, 3)");
    EXPECT_EQ(h.eval("List()"), "List()");
    EXPECT_EQ(h.eval("List.empty"), "List()");
    EXPECT_EQ(h.eval("1 :: 2 :: Nil"), "List(1, 2)");
    EXPECT_EQ(h.eval("List(1, 2, 3).map(x => x * 10)"), "List(10, 20, 30)");
    EXPECT_EQ(h.eval("List(1, 2, 3, 4).filter(_ % 2 == 0)"), "List(2, 4)");
    EXPECT_EQ(h.eval("List(1, 2).flatMap(x => List(x, x))"), "List(1, 1, 2, 2)");
    EXPECT_EQ(h.eval("List(1, 2, 3).tail"), "List(2, 3)");
    EXPECT_EQ(h.eval("List(1, 2, 3).drop(2)"), "List(3)");
    EXPECT_EQ(h.eval("List(1, 2, 3).drop(5)"), "List()");
    EXPECT_EQ(h.eval("Nil.tail"), "error: UnsupportedOperationException: tail of empty list");
    EXPECT_EQ(h.eval("List() == Nil"), "true");
    EXPECT_EQ(h.eval("List(1, 2, 3).withFilter(_ > 1).map(_ * 2)"), "List(4, 6)");
    EXPECT_EQ(h.eval("List(1, 2).map(_ > 1).filter(x => x)"), "List(true)");
    EXPECT_EQ(h.eval("List(1).filter(x => 1)"),
              "error: ClassCastException: filter expects a function returning Boolean, got Int");
}
```

`tests/conformance/10-lists/`:

`list-basics.scala`
```scala
// EXPECT: List(1, 2, 3) List(0, 1, 2, 3) 3 List(2, 4, 6) List(1, 3) List(2, 3) true
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val ys = 0 :: xs
  println(xs.toString + " " + ys + " " + xs.length + " " + xs.map(_ * 2) + " " + xs.filter(_ != 2) + " " +
    xs.tail + " " + (xs == List(1, 2, 3)))
```

`list-of-case-classes.scala`
```scala
// EXPECT: List(P(1), P(2)) true List((1,a), (2,b))
case class P(n: Int)
@main def run(): Unit =
  val ps = List(P(1), P(2))
  println(ps.toString + " " + (ps == List(P(1), P(2))) + " " + List((1, "a"), (2, "b")))
```

`with-filter-is-lazy.scala` (the interleaving `scalac` shows, Design note 11)
```scala
// EXPECT: f1 x1 f2 x2 f3 x3
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
@main def run(): Unit =
  List(1, 2, 3).withFilter(x => { note("f" + x); true }).foreach(x => note("x" + x))
  println(log)
```

`placeholders.scala`
```scala
// EXPECT: List(2, 3, 4) List(3) List(1, 2, 3) 6
@main def run(): Unit =
  val xs = List(1, 2, 3)
  var sum = 0
  xs.foreach(sum += _)
  println(xs.map(_ + 1).toString + " " + xs.filter(_ > 2) + " " + xs.map(_.toString) + " " + sum)
```

Run: `cmake --build build_release && ctest --test-dir build_release -R '10-lists|Primitives' --output-on-failure` → FAIL (`Not found: List`).

- [ ] **Step 2: Implement**

In `src/runtime/Primitives.cpp` (after the Phase 1 `List` section):

```cpp
// Builds a List element by element (Design note 6): the list so far is slot 0
// of the builder's own context, the element being appended slot 1. While the
// builder is open its context is the innermost one: every call made during
// the build takes context() (or a child of it) as its parent.
class ListBuilder {
public:
    explicit ListBuilder(ProtoContext* parent) : scope_(parent->space, parent) {
        scope_.resizeAutomaticLocals(2);
        scope_.setAutomaticLocal(0, scope_.newList()->asObject(&scope_));
    }
    ProtoContext* context() { return &scope_; }
    void add(const ProtoObject* v) {
        scope_.setAutomaticLocal(1, v);
        const ProtoList* l = scope_.getAutomaticLocal(0)->asList(&scope_);
        scope_.setAutomaticLocal(0, l->appendLast(&scope_, v)->asObject(&scope_));
    }
    void addAll(const ProtoList* other) {
        const ProtoList* l = scope_.getAutomaticLocal(0)->asList(&scope_);
        scope_.setAutomaticLocal(0, l->extend(&scope_, other)->asObject(&scope_));
    }
    // The finished list; the builder's context hands it to its parent on exit.
    const ProtoObject* finish() {
        const ProtoObject* r = scope_.getAutomaticLocal(0);
        scope_.returnValue = r;
        return r;
    }

private:
    ProtoContext scope_;
};

// f(x) in a short-lived context (its garbage is released when it ends); the
// result is handed to `parent`.
const ProtoObject* callOne(ProtoContext* parent, const ProtoObject* f, const ProtoObject* x) {
    ProtoContext step(parent->space, parent);
    step.resizeAutomaticLocals(1);
    step.setAutomaticLocal(0, x);
    const ProtoObject* r = activeCallContext()->engine->invoke(&step, f, step.getAutomaticLocals(), 1);
    step.returnValue = r;
    return r;
}

bool truth(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw ScalaError("ClassCastException", std::string(method) +
                                                " expects a function returning Boolean, got " +
                                                typeName(ctx, layoutOf(), v));
}

// Appends what `f` returned for flatMap: a List's elements, or an Option-like
// value's content (anything answering isEmpty/get: Scala's IterableOnce).
void addFlat(ListBuilder& b, const ProtoObject* r, const char* method) {
    ProtoContext* ctx = b.context();
    if (isListFast(r)) {
        b.addAll(r->asList(ctx));
        return;
    }
    const RuntimeLayout& L = layoutOf();
    if (!isScalaInstance(ctx, L, r))
        throw ScalaError("ClassCastException", std::string(method) +
                                                    " expects a function returning a List or an Option, got " +
                                                    typeName(ctx, L, r));
    ExecutionEngine* engine = activeCallContext()->engine;
    const ProtoObject* v;
    {
        // The calls run in a child context that is closed before the builder
        // allocates again (the innermost context allocates, Design note 6).
        ProtoContext probe(ctx->space, ctx);
        const auto* isEmpty = proto::ProtoString::createSymbol(&probe, "isEmpty");
        if (engine->send(&probe, r, isEmpty, nullptr, 0) == PROTO_TRUE) return;
        v = engine->send(&probe, r, proto::ProtoString::createSymbol(&probe, "get"), nullptr, 0);
        probe.returnValue = v;  // re-rooted in the builder's context when `probe` ends
    }
    b.add(v);
}

PRIM(list_cons) {  // x :: xs is xs.::(x): an O(log n) prepend (DESIGN §6)
    return self->asList(ctx)->appendFirst(ctx, arg(ctx, args, 0, "::", 1))->asObject(ctx);
}

PRIM(list_tail) {
    expectArgs(ctx, args, "tail", 0);
    const ProtoList* list = self->asList(ctx);
    if (list->getSize(ctx) == 0) throw ScalaError("UnsupportedOperationException", "tail of empty list");
    return list->removeFirst(ctx)->asObject(ctx);
}

PRIM(list_drop) {
    const long long n = intArg(ctx, arg(ctx, args, 0, "drop", 1), "drop");
    const ProtoList* list = self->asList(ctx);
    const long long size = static_cast<long long>(list->getSize(ctx));
    if (n <= 0) return self;
    if (n >= size) return ctx->newList()->asObject(ctx);
    return list->getSlice(ctx, static_cast<int>(n), static_cast<int>(size))->asObject(ctx);
}

PRIM(list_map) {
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i)
        b.add(callOne(b.context(), f, list->getAt(b.context(), static_cast<int>(i))));
    return b.finish();
}

PRIM(list_flatMap) {
    const ProtoObject* f = arg(ctx, args, 0, "flatMap", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i)
        addFlat(b, callOne(b.context(), f, list->getAt(b.context(), static_cast<int>(i))), "flatMap");
    return b.finish();
}

PRIM(list_filter) {
    const ProtoObject* p = arg(ctx, args, 0, "filter", 1);
    const ProtoList* list = self->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = list->getAt(b.context(), static_cast<int>(i));
        if (truth(b.context(), callOne(b.context(), p, x), "filter")) b.add(x);
    }
    return b.finish();
}

// xs.withFilter(p): Scala's lazy WithFilter. Its map/flatMap/foreach test the
// predicates on an element right before using it, so for-comprehension
// guards and bodies interleave exactly as in Scala (Design note 11).
const ProtoObject* makeWithFilter(ProtoContext* ctx, const ProtoObject* list, const ProtoList* preds) {
    const RuntimeLayout& L = layoutOf();
    return L.withFilterProto->newChild(ctx)
        ->setAttribute(ctx, L.listKey, list)
        ->setAttribute(ctx, L.predsKey, preds->asObject(ctx));
}

PRIM(list_withFilter) {
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    const ProtoObject* preds[1] = {p};
    return makeWithFilter(ctx, self, ctx->newList(1, preds));
}

PRIM(withFilter_withFilter) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    const ProtoList* preds = self->getOwnAttributeDirect(ctx, L.predsKey)->asList(ctx);
    return makeWithFilter(ctx, self->getOwnAttributeDirect(ctx, L.listKey), preds->appendLast(ctx, p));
}

bool accepted(ProtoContext* ctx, const ProtoList* preds, const ProtoObject* x) {
    for (unsigned long k = 0, n = preds->getSize(ctx); k < n; ++k)
        if (!truth(ctx, callOne(ctx, preds->getAt(ctx, static_cast<int>(k)), x), "withFilter")) return false;
    return true;
}

enum class WithFilterOp { Map, FlatMap, Foreach };

const ProtoObject* withFilterApply(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                                   WithFilterOp op, const char* method) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, method, 1);
    const ProtoList* list = self->getOwnAttributeDirect(ctx, L.listKey)->asList(ctx);
    const ProtoList* preds = self->getOwnAttributeDirect(ctx, L.predsKey)->asList(ctx);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = list->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = list->getAt(b.context(), static_cast<int>(i));
        if (!accepted(b.context(), preds, x)) continue;
        const ProtoObject* r = callOne(b.context(), f, x);
        if (op == WithFilterOp::Map) b.add(r);
        else if (op == WithFilterOp::FlatMap) addFlat(b, r, method);
    }
    if (op == WithFilterOp::Foreach) return L.unit;
    return b.finish();
}

PRIM(withFilter_map)     { return withFilterApply(ctx, self, args, WithFilterOp::Map, "map"); }
PRIM(withFilter_flatMap) { return withFilterApply(ctx, self, args, WithFilterOp::FlatMap, "flatMap"); }
PRIM(withFilter_foreach) { return withFilterApply(ctx, self, args, WithFilterOp::Foreach, "foreach"); }

// The `List` companion: List(xs*) is the argument list itself; List.empty.
PRIM(listCompanion_apply) { return args ? args->asObject(ctx) : ctx->newList()->asObject(ctx); }
PRIM(listCompanion_empty) { expectArgs(ctx, args, "empty", 0); return ctx->newList()->asObject(ctx); }
```

`callNative` hands a native method the argument `ProtoList` it built in its own scope (`ExecutionEngine.cpp:64-69`), so returning `args` from `List.apply` is safe: the scope's `returnValue` re-roots it.

Installation (`installPrimitives`): add `{"::", &list_cons}, {"tail", &list_tail}, {"drop", &list_drop}, {"map", &list_map}, {"flatMap", &list_flatMap}, {"filter", &list_filter}, {"withFilter", &list_withFilter}` to the `lists` table; install `{"map", &withFilter_map}, {"flatMap", &withFilter_flatMap}, {"foreach", &withFilter_foreach}, {"withFilter", &withFilter_withFilter}` on `L.withFilterProto` and `{"apply", &listCompanion_apply}, {"empty", &listCompanion_empty}` on `L.listCompanion`; bind the globals:

```cpp
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "List"), L.listCompanion);
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Nil"), ctx->newList()->asObject(ctx));
```

and make `builtinGlobalNames()` return `{"println", "print", "List", "Nil", "__raise"}` (`__raise` is installed by Task 11; declaring its name now is harmless because nothing references it until then).

- [ ] **Step 3: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release -R '10-lists|Primitives' --output-on-failure` → PASS; full suite → 100%; `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release -R 10-lists` → PASS.

```bash
git add src/runtime tests/unit/test_primitives.cpp tests/conformance/10-lists
git commit -m "List for Phase 2: List(...), Nil, ::, map, flatMap, filter, lazy withFilter

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 11: The prelude — `Option`, `Some`, `None`

**Files:**
- Create: `lib/prelude.scala`, `src/support/PreludeSource.cpp.in`, `src/runtime/Prelude.h`, `src/runtime/Prelude.cpp`, `tests/conformance/08-case-classes/option-*.scala`
- Modify: `CMakeLists.txt`, `src/runtime/Primitives.cpp` (`__raise`), `src/repl/Session.cpp`, `tests/unit/EvalHarness.h`, `tests/unit/test_engine.cpp`

**Interfaces:**
- Consumes: Tasks 7, 8, 10.
- Produces (Open question Q7): `lib/prelude.scala` compiled at session start into every `Session` and `EvalHarness`; `const char* preludeSource();` (generated) and `void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals, std::vector<std::unique_ptr<BytecodeModule>>& modules);`; the internal global `__raise(className, message)`.

- [ ] **Step 1: Failing fixtures**

`tests/conformance/08-case-classes/option-basics.scala`
```scala
// EXPECT: Some(3) None true 3 0 Some(6) None Some(2) List(3) true
@main def run(): Unit =
  val s = Some(3)
  val n: Option[Int] = None
  println(s.toString + " " + n + " " + s.isDefined + " " + s.getOrElse(0) + " " + n.getOrElse(0) + " " +
    s.map(_ * 2) + " " + s.filter(_ > 5) + " " + s.flatMap(x => Some(x - 1)) + " " + s.toList + " " +
    (Some(1) == Some(1)))
```

`tests/conformance/08-case-classes/option-none-get.scala`
```scala
// EXPECT-ERROR: NoSuchElementException: None.get
@main def run(): Unit = println(None.get)
```

`tests/conformance/10-lists/flatmap-option.scala`
```scala
// EXPECT: List(1, 3)
@main def run(): Unit =
  println(List(1, 2, 3).flatMap(x => if x % 2 == 1 then Some(x) else None))
```

Append to `tests/unit/test_engine.cpp`:

```cpp
TEST(Prelude, OptionIsDefinedInEverySession) {
    EvalHarness h;
    EXPECT_EQ(h.eval("Some(1)"), "Some(1)");
    EXPECT_EQ(h.eval("None"), "None");
    EXPECT_EQ(h.eval("Some(2).map(_ + 1).getOrElse(0)"), "3");
    EXPECT_EQ(h.eval("None.get"), "error: NoSuchElementException: None.get");
}
```

Run → FAIL (`Not found: Some`).

- [ ] **Step 2: The prelude source**

`lib/prelude.scala`:

```scala
// The protoScala prelude: definitions every program sees, compiled when a
// session starts (DESIGN §6: the prelude is written in protoScala). Phase 2
// defines Option; Phase 3 extends it (Either, Try, more collection methods).
// `__raise` is an internal primitive that stands in for `throw` until
// exceptions exist.

sealed abstract class Option[+A]:
  def isEmpty: Boolean
  def get: A
  def isDefined: Boolean = !isEmpty
  def nonEmpty: Boolean = !isEmpty
  // The default is evaluated eagerly until by-name parameters exist (D33).
  def getOrElse[B >: A](default: B): B = if isEmpty then default else get
  def orElse[B >: A](alternative: Option[B]): Option[B] = if isEmpty then alternative else this
  def map[B](f: A => B): Option[B] = if isEmpty then None else Some(f(get))
  def flatMap[B](f: A => Option[B]): Option[B] = if isEmpty then None else f(get)
  def filter(p: A => Boolean): Option[A] = if isEmpty || p(get) then this else None
  def withFilter(p: A => Boolean): Option[A] = filter(p)
  def foreach[U](f: A => U): Unit = if !isEmpty then f(get)
  def contains[B >: A](elem: B): Boolean = !isEmpty && get == elem
  def exists(p: A => Boolean): Boolean = !isEmpty && p(get)
  def toList: List[A] = if isEmpty then Nil else get :: Nil

final case class Some[+A](value: A) extends Option[A]:
  def isEmpty: Boolean = false
  def get: A = value

case object None extends Option[Nothing]:
  def isEmpty: Boolean = true
  def get: Nothing = __raise("NoSuchElementException", "None.get")
```

(`Option.withFilter` is strict: an `Option` holds at most one element, so the result is the same as Scala's lazy one.)

- [ ] **Step 3: Embedding**

`src/support/PreludeSource.cpp.in`:

```cpp
// Generated by CMake from lib/prelude.scala (configure_file); do not edit.
namespace protoScala {

const char* preludeSource() {
    return R"__protoscala_prelude__(@PROTOSCALA_PRELUDE_SOURCE@)__protoscala_prelude__";
}

} // namespace protoScala
```

`CMakeLists.txt`, after the version header:

```cmake
# --- Prelude (lib/prelude.scala, embedded in the binary) -------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala" PROTOSCALA_PRELUDE_SOURCE)
string(FIND "${PROTOSCALA_PRELUDE_SOURCE}" ")__protoscala_prelude__" PROTOSCALA_PRELUDE_DELIMITER)
if(NOT PROTOSCALA_PRELUDE_DELIMITER EQUAL -1)
    message(FATAL_ERROR "lib/prelude.scala must not contain the raw-string delimiter")
endif()
configure_file(src/support/PreludeSource.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/PreludeSource.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/prelude.scala")
```

and add `src/runtime/Prelude.cpp` and `${CMAKE_CURRENT_BINARY_DIR}/generated/PreludeSource.cpp` to `protoscala_runtime`. (`configure_file` substitutes `@PROTOSCALA_PRELUDE_SOURCE@` once and does not rescan the inserted text; the prelude contains no `;`, so CMake's list semantics do not alter it.)

`src/runtime/Prelude.h`:

```cpp
/*
 * Prelude — lib/prelude.scala, embedded in the binary (PreludeSource.cpp is
 * generated by CMake) and compiled into every session before user code.
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/GlobalTable.h"

#include <memory>
#include <vector>

namespace proto { class ProtoContext; }

namespace protoScala {

class ExecutionEngine;

const char* preludeSource();

// Parses, compiles and runs the prelude under `parent`: its definitions join
// `globals`, its modules `modules` (retained for the session). Throws
// std::logic_error when the prelude fails — a build defect, never user input.
void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals,
                 std::vector<std::unique_ptr<BytecodeModule>>& modules);

} // namespace protoScala
```

`src/runtime/Prelude.cpp`:

```cpp
#include "runtime/Prelude.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"

#include <stdexcept>
#include <string>

namespace protoScala {

void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals,
                 std::vector<std::unique_ptr<BytecodeModule>>& modules) {
    auto where = [](SourcePos p) {
        return " (lib/prelude.scala:" + std::to_string(p.line) + ":" + std::to_string(p.column) + ")";
    };
    try {
        auto unit = parseSource(preludeSource());
        desugar(*unit);
        Compiler compiler(globals);
        CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        cu.module->linkSymbols(parent);
        const BytecodeModule& mod = *cu.module;
        modules.push_back(std::move(cu.module));
        engine.run(parent, mod);
    } catch (const ParseError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const CompileError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const ScalaError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what());
    }
}

} // namespace protoScala
```

`Session::Session` ends with

```cpp
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    loadPrelude(&ctx, engine_, globals_, modules_);
```

and the `EvalHarness` constructor does the same with its members.

- [ ] **Step 4: `__raise`**

```cpp
// __raise(className, message): raises a Scala error. The prelude's stand-in
// for `throw` until exceptions exist; not part of the language.
PRIM(prim_raise) {
    const std::string cls = stringArg(ctx, arg(ctx, args, 0, "__raise", 2), "__raise");
    const std::string msg = stringArg(ctx, args->getAt(ctx, 1), "__raise");
    throw ScalaError(cls, msg);
}
```

installed in the `globals` table as `{"__raise", &prim_raise}`.

- [ ] **Step 5: Run, measure cold start, commit**

Run: `cmake --build build_release && ctest --test-dir build_release` → 100%.

Run: `cmake -B build_bench -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build_bench && benchmarks/cold-start.sh build_bench/protoscala 21` three times.
Expected: both medians below 20 ms. The prelude adds a parse, a compile and a run of about 40 lines at every start. **If a median is at or above 20 ms, stop and report to the maintainer** with the three measurements and `perf stat -r 3 build_bench/protoscala examples/hello.scala` before and after this task; the remedy (a lazily compiled or a precompiled prelude) is a design decision (Open question Q7). Record the numbers in the task report (Task 17 copies them into `benchmarks/RESULTS.md`).

```bash
git add lib src/support/PreludeSource.cpp.in src/runtime src/repl/Session.cpp CMakeLists.txt tests/unit tests/conformance
git commit -m "prelude: Option, Some and None written in protoScala, embedded in the binary

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 12: Pattern matching, `isInstanceOf` and `asInstanceOf`

**Files:**
- Create: `src/compiler/CompilePatterns.cpp`, `tests/conformance/11-pattern-matching/*.scala`
- Modify: `src/compiler/Compiler.h`, `src/compiler/Compiler.cpp`, `src/frontend/AST.h`, `src/frontend/AST.cpp`, `src/frontend/Desugar.cpp`, `CMakeLists.txt`, `tests/unit/test_compiler.cpp`, `tests/unit/test_engine.cpp`

**Interfaces:**
- Consumes: Tasks 2–11.
- Produces:
  - `void patternVariables(const Pattern& p, std::vector<std::string>& out);` moved from `Desugarer` to `AST.cpp` (declared in `AST.h`), used by Desugar and the compiler.
  - `Compiler::compileMatch`, `compilePattern`, `compileExtractor`, `compileListPattern`, `extractInto`, `emitProtoTest`, `bindPattern`, `checkNoVariables`, `compileTypeTest`, `compileInstanceOf` (declared in `Compiler.h`, defined in `CompilePatterns.cpp`).
  - The DESIGN §5.3 cascade: type/class tests (`TEST_TYPE`, `TEST_PROTO`), extraction (`UNAPPLY_FIELDS` for case classes and tuples, `UNCONS` for `::`, indices and `drop` for `List(...)`, `unapply` for custom extractors: a `Boolean` for no sub-patterns, otherwise a value answering `isEmpty`/`get` whose `get` is the single result or a tuple — Scala 3's protocol), binding to local slots, guards after binding, first match wins, `MatchError: <value> (of class <T>)` otherwise.
  - `x.isInstanceOf[T]`, `x.asInstanceOf[T]` (D29).

- [ ] **Step 1: Failing fixtures and tests**

`tests/conformance/11-pattern-matching/` (the expected lines of `literals-*`, `seq-patterns` and `pattern-lambdas` are `scalac`'s; note `99` matching `'c'`: Scala's `==` is cooperative, `'c' == 99`):

`literals-indent.scala`
```scala
// EXPECT: one few greeting char yes null unit two and a half char
def describe(x: Any): String = x match
  case 1 => "one"
  case 2 | 3 => "few"
  case "hi" => "greeting"
  case 'c' => "char"
  case true => "yes"
  case null => "null"
  case () => "unit"
  case 2.5 => "two and a half"
  case _ => "other"

@main def run(): Unit =
  println(List(1, 3, "hi", 'c', true, null, (), 2.5, 99).map(describe).mkString(" "))
```

`literals-braces.scala`
```scala
// EXPECT: one few greeting char yes null unit two and a half char
def describe(x: Any): String = x match {
  case 1 => "one"
  case 2 | 3 => "few"
  case "hi" => "greeting"
  case 'c' => "char"
  case true => "yes"
  case null => "null"
  case () => "unit"
  case 2.5 => "two and a half"
  case _ => "other"
}

@main def run(): Unit = {
  println(List(1, 3, "hi", 'c', true, null, (), 2.5, 99).map(describe).mkString(" "))
}
```

`variables-and-guards.scala`
```scala
// EXPECT: negative zero small big:1000
def classify(n: Int): String = n match
  case 0 => "zero"
  case x if x < 0 => "negative"
  case x if x < 10 => "small"
  case big => "big:" + big

@main def run(): Unit = println(List(-5, 0, 7, 1000).map(classify).mkString(" "))
```

`typed-patterns.scala` (`5L` matches `Int`: D29)
```scala
// EXPECT: int:3 long-is-int:5 double:1.5 string:3 bool:true char:z list:2 point:1 unit none
case class Point(x: Int, y: Int)
def kind(v: Any): String = v match
  case i: Int if i == 5 => "long-is-int:" + i
  case i: Int => "int:" + i
  case d: Double => "double:" + d
  case s: String => "string:" + s.length
  case b: Boolean => "bool:" + b
  case c: Char => "char:" + c
  case l: List[?] => "list:" + l.length
  case p: Point => "point:" + p.x
  case u: Unit => "unit"
  case _ => "none"

@main def run(): Unit =
  println(List(3, 5L, 1.5, "abc", true, 'z', List(1, 2), Point(1, 2), (), null).map(kind).mkString(" "))
```

`case-class-extraction.scala`
```scala
// EXPECT: 7 origin on-x (1,2)
case class Point(x: Int, y: Int)
case class Line(a: Point, b: Point)
def describe(v: Any): String = v match
  case Line(Point(x1, _), Point(x2, _)) => (x1 + x2).toString
  case Point(0, 0) => "origin"
  case Point(_, 0) => "on-x"
  case Point(x, y) => "(" + x + "," + y + ")"

@main def run(): Unit =
  println(List(Line(Point(3, 0), Point(4, 9)), Point(0, 0), Point(5, 0), Point(1, 2)).map(describe).mkString(" "))
```

`tuple-patterns.scala`
```scala
// EXPECT: sum=3 first=a swapped=(2,1) 3-tuple
def f(v: Any): String = v match
  case (a: Int, b: Int) => "sum=" + (a + b)
  case (s: String, _) => "first=" + s
  case (x, y, z) => "3-tuple"
  case _ => "?"

@main def run(): Unit =
  val (a, b) = (1, 2)
  println(f((1, 2)) + " " + f(("a", 1)) + " swapped=" + (b, a) + " " + f((1, 2, 3)))
```

`cons-patterns.scala`
```scala
// EXPECT: 6 empty one:9 first=1,second=2,rest=List(3)
def sum(xs: List[Int]): Int = xs match
  case Nil => 0
  case h :: t => h + sum(t)
def shape(xs: List[Int]): String = xs match
  case Nil => "empty"
  case x :: Nil => "one:" + x
  case a :: b :: rest => "first=" + a + ",second=" + b + ",rest=" + rest

@main def run(): Unit =
  println(sum(List(1, 2, 3)).toString + " " + shape(Nil) + " " + shape(List(9)) + " " + shape(List(1, 2, 3)))
```

`seq-patterns.scala`
```scala
// EXPECT: empty pair:1,2 head:1 rest:List(2, 3) other
def f(xs: List[Int]): String = xs match
  case List() => "empty"
  case List(a, b) => "pair:" + a + "," + b
  case List(1, rest*) if rest.length > 2 => "long"
  case List(h, _*) if h == 1 && xs.length == 1 => "head:" + h
  case List(_, rest @ _*) if rest.length == 2 => "rest:" + rest
  case _ => "other"

@main def run(): Unit =
  println(List(List(), List(1, 2), List(1), List(1, 2, 3), List(5, 6, 7, 8)).map(f).mkString(" "))
```

`alternatives-and-binders.scala`
```scala
// EXPECT: weekend weekday Point(1,1)-diag x=3
case class Point(x: Int, y: Int)
def day(d: String) = d match
  case "sat" | "sun" => "weekend"
  case _ => "weekday"
def pt(p: Point) = p match
  case q @ Point(a, b) if a == b => q.toString + "-diag"
  case Point(a, _) => "x=" + a

@main def run(): Unit = println(day("sun") + " " + day("mon") + " " + pt(Point(1, 1)) + " " + pt(Point(3, 4)))
```

`stable-identifiers.scala`
```scala
// EXPECT: max other exact
val Max = 10
@main def run(): Unit =
  val target = 7
  def check(n: Int) = n match
    case Max => "max"
    case `target` => "exact"
    case _ => "other"
  println(check(10) + " " + check(3) + " " + check(7))
```

`custom-extractors.scala`
```scala
// EXPECT: even:5 odd empty nonempty:3 a->b none
object Even:
  def unapply(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None
object NonEmpty:
  def unapply(s: String): Boolean = s.nonEmpty
object Split:
  def unapply(s: String): Option[(String, String)] =
    val i = s.indexOf("=")
    if i < 0 then None else Some((s.substring(0, i), s.substring(i + 1)))
def num(n: Int): String = n match
  case Even(half) => "even:" + half
  case _ => "odd"
def str(s: String): String = s match
  case NonEmpty() => "nonempty:" + s.length
  case _ => "empty"
def kv(s: String): String = s match
  case Split(k, v) => k + "->" + v
  case _ => "none"

@main def run(): Unit =
  println(num(10) + " " + num(3) + " " + str("") + " " + str("abc") + " " + kv("a=b") + " " + kv("ab"))
```

`match-error.scala`
```scala
// EXPECT-ERROR: MatchError: 5 (of class Int)
@main def run(): Unit =
  val r = 5 match
    case 1 => "one"
  println(r)
```

`match-error-case-class.scala`
```scala
// EXPECT-ERROR: MatchError: Point(1,2) (of class Point)
case class Point(x: Int, y: Int)
@main def run(): Unit =
  Point(1, 2) match
    case Point(0, _) => println("zero")
```

`alternative-binder.scala`
```scala
// EXPECT-ERROR: Illegal variable x in pattern alternative
def f(v: Any) = v match
  case x: Int | x: String => 1
  case _ => 0
```

`pattern-lambdas.scala`
```scala
// EXPECT: List(a, bb) List(3, 7)
@main def run(): Unit =
  val pairs = List((1, "a"), (2, "b"))
  println(pairs.map { case (n, s) => s * n }.toString + " " + List((1, 2), (3, 4)).map { case (a, b) => a + b })
```

`val-patterns.scala`
```scala
// EXPECT: 7 3 4 1 List(2, 3)
case class Point(x: Int, y: Int)
val (a, b) = (3, 4)
@main def run(): Unit =
  val Point(x, y) = Point(3, 4)
  val h :: t = List(1, 2, 3)
  println((a + b).toString + " " + x + " " + y + " " + h + " " + t)
```

`is-instance-of.scala` (`3L.isInstanceOf[Int]` is `true` here, `false` on the JVM: D29)
```scala
// EXPECT: true false true true false true true 3
trait Animal
class Dog extends Animal
class Cat extends Animal
@main def run(): Unit =
  val d: Any = new Dog
  println(d.isInstanceOf[Animal].toString + " " + d.isInstanceOf[Cat] + " " + d.isInstanceOf[Dog] + " " +
    "s".isInstanceOf[String] + " " + null.isInstanceOf[String] + " " + (1, 2).isInstanceOf[Product] + " " +
    3L.isInstanceOf[Int] + " " + (if d.asInstanceOf[Animal] eq d then 3 else 0))
```

`as-instance-of-fail.scala`
```scala
// EXPECT-ERROR: ClassCastException: String cannot be cast to Int
@main def run(): Unit =
  val v: Any = "s"
  println(v.asInstanceOf[Int] + 1)
```

`sealed-adt.scala`
```scala
// EXPECT: 14 (2 + (3 * 4))
sealed trait Expr
case class Num(n: Int) extends Expr
case class Add(l: Expr, r: Expr) extends Expr
case class Mul(l: Expr, r: Expr) extends Expr
def eval(e: Expr): Int = e match
  case Num(n) => n
  case Add(l, r) => eval(l) + eval(r)
  case Mul(l, r) => eval(l) * eval(r)
def show(e: Expr): String = e match
  case Num(n) => n.toString
  case Add(l, r) => "(" + show(l) + " + " + show(r) + ")"
  case Mul(l, r) => "(" + show(l) + " * " + show(r) + ")"

@main def run(): Unit =
  val e = Add(Num(2), Mul(Num(3), Num(4)))
  println(eval(e).toString + " " + show(e))
```

`deep-hierarchy-membership.scala` (twelve ancestors: the marker test has no depth limit short of R3)
```scala
// EXPECT: true true true
trait T0
trait T1 extends T0
trait T2 extends T1
trait T3 extends T2
trait T4 extends T3
trait T5 extends T4
class C0 extends T5
class C1 extends C0
class C2 extends C1
class C3 extends C2
class C4 extends C3
class C5 extends C4
@main def run(): Unit =
  val v: Any = new C5
  println(v.isInstanceOf[T0].toString + " " + v.isInstanceOf[C0] + " " + (v match { case _: T3 => true; case _ => false }))
```

In `tests/unit/test_compiler.cpp` delete `Compiler.Phase2NodesAreRejectedUntilImplemented` and add:

```cpp
TEST(CompilerPatterns, CascadeShape) {
    const auto l = listing("case class P(x: Int)\ndef f(v: Any) = v match { case P(1) => 1; case s: String => 2; case _ => 3 }");
    EXPECT_TRUE(has(l, "TEST_PROTO"));
    EXPECT_TRUE(has(l, "UNAPPLY_FIELDS"));
    EXPECT_TRUE(has(l, "TEST_TYPE 4 ; String"));
    EXPECT_TRUE(has(l, "MATCH_ERROR"));
    EXPECT_TRUE(has(listing("def f(xs: List[Int]) = xs match { case h :: t => h; case Nil => 0 }"), "UNCONS"));
}

TEST(CompilerPatterns, Errors) {
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case x: Int | x: String => 1 }"),
                    "Illegal variable x in pattern alternative"));
    EXPECT_TRUE(has(compileError("case class P(x: Int)\ndef f(v: Any) = v match { case P(a, b) => 1 }"),
                    "wrong number of arguments for pattern P"));
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case q: Nope => 1 }"), "Not found: type Nope"));
    EXPECT_TRUE(has(compileError("def f(v: Any) = v match { case Zork(a) => 1 }"), "Not found: Zork"));
}
```

Run: `cmake --build build_release && ctest --test-dir build_release -R '11-pattern|CompilerPatterns' --output-on-failure` → FAIL.

- [ ] **Step 2: `patternVariables` in AST**

Move `Desugarer::patternVariables` (Task 3) to `AST.cpp` as the free function `void patternVariables(const Pattern& p, std::vector<std::string>& out)` declared in `AST.h`; Desugar calls it.

- [ ] **Step 3: Capture analysis of cases**

In `CaptureAnalysis::walk` (`Compiler.cpp`), the `Match` case of Task 3 becomes:

```cpp
            case NodeKind::Match: {
                const auto& m = as<Match>(n);
                walk(m.scrutinee.get(), depth);
                for (const CaseDef& c : m.cases) {
                    walkPatternPaths(*c.pattern, depth);  // stable identifiers and extractor objects
                    scopes_.emplace_back();
                    std::vector<std::string> vars;
                    patternVariables(*c.pattern, vars);
                    for (const auto& v : vars) scopes_.back().names[v] = Decl{nullptr, depth, DeclKind::Param, -1};
                    walk(c.guard.get(), depth);
                    walk(c.body.get(), depth);
                    scopes_.pop_back();
                }
                return;
            }
```

with

```cpp
    void walkPatternPaths(const Pattern& p, int depth) {
        if (p.expr && (p.kind == Pattern::Kind::Stable || p.kind == Pattern::Kind::Extractor ||
                       p.kind == Pattern::Kind::Literal))
            walk(*p.expr, depth);
        for (const auto& a : p.args) walkPatternPaths(*a, depth);
    }
```

(Pattern variables are never boxed: like parameters they are bound once, before any closure over them is created.)

- [ ] **Step 4: The cascade (`src/compiler/CompilePatterns.cpp`)**

Declarations in `Compiler.h` (private):

```cpp
    // --- Pattern matching (CompilePatterns.cpp) ---
    void compileMatch(const Match& m);
    void compilePattern(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void compileExtractor(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void compileListPattern(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void extractInto(const std::vector<std::string>& keys, int slot, const std::vector<PatternPtr>& subs,
                     std::vector<std::size_t>& fail, SourcePos pos);
    void emitProtoTest(const std::string& typeKey, int slot, SourcePos pos, std::vector<std::size_t>& fail);
    void bindPattern(const std::string& name, int slot, SourcePos pos);
    void checkNoVariables(const Pattern& p) const;
    // Pushes a Boolean: is the value in `slot` a T? Returns false (emitting
    // nothing) when every value matches, which only `Any` in a pattern does.
    bool compileTypeTest(const TypeTree& t, int slot, SourcePos pos, bool inPattern);
    void compileInstanceOf(const Node& value, const TypeTree& t, bool cast, SourcePos pos);
```

```cpp
/*
 * Compiler members for pattern matching (DESIGN §5.3). `e match { cases }`
 * is a decision cascade: the scrutinee lives in a local slot; each pattern
 * tests and extracts from slots and binds its variables in the case's scope;
 * a failed test jumps to the next case; the guard runs after binding; no
 * matching case raises MatchError.
 */
#include "compiler/Compiler.h"

#include <unordered_map>

namespace protoScala {

void Compiler::compileMatch(const Match& m) {
    compileExpr(*m.scrutinee);
    const int scrutinee = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(scrutinee), m.pos, -1);
    std::vector<std::size_t> toEnd;
    for (const CaseDef& c : m.cases) {
        fn_->scopes.emplace_back();
        std::vector<std::size_t> fail;
        compilePattern(*c.pattern, scrutinee, fail);
        if (c.guard) {
            compileExpr(*c.guard);
            fail.push_back(emitJump(Op::JUMP_IF_FALSE, c.pos, -1));
        }
        compileExpr(*c.body);
        toEnd.push_back(emitJump(Op::JUMP, c.pos, 0));
        adjust(-1);  // the next case starts without this case's value
        for (std::size_t f : fail) fn_->mod->patchJumpTo(f, fn_->mod->pos());
        fn_->scopes.pop_back();
    }
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(scrutinee), m.pos, +1);
    emit(Op::MATCH_ERROR, 0, m.pos, 0);  // throws; the pushed value stands for the result at the join
    for (std::size_t j : toEnd) fn_->mod->patchJumpTo(j, fn_->mod->pos());
}

void Compiler::bindPattern(const std::string& name, int slot, SourcePos pos) {
    if (name == "_") return;
    const LocalInfo info = declareLocal(name, BindingKind::Val, /*boxed=*/false);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(info.slot), pos, -1);
}

void Compiler::emitProtoTest(const std::string& typeKey, int slot, SourcePos pos,
                             std::vector<std::size_t>& fail) {
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::TEST_PROTO, fn_->mod->addSymbol(typeKey), pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, pos, -1));
}

// Reads the attributes `keys` of the value in `slot` into fresh slots and
// matches `subs` against them.
void Compiler::extractInto(const std::vector<std::string>& keys, int slot, const std::vector<PatternPtr>& subs,
                           std::vector<std::size_t>& fail, SourcePos pos) {
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::UNAPPLY_FIELDS, fn_->mod->addNames(keys), pos, static_cast<int>(keys.size()) - 1);
    std::vector<int> slots(keys.size());
    for (std::size_t k = keys.size(); k-- > 0;) {
        slots[k] = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(slots[k]), pos, -1);
    }
    for (std::size_t k = 0; k < subs.size(); ++k) compilePattern(*subs[k], slots[k], fail);
}

void Compiler::checkNoVariables(const Pattern& p) const {
    std::vector<std::string> vars;
    patternVariables(p, vars);
    if (!vars.empty())
        throw CompileError("Illegal variable " + vars.front() + " in pattern alternative", p.pos);
}

void Compiler::compilePattern(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    checkNativeStack(StackUse::Source);
    using K = Pattern::Kind;
    switch (p.kind) {
        case K::Wildcard:
            return;
        case K::Var:
            bindPattern(p.name, slot, p.pos);
            return;
        case K::Literal:
        case K::Stable:  // the pattern's value == the scrutinee (SLS 8.1.4-8.1.5)
            compileExpr(*p.expr);
            emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
            emit(Op::EQ, 0, p.pos, -1);
            fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
            return;
        case K::Typed:
            if (compileTypeTest(*p.type, slot, p.pos, /*inPattern=*/true))
                fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
            compilePattern(*p.args[0], slot, fail);
            return;
        case K::Bind:
            compilePattern(*p.args[0], slot, fail);
            bindPattern(p.name, slot, p.pos);
            return;
        case K::Alt: {
            std::vector<std::size_t> matched;
            for (std::size_t k = 0; k < p.args.size(); ++k) {
                checkNoVariables(*p.args[k]);
                if (k + 1 == p.args.size()) {
                    compilePattern(*p.args[k], slot, fail);
                    break;
                }
                std::vector<std::size_t> next;
                compilePattern(*p.args[k], slot, next);
                matched.push_back(emitJump(Op::JUMP, p.pos, 0));
                for (std::size_t f : next) fn_->mod->patchJumpTo(f, fn_->mod->pos());
            }
            for (std::size_t j : matched) fn_->mod->patchJumpTo(j, fn_->mod->pos());
            return;
        }
        case K::Tuple: {
            const std::size_t n = p.args.size();
            if (n > kMaxTupleArity)
                throw CompileError("tuple patterns of more than 22 elements are not supported (D32)", p.pos);
            emitProtoTest(tupleTypeKey(static_cast<unsigned>(n)), slot, p.pos, fail);
            std::vector<std::string> keys;
            for (std::size_t k = 1; k <= n; ++k) keys.push_back("_" + std::to_string(k));
            extractInto(keys, slot, p.args, fail, p.pos);
            return;
        }
        case K::Extractor:
            compileExtractor(p, slot, fail);
            return;
        case K::SeqWildcard:
            throw CompileError("a sequence wildcard is only allowed as the last argument of a sequence "
                               "pattern such as List(a, rest*)", p.pos);
    }
}

void Compiler::compileExtractor(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    const std::string& name = p.name;
    bool shadowed = false;  // a local or member with the extractor's name
    if (p.expr->kind == NodeKind::Ident) {
        bool found = false;
        captureInto(fn_, name, p.pos, &found);
        shadowed = found || memberOf(name) != nullptr;
    }
    // h :: t on a List: getAt(0) and the tail slice (DESIGN §5.3, §6).
    if (name == "::" && !shadowed) {
        if (p.args.size() != 2) throw CompileError("the :: pattern takes two patterns", p.pos);
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::ConsList), p.pos, 0);
        fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::UNCONS, 0, p.pos, +1);
        const int tail = newSlot();
        const int head = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(tail), p.pos, -1);
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(head), p.pos, -1);
        compilePattern(*p.args[0], head, fail);
        compilePattern(*p.args[1], tail, fail);
        return;
    }
    // List(p1, ..., pn[, rest*]) on a List.
    if (name == "List" && !shadowed) {
        const GlobalBinding* g = globals_.binding("List");
        if (g && g->kind == BindingKind::Builtin) {
            compileListPattern(p, slot, fail);
            return;
        }
    }
    // A case class whose companion keeps the synthesised unapply (or a tuple
    // class): its fields, read by key (DESIGN §5.3).
    const ClassInfo* cls = shadowed ? nullptr : globals_.findType(name);
    const GlobalBinding* term = globals_.binding(name);
    const bool synthesised = cls && cls->isCase && cls->kind == ClassKind::Class &&
                             ((cls->builtin && !term) ||
                              (!cls->companionHasUnapply && term && term->key == cls->companionTermKey));
    if (synthesised) {
        if (p.args.size() != cls->fields.size())
            throw CompileError("wrong number of arguments for pattern " + name + ": expected " +
                                   std::to_string(cls->fields.size()) + ", found " +
                                   std::to_string(p.args.size()), p.pos);
        emitProtoTest(cls->key, slot, p.pos, fail);
        extractInto(cls->fields, slot, p.args, fail, p.pos);
        return;
    }
    if (!shadowed && !term && p.expr->kind == NodeKind::Ident)
        throw CompileError("Not found: " + name, p.pos);
    // A custom extractor (Scala 3): X.unapply(v) is a Boolean for X(), else a
    // value answering isEmpty/get whose get is the single result or a tuple.
    // The parameter type of unapply is not checked (types are erased, D29).
    compileExpr(*p.expr);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("unapply", 1), p.pos, -1);
    const int result = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(result), p.pos, -1);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(result), p.pos, +1);
    if (p.args.empty()) {
        fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
        return;
    }
    emit(Op::SEND, fn_->mod->addSendSite("isEmpty", 0), p.pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_TRUE, p.pos, -1));
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(result), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("get", 0), p.pos, 0);
    const int got = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(got), p.pos, -1);
    if (p.args.size() == 1) {
        compilePattern(*p.args[0], got, fail);
        return;
    }
    std::vector<std::string> keys;
    for (std::size_t k = 1; k <= p.args.size(); ++k) keys.push_back("_" + std::to_string(k));
    extractInto(keys, got, p.args, fail, p.pos);
}

// List(p1, ..., pn) / List(p1, ..., rest*): a size test, the elements by
// index, the rest by drop (DESIGN §5.3 sequence patterns).
void Compiler::compileListPattern(const Pattern& p, int slot, std::vector<std::size_t>& fail) {
    const bool seq = !p.args.empty() && p.args.back()->kind == Pattern::Kind::SeqWildcard;
    const std::size_t fixed = p.args.size() - (seq ? 1 : 0);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::List), p.pos, 0);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
    emit(Op::SEND, fn_->mod->addSendSite("length", 0), p.pos, 0);
    emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(fixed)), p.pos, +1);
    emit(seq ? Op::GE : Op::EQ, 0, p.pos, -1);
    fail.push_back(emitJump(Op::JUMP_IF_FALSE, p.pos, -1));
    for (std::size_t k = 0; k < fixed; ++k) {
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(k)), p.pos, +1);
        emit(Op::SEND, fn_->mod->addSendSite("apply", 1), p.pos, -1);
        const int element = newSlot();
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(element), p.pos, -1);
        compilePattern(*p.args[k], element, fail);
    }
    if (seq && !p.args.back()->name.empty()) {
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), p.pos, +1);
        emit(Op::PUSH_CONST, fn_->mod->addInt(static_cast<long long>(fixed)), p.pos, +1);
        emit(Op::SEND, fn_->mod->addSendSite("drop", 1), p.pos, -1);
        const LocalInfo rest = declareLocal(p.args.back()->name, BindingKind::Val, false);
        emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(rest.slot), p.pos, -1);
    }
}

bool Compiler::compileTypeTest(const TypeTree& t, int slot, SourcePos pos, bool inPattern) {
    std::string name;
    switch (t.kind) {
        case TypeTree::Kind::Name: case TypeTree::Kind::Applied: name = t.name; break;
        case TypeTree::Kind::Tuple: name = "Tuple" + std::to_string(t.args.size()); break;
        case TypeTree::Kind::Function: name = "Function"; break;
        default: throw CompileError("this type cannot be tested at run time", pos);
    }
    for (const char* prefix : {"scala.", "java.lang.", "Predef."})
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
    static const std::unordered_map<std::string, TypeCode> builtin = {
        {"Int", TypeCode::Integer}, {"Long", TypeCode::Integer}, {"Short", TypeCode::Integer},
        {"Byte", TypeCode::Integer}, {"BigInt", TypeCode::Integer}, {"Integer", TypeCode::Integer},
        {"Double", TypeCode::Double}, {"Float", TypeCode::Double}, {"Boolean", TypeCode::Boolean},
        {"Char", TypeCode::Char}, {"String", TypeCode::String}, {"Unit", TypeCode::Unit},
        {"List", TypeCode::List}, {"Function", TypeCode::Function}, {"AnyRef", TypeCode::AnyRef},
        {"Object", TypeCode::AnyRef}, {"AnyVal", TypeCode::AnyVal}, {"Null", TypeCode::Null},
        {"Nothing", TypeCode::Nothing}};
    if (name == "Any") {
        if (inPattern) return false;  // `case x: Any` matches everything
        emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::NonNull), pos, 0);
        return true;
    }
    auto code = builtin.find(name);
    if (code == builtin.end() && name.rfind("Function", 0) == 0 && name.size() > 8 &&
        name.find_first_not_of("0123456789", 8) == std::string::npos)
        code = builtin.find("Function");  // Function0..Function22
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    if (code != builtin.end()) {
        emit(Op::TEST_TYPE, static_cast<std::uint64_t>(code->second), pos, 0);
        return true;
    }
    const ClassInfo* c = globals_.findType(name);
    if (!c) throw CompileError("Not found: type " + name, pos);
    emit(Op::TEST_PROTO, fn_->mod->addSymbol(c->key), pos, 0);
    return true;
}

// x.isInstanceOf[T] / x.asInstanceOf[T] (LANGUAGE §3): only the class of T is
// checked (erasure); null is an instance of nothing and casts to anything (D29).
void Compiler::compileInstanceOf(const Node& value, const TypeTree& t, bool cast, SourcePos pos) {
    compileExpr(value);
    const int slot = newSlot();
    emit(Op::STORE_LOCAL, static_cast<std::uint64_t>(slot), pos, -1);
    if (!cast) {
        compileTypeTest(t, slot, pos, /*inPattern=*/false);
        return;
    }
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::TEST_TYPE, static_cast<std::uint64_t>(TypeCode::Null), pos, 0);
    const std::size_t isNull = emitJump(Op::JUMP_IF_TRUE, pos, -1);
    compileTypeTest(t, slot, pos, /*inPattern=*/false);
    const std::size_t ok = emitJump(Op::JUMP_IF_TRUE, pos, -1);
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
    emit(Op::CAST_FAIL, fn_->mod->addString(dump(t)), pos, 0);
    adjust(-1);  // CAST_FAIL never returns
    fn_->mod->patchJumpTo(isNull, fn_->mod->pos());
    fn_->mod->patchJumpTo(ok, fn_->mod->pos());
    emit(Op::PUSH_LOCAL, static_cast<std::uint64_t>(slot), pos, +1);
}

} // namespace protoScala
```

In `Compiler::compileExpr`: `case NodeKind::Match: compileMatch(as<Match>(n)); return;` and the `TypeApply` case becomes

```cpp
        case NodeKind::TypeApply: {
            const auto& ta = as<TypeApply>(n);
            if (ta.fn->kind == NodeKind::Select && ta.types.size() == 1) {
                const auto& sel = as<Select>(*ta.fn);
                if (sel.name == "isInstanceOf" || sel.name == "asInstanceOf") {
                    compileInstanceOf(*sel.qualifier, *ta.types[0], sel.name == "asInstanceOf", n.pos);
                    return;
                }
            }
            compileExpr(*ta.fn);  // other type arguments are erased
            return;
        }
```

Add `src/compiler/CompilePatterns.cpp` to `protoscala_compiler`.

- [ ] **Step 5: Engine-level checks**

Append to `tests/unit/test_engine.cpp`:

```cpp
TEST(EnginePatterns, MatchBindsGuardsAndFails) {
    EvalHarness h;
    EXPECT_EQ(h.eval("List(1, 2, 3) match { case h :: t => h + t.length; case Nil => 0 }"), "3");
    EXPECT_EQ(h.eval("(1, \"a\") match { case (n, s) => s * (n + 1) }"), "aa");
    EXPECT_EQ(h.eval("Some(4) match { case Some(n) if n > 3 => n; case _ => 0 }"), "4");
    EXPECT_EQ(h.eval("3 match { case 1 => 1 }"), "error: MatchError: 3 (of class Int)");
    EXPECT_EQ(h.eval("(1, 2).isInstanceOf[Product]"), "true");
    EXPECT_EQ(h.eval("\"x\".asInstanceOf[Int]"), "error: ClassCastException: String cannot be cast to Int");
    EXPECT_EQ(h.eval("null.asInstanceOf[String] == null"), "true");
}
```

- [ ] **Step 6: Run and commit**

Run: `cmake --build build_release && ctest --test-dir build_release -R '11-pattern|Patterns|Compiler|Desugar' --output-on-failure` → PASS; full suite → 100%; `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release -R 11-pattern` → PASS.

```bash
git add src tests CMakeLists.txt
git commit -m "pattern matching: every DESIGN §5.3 pattern, MatchError, isInstanceOf/asInstanceOf

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 13: For-comprehensions end to end

**Files:**
- Create: `tests/conformance/12-for-comprehensions/*.scala`
- Modify: only if a fixture fails (Desugar did the rewriting in Task 3; `List`/`Option` supply the methods since Tasks 10–11; pattern generators compile since Task 12)

**Interfaces:**
- Consumes: Tasks 3, 10, 11, 12.
- Produces: the ROADMAP "for-comprehensions over `List`" fixtures, in all three syntaxes, plus `Option`, a user class, mixed `List`/`Option`, pattern generators (with Scala 3.9's `case`, which protoScala also accepts without), value definitions and the lazy-guard ordering.

- [ ] **Step 1: Fixtures**

`tests/conformance/12-for-comprehensions/`:

`yield-parens.scala`
```scala
// EXPECT: List(2, 4, 6)
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val ys = for (x <- xs) yield x * 2
  println(ys)
```

`yield-braces.scala`
```scala
// EXPECT: List(2, 4, 6)
@main def run(): Unit = {
  val xs = List(1, 2, 3)
  val ys = for { x <- xs } yield x * 2
  println(ys)
}
```

`yield-indent.scala`
```scala
// EXPECT: List(2, 4, 6)
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val ys =
    for
      x <- xs
    yield x * 2
  println(ys)
```

`nested-with-guard.scala` (`scalac`: `List(10, 20, 40)`)
```scala
// EXPECT: List(10, 20, 40)
@main def run(): Unit =
  val r = for { x <- List(1, 2); y <- List(10, 20) if x + y != 21 } yield x * y
  println(r)
```

`nested-indent.scala`
```scala
// EXPECT: List(10, 20, 40)
@main def run(): Unit =
  val r =
    for
      x <- List(1, 2)
      y <- List(10, 20)
      if x + y != 21
    yield x * y
  println(r)
```

`foreach-do.scala`
```scala
// EXPECT: 1a 1b 2a 2b
@main def run(): Unit =
  var out = ""
  for x <- List(1, 2); y <- List("a", "b") do out = out + (if out.isEmpty then "" else " ") + x + y
  println(out)
```

`foreach-old-style.scala`
```scala
// EXPECT: 6
@main def run(): Unit =
  var total = 0
  for (x <- List(1, 2, 3))
    total += x
  println(total)
```

`value-definitions.scala` (`scalac`: `List(4, 6) List((1,1), (2,4), (3,9))`)
```scala
// EXPECT: List(4, 6) List((1,1), (2,4), (3,9))
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val a = for (x <- xs; y = x * 2 if y > 2) yield y
  val b = for (x <- xs; sq = x * x) yield (x, sq)
  println(a.toString + " " + b)
```

`pattern-generators.scala` (`scalac`: `List(a1, b2) List(1, 3)`)
```scala
// EXPECT: List(a1, b2) List(1, 3)
@main def run(): Unit =
  val pairs = List(("a", 1), ("b", 2))
  val xs = for ((s, n) <- pairs) yield s + n
  val ys = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  println(xs.toString + " " + ys)
```

`guards-are-lazy.scala` (`scalac`: `f1 x1 f2 x2 f3 x3`)
```scala
// EXPECT: f1 x1 f2 x2 f3 x3
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
@main def run(): Unit =
  for (x <- List(1, 2, 3) if { note("f" + x); true }) note("x" + x)
  println(log)
```

`over-option.scala`
```scala
// EXPECT: Some(3) None Some(6)
@main def run(): Unit =
  val a = for (x <- Some(1); y <- Some(2)) yield x + y
  val b = for (x <- Some(1); y <- (None: Option[Int])) yield x + y
  val c = for { x <- Some(3) if x > 2 } yield x * 2
  println(a.toString + " " + b + " " + c)
```

`over-user-class.scala`
```scala
// EXPECT: Box(12)
case class Box(v: Int):
  def map(f: Int => Int): Box = Box(f(v))
  def flatMap(f: Int => Box): Box = f(v)

@main def run(): Unit =
  println(for (a <- Box(3); b <- Box(4)) yield a * b)
```

`list-and-option.scala` (`scalac`: `List(1, 3)`)
```scala
// EXPECT: List(1, 3)
@main def run(): Unit =
  val xs = for (x <- List(1, 2, 3); y <- (if x % 2 == 1 then Some(x) else None)) yield y
  println(xs)
```

Run: `ctest --test-dir build_release -R 12-for --output-on-failure`
Expected: PASS; a failure is a defect in Tasks 2, 3, 10–12 — fix it there with a unit test first.

- [ ] **Step 2: Commit**

```bash
git add tests/conformance/12-for-comprehensions
git commit -m "tests: for-comprehensions over List, Option and user classes, in the three syntaxes

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 14: The REPL — classes, objects, pattern matching and redefinition

**Files:**
- Create: `tests/cli/repl-classes.sh`
- Modify: `src/repl/Session.cpp`, `src/compiler/Compiler.cpp` (REPL definitions), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 7–13.
- Produces (Open question Q13): the REPL echoes `// defined class C`, `// defined case class C`, `// defined trait T`, `// defined object O`, `// defined case object O` (the Scala 3 REPL's wording; a synthesised companion is not echoed); results are shown with the value's Scala `toString` (`engine_.showTopLevel`); a redefinition of a class shadows the old one (a fresh type key `@C#N`), and values built from the old class keep it; a class and its companion must be defined in the same input (the Scala REPL's rule); generated names (`<t0>`, …) are never echoed.

- [ ] **Step 1: A failing CLI test**

`tests/cli/repl-classes.sh`:

```bash
#!/usr/bin/env bash
#
# CLI check: classes, objects, case classes, match and for at the REPL,
# multi-line template input, and class redefinition (Phase 2 plan, Task 14).
#
# Usage: repl-classes.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-classes.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-classes.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
out=$(printf '%s\n' \
    'case class Point(x: Int, y: Int)' \
    'val p = Point(1, 2)' \
    'p.copy(y = 5)' \
    'trait Shape:' \
    '  def area: Double' \
    '' \
    'class Sq(s: Double) extends Shape:' \
    '  def area = s * s' \
    '' \
    'new Sq(3.0).area' \
    'object Counter:' \
    '  var n = 0' \
    '' \
    'Counter.n += 1' \
    'Counter.n' \
    'p match' \
    '  case Point(a, b) => a + b' \
    '' \
    'val (u, v) = (7, 8)' \
    'for (x <- List(1, 2)) yield x * 10' \
    'case class Point(x: Int)' \
    'p' \
    'Point(7)' \
    ':quit' | HOME="$work/home" timeout 60s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; cat "$work/err"; exit 1; }
plain="${out//scala> /}"
plain="${plain//     | /}"
for piece in '// defined case class Point' 'val p = Point(1,2)' 'val res0 = Point(1,5)' \
              '// defined trait Shape' '// defined class Sq' 'val res1 = 9.0' \
              '// defined object Counter' 'val res2 = 1' 'val res3 = 3' 'val u = 7' 'val v = 8' \
              'val res4 = List(10, 20)' 'val res5 = Point(1,2)' 'val res6 = Point(7)'; do
    grep -qxF -- "$piece" <<<"$plain" || { echo "FAIL: no '$piece' in:"; echo "$out"; cat "$work/err"; exit 1; }
done
[[ $(grep -c '// defined case class Point' <<<"$plain") -eq 2 ]] || { echo "FAIL: redefinition not echoed"; echo "$out"; exit 1; }
if grep -qF '<t' <<<"$plain"; then echo "FAIL: a generated name was echoed"; echo "$out"; exit 1; fi
if grep -qF 'defined object Point' <<<"$plain"; then echo "FAIL: synthetic companion echoed"; exit 1; fi
[[ ! -s "$work/err" ]] || { echo "FAIL: unexpected stderr:"; cat "$work/err"; exit 1; }
echo OK
```

Register it in the `foreach(cli_test ...)` list of `tests/CMakeLists.txt` (the one-argument group). Run: `ctest --test-dir build_release -R cli/repl-classes --output-on-failure` → FAIL (at least the `<t0>` echo and, depending on the order of earlier tasks, the result rendering).

- [ ] **Step 2: Echo**

In `Compiler::compileUnit` (step 1), skip REPL definitions whose name starts with `<` (pattern-`val` temporaries) when filling `out.definitions`.

In `Session::evaluate` (`src/repl/Session.cpp:101-127`), render results with the engine active so Scala `toString` methods run, and report a `toString` that throws:

```cpp
    try {
        if (outcome) {
            for (const ReplDefinition& d : cu.definitions) {
                if (d.text.rfind("val ", 0) == 0 || d.text.rfind("var ", 0) == 0)
                    outcome->echo.push_back(d.text + " = " + showResult(&ctx, global(d.key)));
                else
                    outcome->echo.push_back(d.text);  // "def f", "lazy val x", "// defined class C"
            }
            if (bindsResult)
                outcome->echo.push_back("val " + cu.resultName + " = " + showResult(&ctx, global(cu.resultKey)));
        }
    } catch (const ScalaError& e) {  // a toString that throws
        std::fflush(stdout);
        std::fprintf(stderr, "%s: error: %s\n", sourceName.c_str(), e.what());
        return EvalStatus::Error;
    }
```

where `showResult` becomes a `Session` member using `engine_.showTopLevel(ctx, v)` (quoted for a `String`, as today).

- [ ] **Step 3: Run and commit**

Run: `ctest --test-dir build_release -R 'cli/' --output-on-failure` → PASS (all CLI checks).

```bash
git add src/repl src/compiler/Compiler.cpp tests/cli/repl-classes.sh tests/CMakeLists.txt
git commit -m "repl: classes, objects, case classes, match and for; class redefinition

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 15: Object graphs under GC pressure; `attr_lookup` and `object_tree` benchmarks

**Files:**
- Create: `benchmarks/comparable/attr_lookup.scala`, `benchmarks/comparable/object_tree.scala`, `benchmarks/comparable/python/object_tree.py`
- Modify: `tests/cli/gc-pressure.sh`, `benchmarks/run_benchmarks.py`, `benchmarks/README.md`, `benchmarks/RESULTS.md`
- Generated: `benchmarks/reports/<date>-phase2.md`

**Interfaces:**
- Consumes: the complete Phase 2 language.
- Produces: GC-pressure evidence for object graphs (P1, P6); the pending `attr_lookup` twin (protoPython `benchmarks/attr_lookup.py`, protoST `benchmarks/comparable/attr_lookup.st`) and a new deep-object-graph workload consistent with DESIGN §1 (protoScala's value is in persistent structures and deep object graphs, not integer loops).

- [ ] **Step 1: GC pressure (failing first only if a defect exists)**

Append to `tests/cli/gc-pressure.sh` (before the final `echo OK`):

```bash
# Object graphs (Phase 2): immutable case-class trees built, matched and
# dropped every round; a for-comprehension building tuples; a mutable
# instance updated in the loop. Expected values computed independently:
# the sum over rounds r = 0..39 of the leaf values (s % 7) of build(10, r),
# plus 4 tuples per round.
cat >"$work/objects.scala" <<'SCALA'
sealed trait Tree
case class Leaf(v: Int) extends Tree
case class Node(l: Tree, r: Tree) extends Tree
def build(d: Int, s: Int): Tree =
  if d == 0 then Leaf(s % 7) else Node(build(d - 1, 2 * s + 1), build(d - 1, 2 * s + 2))
def sum(t: Tree): Int = t match
  case Leaf(v) => v
  case Node(l, r) => sum(l) + sum(r)
class Counter:
  var n = 0
@main def run(): Unit =
  val c = new Counter
  var total = 0
  var round = 0
  while round < 40 do
    total += sum(build(10, round))
    val ps = for (x <- List(1, 2, 3, 4); y <- List(x, x + 1) if (x + y) % 2 == 1) yield (x, y)
    total += ps.length
    c.n += 1
    round += 1
  println(total.toString + " " + c.n)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/objects.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "123037 40" ]] || { echo "FAIL (objects): exit $rc, '$out'"; exit 1; }
```

Run: `ctest --test-dir build_release -R cli/gc-pressure --output-on-failure` → PASS. A failure here is a rooting defect (P1): find it with the smallest failing program before anything else (`superpowers:systematic-debugging`); never raise the limit to make it pass.

- [ ] **Step 2: The `attr_lookup` twin**

`benchmarks/comparable/attr_lookup.scala`:

```scala
// EXPECT: 600000
// attr_lookup.scala - 100000 iterations reading three fields of an object and
// summing them. Twin of protoPython's benchmarks/attr_lookup.py (BENCH_N=100000)
// and protoST's benchmarks/comparable/attr_lookup.st. Result: 600000.

class FastObject(val a: Int, val b: Int, val c: Int)

def runBench(obj: FastObject, n: Int): Int =
  var total = 0
  var i = 0
  while i < n do
    total += obj.a
    total += obj.b
    total += obj.c
    i += 1
  total

@main def benchAttrLookup(): Unit =
  println(runBench(new FastObject(1, 2, 3), 100000))
```

- [ ] **Step 3: The deep object graph workload**

`benchmarks/comparable/object_tree.scala` (the expected line was computed by the Python twin below):

```scala
// EXPECT: 131071 278364170 725606090
// object_tree.scala - a deep immutable object graph: build a complete binary
// tree of case classes (depth 16, 131071 objects), path-copy its leftmost
// spine (the copy shares every right subtree with the original), and fold
// both versions with pattern matching. Twin of
// benchmarks/comparable/python/object_tree.py.

sealed trait Tree
case class Leaf(value: Int) extends Tree
case class Node(left: Tree, right: Tree, weight: Int) extends Tree

def build(depth: Int, seed: Int): Tree =
  if depth == 0 then Leaf(seed % 1000)
  else Node(build(depth - 1, seed * 2 + 1), build(depth - 1, seed * 2 + 2), depth)

def bump(t: Tree): Tree = t match
  case Leaf(v)       => Leaf(v + 1)
  case Node(l, r, w) => Node(bump(l), r, w)

def count(t: Tree): Int = t match
  case Leaf(_)       => 1
  case Node(l, r, _) => 1 + count(l) + count(r)

def checksum(t: Tree): Long = t match
  case Leaf(v)       => v
  case Node(l, r, w) => (checksum(l) * 31 + checksum(r) + w) % 1000000007L

@main def benchObjectTree(): Unit =
  val t = build(16, 0)
  val t2 = bump(t)
  println(count(t).toString + " " + checksum(t) + " " + checksum(t2))
```

`benchmarks/comparable/python/object_tree.py`:

```python
# object_tree.py - CPython/protoPython twin of benchmarks/comparable/object_tree.scala:
# build a complete binary tree (depth 16, 131071 objects), path-copy its
# leftmost spine, fold both versions. Prints the result on the last line.
# Result: 131071 278364170 725606090.
import sys

sys.setrecursionlimit(10000)
M = 1000000007


class Leaf:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class Node:
    __slots__ = ("left", "right", "weight")

    def __init__(self, left, right, weight):
        self.left = left
        self.right = right
        self.weight = weight


def build(depth, seed):
    if depth == 0:
        return Leaf(seed % 1000)
    return Node(build(depth - 1, seed * 2 + 1), build(depth - 1, seed * 2 + 2), depth)


def bump(t):
    if isinstance(t, Leaf):
        return Leaf(t.value + 1)
    return Node(bump(t.left), t.right, t.weight)


def count(t):
    if isinstance(t, Leaf):
        return 1
    return 1 + count(t.left) + count(t.right)


def checksum(t):
    if isinstance(t, Leaf):
        return t.value
    return (checksum(t.left) * 31 + checksum(t.right) + t.weight) % M


t = build(16, 0)
t2 = bump(t)
print(count(t), checksum(t), checksum(t2))
```

Run: `python3 benchmarks/comparable/python/object_tree.py` → `131071 278364170 725606090`; `build_release/protoscala benchmarks/comparable/object_tree.scala` → the same line (the CTest case `benchmarks/object_tree.scala` checks it; both files are also smoke tests automatically).

- [ ] **Step 4: Harness entries**

In `benchmarks/run_benchmarks.py`, append to `WORKLOADS`:

```python
    {"name": "attr_lookup", "scala": "attr_lookup.scala",
     "what": "3 field reads x 100000", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "attr_lookup.py", {"BENCH_N": "100000"}, "600000"),
     "st": (PROTOST_BENCH / "attr_lookup.st", "600000"),
     "clj": None},
    {"name": "object_tree", "scala": "object_tree.scala",
     "what": "build, path-copy and fold a 131071-object case-class tree", "origin": "protoScala",
     "py": (PY_TWINS_DIR / "object_tree.py", {}, "131071 278364170 725606090"),
     "st": None,
     "clj": None},
```

remove the `attr_lookup` entry from `PENDING`, and add the workload notes (`"object_tree": "the deep-object-graph workload of DESIGN §1; CPython runs the __slots__ twin in comparable/python/."`). In `benchmarks/README.md`, add both rows to the comparable table, drop `attr_lookup` from **Pending**, and note that `object_tree` is protoScala's own twin set (a persistent-structure workload, DESIGN §1).

- [ ] **Step 5: Measure and record**

Run (idle machine; the harness waits for load < 4): `SCALA_HOME=../tools/scala3-3.9.0 benchmarks/bench.sh --only attr_lookup,object_tree,fib,tak --runs 5 --name phase2`
Expected: exit 0, every cell verified; the report `benchmarks/reports/<date>-phase2.md` is written. Add its row to the index table of `benchmarks/RESULTS.md` (date, report, geomean, "Phase 2: attr_lookup and object_tree added; commit `<hash>`; load …") and a "Cold start (Phase 2)" section with the Task 11 measurements. Do not interpret the numbers beyond what the report states (DESIGN §1: performance is not a Phase 2 goal); if `object_tree` takes more than 10 s on protoScala, report it — it is still recorded, not tuned in this phase.

- [ ] **Step 6: Commit**

```bash
git add tests/cli/gc-pressure.sh benchmarks
git commit -m "benchmarks: attr_lookup twin and the object_tree deep-graph workload; GC pressure for object graphs

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 16: Tutorial — chapters 6, 7 and 9; chapters 2, 3, 5 and 14 extended

**Files:**
- Create: `docs/tutorial/06-classes-objects-and-traits.md`, `docs/tutorial/07-case-classes-and-pattern-matching.md`, `docs/tutorial/09-for-comprehensions.md`, the fixtures below under `tests/conformance/tutorial/`
- Modify: `docs/TUTORIAL.md`, `docs/tutorial/02-for-the-python-or-javascript-developer.md`, `docs/tutorial/03-for-the-scala-developer.md`, `docs/tutorial/05-functions-and-closures.md`, `docs/tutorial/14-repl-and-tooling.md`, `tests/cli/tutorial-repl.sh`

**Interfaces:**
- Consumes: the Phase 2 language (Tasks 1–15).
- Produces: the Documentation-track deliverables of Phase 2 (ROADMAP): the chapters TUTORIAL.md assigns to Phase 2 — **6** (classes, objects, traits), **7** (case classes and pattern matching) and **9** (for-comprehensions; TUTORIAL.md numbers it 9, Open question Q14) — for both audiences; every runnable snippet verbatim in a fixture whose `// EXPECT:` line is the output printed under it.

Every chapter follows the Phase 1 chapters' shape (`docs/tutorial/05-functions-and-closures.md`): an "Implementation status" box, numbered sections, each snippet preceded by `Fixture: [...](../../tests/conformance/tutorial/<file>)` and followed by a `Prints:` block, prose for both audiences, and D-ids where protoScala departs from Scala 3.

- [ ] **Step 1: Fixtures (failing until the chapter snippets are copied from them verbatim)**

`tests/conformance/tutorial/06-classes-point.scala`
```scala
// EXPECT: Point(3, 4) 7 Point(4, 4)
class Point(val x: Int, var y: Int):
  def sum = x + y
  def moved(dx: Int) = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"

@main def run(): Unit =
  val p = new Point(3, 4)
  println(p.toString + " " + p.sum + " " + p.moved(1))
```

`06-classes-counter.scala`
```scala
// EXPECT: 2 12
class Counter:
  var count = 0
  def inc(): Unit = count += 1

@main def run(): Unit =
  val c = new Counter
  c.inc()
  c.inc()
  val two = c.count
  c.count += 10
  println(two.toString + " " + c.count)
```

`06-classes-objects.scala`
```scala
// EXPECT: Temp(21.0) Temp(100.0) 2
class Temp(val celsius: Double):
  override def toString = "Temp(" + celsius + ")"

object Temp:
  var made = 0
  def apply(c: Double): Temp =
    made += 1
    new Temp(c)
  def fromF(f: Double): Temp = apply((f - 32) * 5 / 9)

@main def run(): Unit =
  println(Temp(21.0).toString + " " + Temp.fromF(212.0) + " " + Temp.made)
```

`06-classes-traits.scala`
```scala
// EXPECT: T2>T1>B>A
abstract class A:
  def who: String = "A"
trait T1 extends A:
  override def who = "T1>" + super.who
trait T2 extends A:
  override def who = "T2>" + super.who
class B extends A:
  override def who = "B>" + super.who
class C extends B with T1 with T2

@main def run(): Unit = println(new C().who)
```

`06-classes-abstract.scala`
```scala
// EXPECT: circle 12.56 square 4.0
trait Shape:
  def area: Double
  def name: String
  def describe = name + " " + area

class Circle(r: Double) extends Shape:
  def area = 3.14 * r * r
  def name = "circle"

class Square(side: Double) extends Shape:
  def area = side * side
  def name = "square"

@main def run(): Unit =
  println(new Circle(2.0).describe + " " + new Square(2.0).describe)
```

`06-classes-private.scala`
```scala
// EXPECT-ERROR: value balance is not a member of Account
class Account(private val balance: Int):
  def canPay(amount: Int) = amount <= balance

@main def run(): Unit =
  val a = new Account(100)
  println(a.canPay(50))
  println(a.balance)
```

`06-classes-apply.scala`
```scala
// EXPECT: 0 7 10
class Pair:
  var first = 0
  var second = 0
  def apply(i: Int): Int = if i == 0 then first else second
  def update(i: Int, v: Int): Unit = if i == 0 then first = v else second = v

@main def run(): Unit =
  val p = new Pair
  p(1) = 7
  val scale = (n: Int) => n * 10
  println(p(0).toString + " " + p(1) + " " + scale(1))
```

`07-case-basics.scala`
```scala
// EXPECT: Point(1,2) true Point(1,5) 3
case class Point(x: Int, y: Int)

@main def run(): Unit =
  val p = Point(1, 2)
  println(p.toString + " " + (p == Point(1, 2)) + " " + p.copy(y = 5) + " " + (p.x + p.y))
```

`07-case-tuples.scala`
```scala
// EXPECT: (Ada,36) Ada 36 true
@main def run(): Unit =
  val person = ("Ada", 36)
  val (name, age) = person
  println(person.toString + " " + name + " " + age + " " + (person == ("Ada", 36)))
```

`07-case-option.scala`
```scala
// EXPECT: Some(4) None 4 0
def half(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None

@main def run(): Unit =
  println(half(8).toString + " " + half(3) + " " + half(8).getOrElse(0) + " " + half(3).getOrElse(0))
```

`07-case-adt.scala`
```scala
// EXPECT: 14 (2 + (3 * 4))
sealed trait Expr
case class Num(n: Int) extends Expr
case class Add(l: Expr, r: Expr) extends Expr
case class Mul(l: Expr, r: Expr) extends Expr

def eval(e: Expr): Int = e match
  case Num(n) => n
  case Add(l, r) => eval(l) + eval(r)
  case Mul(l, r) => eval(l) * eval(r)

def show(e: Expr): String = e match
  case Num(n) => n.toString
  case Add(l, r) => "(" + show(l) + " + " + show(r) + ")"
  case Mul(l, r) => "(" + show(l) + " * " + show(r) + ")"

@main def run(): Unit =
  val e = Add(Num(2), Mul(Num(3), Num(4)))
  println(eval(e).toString + " " + show(e))
```

`07-case-patterns.scala`
```scala
// EXPECT: zero small:3 list:1+2 pair tuple:a text other
def kind(v: Any): String = v match
  case 0 => "zero"
  case n: Int if n < 10 => "small:" + n
  case h :: t if t.length == 1 => "list:" + h + "+" + t.head
  case (_, _: Int) => "pair"
  case (s: String, _) => "tuple:" + s
  case "hello" | "hi" => "text"
  case _ => "other"

@main def run(): Unit =
  println(List(0, 3, List(1, 2), ("k", 5), ("a", "b"), "hi", 3.5).map(kind).mkString(" "))
```

`07-case-extractors.scala`
```scala
// EXPECT: even:5 odd a->b none
object Even:
  def unapply(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None
object Split:
  def unapply(s: String): Option[(String, String)] =
    val i = s.indexOf("=")
    if i < 0 then None else Some((s.substring(0, i), s.substring(i + 1)))

def num(n: Int) = n match
  case Even(half) => "even:" + half
  case _ => "odd"
def kv(s: String) = s match
  case Split(k, v) => k + "->" + v
  case _ => "none"

@main def run(): Unit = println(num(10) + " " + num(3) + " " + kv("a=b") + " " + kv("ab"))
```

`07-case-match-error.scala`
```scala
// EXPECT-ERROR: MatchError: 5 (of class Int)
@main def run(): Unit =
  val r = 5 match
    case 1 => "one"
  println(r)
```

`09-for-yield.scala`
```scala
// EXPECT: List(2, 4, 6) List(10, 20, 40)
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val doubled = for x <- xs yield x * 2
  val products =
    for
      x <- List(1, 2)
      y <- List(10, 20)
      if x + y != 21
    yield x * y
  println(doubled.toString + " " + products)
```

`09-for-do.scala`
```scala
// EXPECT: 1a 1b 2a 2b
@main def run(): Unit =
  var out = ""
  for x <- List(1, 2); y <- List("a", "b") do
    out = out + (if out.isEmpty then "" else " ") + x + y
  println(out)
```

`09-for-desugared.scala`
```scala
// EXPECT: true
@main def run(): Unit =
  val a = for (x <- List(1, 2); y <- List(10, 20) if x + y != 21) yield x * y
  val b = List(1, 2).flatMap(x => List(10, 20).withFilter(y => x + y != 21).map(y => x * y))
  println(a == b)
```

`09-for-patterns.scala`
```scala
// EXPECT: List(a1, b2) List(1, 3) List(4, 6)
@main def run(): Unit =
  val pairs = List(("a", 1), ("b", 2))
  val joined = for ((s, n) <- pairs) yield s + n
  val present = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  val big = for (x <- List(1, 2, 3); y = x * 2 if y > 2) yield y
  println(joined.toString + " " + present + " " + big)
```

`09-for-option.scala`
```scala
// EXPECT: Some(3) None
def parse(s: String): Option[Int] = if s == "1" then Some(1) else if s == "2" then Some(2) else None

@main def run(): Unit =
  val ok = for (a <- parse("1"); b <- parse("2")) yield a + b
  val bad = for (a <- parse("1"); b <- parse("x")) yield a + b
  println(ok.toString + " " + bad)
```

`09-for-placeholders.scala`
```scala
// EXPECT: List(2, 3, 4) List(3) 6
@main def run(): Unit =
  val xs = List(1, 2, 3)
  var sum = 0
  xs.foreach(sum += _)
  println(xs.map(_ + 1).toString + " " + xs.filter(_ > 2) + " " + sum)
```

Chapter 2 and 3 fixtures:

`02-python-js-classes.scala`
```scala
// EXPECT: Rex says woof; Tom says meow
trait Animal:
  def name: String
  def sound: String
  def speak = name + " says " + sound

class Dog(val name: String) extends Animal:
  def sound = "woof"

class Cat(val name: String) extends Animal:
  def sound = "meow"

@main def run(): Unit =
  println(new Dog("Rex").speak + "; " + new Cat("Tom").speak)
```

`02-python-js-match.scala`
```scala
// EXPECT: circle:3.0 rect:2.0x5.0 unknown
case class Circle(r: Double)
case class Rect(w: Double, h: Double)

def describe(shape: Any): String = shape match
  case Circle(r) => "circle:" + r
  case Rect(w, h) => "rect:" + w + "x" + h
  case _ => "unknown"

@main def run(): Unit =
  println(describe(Circle(3.0)) + " " + describe(Rect(2.0, 5.0)) + " " + describe(42))
```

`02-python-js-comprehension.scala`
```scala
// EXPECT: List(0, 4, 16)
@main def run(): Unit =
  val xs = List(0, 1, 2, 3, 4)
  println(for (x <- xs if x % 2 == 0) yield x * x)
```

`03-scala-dev-d28-construction.scala` (Scala prints `n1 true`)
```scala
// EXPECT: n1 false
class Node(val id: Int):
  Registry.last = this
  val label = "n" + id

object Registry:
  var last: Any = null

@main def run(): Unit =
  val n = new Node(1)
  println(n.label + " " + (Registry.last == n))
```

`03-scala-dev-d29-type-tests.scala` (Scala prints `false false`)
```scala
// EXPECT: true true
@main def run(): Unit =
  println(3L.isInstanceOf[Int].toString + " " + (1L << 40).isInstanceOf[Int])
```

`03-scala-dev-d30-uninitialised-field.scala` (Scala prints `1`)
```scala
// EXPECT-ERROR: value b is not a member of A
class A:
  val a = b + 1
  val b = 1

@main def run(): Unit = println(new A().a)
```

`03-scala-dev-d31-no-overloading.scala`
```scala
// EXPECT-ERROR: f is already defined in Calc
class Calc:
  def f(x: Int) = x
  def f(x: String) = x.length
```

`03-scala-dev-d33-getorelse-eager.scala` (Scala prints `1 ` — the default is by-name)
```scala
// EXPECT: 1 evaluated
var log = ""
def fallback(): Int =
  log = "evaluated"
  0

@main def run(): Unit =
  val x = Some(1).getOrElse(fallback())
  println(x.toString + " " + log)
```

`03-scala-dev-d34-case-lambda-arity.scala` (Scala prints `3`)
```scala
// EXPECT-ERROR: wrong number of arguments
@main def run(): Unit =
  val add: (Int, Int) => Int = { case (a, b) => a + b }
  println(add(1, 2))
```

Run: `ctest --test-dir build_release -R conformance/tutorial --output-on-failure` → every new fixture PASSES (they exercise implemented features); the chapter text of Step 2 must quote them verbatim.

- [ ] **Step 2: Chapter 6 — `docs/tutorial/06-classes-objects-and-traits.md`**

Write the chapter with these sections (each snippet is the named fixture, verbatim, with its `Prints:` block):

1. Title `# 6. Classes, Objects and Traits`; implementation-status box: everything here runs, except classes/objects/traits nested in blocks or in other templates, anonymous classes (`new T { ... }`), multiple constructor parameter lists and `super[T]` (Phase 4); access modifiers are advisory except `private` (D5).
2. **6.1 Classes** — `06-classes-point.scala`. Constructor parameters: `val` (a public read-only field), `var` (a field with a setter), plain (private to the class). Methods, `override def toString`. For Python/JS readers: `class Point(val x: Int, var y: Int)` is `__init__`/`constructor` plus the field declarations in one line; `new` creates an instance (optional in Scala 3 for classes with a companion `apply`, see 6.3). For Scala readers: instances of a class without `var` fields are immutable protoCore objects, rebuilt field by field during construction (D28, §3.2).
3. **6.2 Mutable state** — `06-classes-counter.scala`. `var` fields, `c.count += 10` from outside (setters `count_=`), mutable instances keep their identity.
4. **6.3 Objects and companions** — `06-classes-objects.scala`. `object` is a lazily created singleton (first access); a companion object shares the class's name, can read its private members, and `Temp(21.0)` calls `Temp.apply` (the universal `apply` rule, DESIGN §5.1). Bridge: a Python module-level singleton / a JS object literal; `apply` ≈ `__call__`.
5. **6.4 Traits, abstract members and linearization** — `06-classes-abstract.scala`, then `06-classes-traits.scala`: explain `L(C) = C, T2, T1, B, A, AnyRef, Any`, that dispatch follows it and that `super.who` in a trait means "the next definition after this trait in the object's linearization" (stackable traits, DESIGN §4.4). Bridge: traits are mixins (Python multiple inheritance with a C3-like MRO; JS mixin functions), with Scala's right-to-left rule.
6. **6.5 Privacy** — `06-classes-private.scala`: the error is reported at run time, `NoSuchMethodError: value balance is not a member of Account` (D5; Scala reports it at compile time).
7. **6.6 `apply`, `update` and functions as objects** — `06-classes-apply.scala`: `p(1) = 7` is `p.update(1, 7)`, `p(0)` is `p.apply(0)`, and a lambda is an object whose `apply` runs it.
8. **6.7 For Scala developers** — a short list: D28 (construction), D30 (uninitialised fields), D31 (no overloading; constructors by arity), top-level templates only (Open question Q6), `==` on plain classes is identity unless `equals` is overridden (as in Scala), `hashCode` of plain objects is an identity hash.

- [ ] **Step 3: Chapter 7 — `docs/tutorial/07-case-classes-and-pattern-matching.md`**

Sections: status box (all of DESIGN §5.3 runs; `enum` and exhaustiveness are Phase 4 / D4); **7.1 Case classes** (`07-case-basics.scala`: structural `==`, `hashCode` equal to the JVM's, `toString`, `copy` with named arguments — the only named arguments before Phase 4, Open question Q9); **7.2 Tuples** (`07-case-tuples.scala`: tuples are case classes `Tuple2`..`Tuple22`, never protoCore tuples, DESIGN §4.6; `val (name, age) = person`); **7.3 Option** (`07-case-option.scala`: the prelude's `Option`, `Some`, `None`; D33); **7.4 Algebraic data types and `match`** (`07-case-adt.scala`: sealed traits, case-class patterns, the decision cascade, no exhaustiveness check — D4); **7.5 A tour of patterns** (`07-case-patterns.scala`: literals, typed patterns with guards, `::`, tuple and alternative patterns, wildcards; then a table of every pattern form with one line each, including binders `p @ ...`, stable identifiers and back-quoted names, `List(a, rest*)`); **7.6 Extractors** (`07-case-extractors.scala`: `unapply` returning `Option` or `Boolean`, Scala 3's protocol); **7.7 When nothing matches** (`07-case-match-error.scala`); **7.8 For Python/JS developers** (Python's `match` statement and JS `switch` compared: `match` is an expression, patterns destructure, guards); **7.9 For Scala developers** (D29 type tests, D32 tuple arity, D34 `{ case ... }` arity, `isInstanceOf`/`asInstanceOf` checked on the class only).

- [ ] **Step 4: Chapter 9 — `docs/tutorial/09-for-comprehensions.md`**

Sections: status box (`for` works over anything with `map`/`flatMap`/`withFilter`/`foreach`: `List`, `Option` and your classes; `Range` (`1 to 10`) arrives with the collections in Phase 3); **9.1 `yield`** (`09-for-yield.scala`, both the one-line and the indented forms); **9.2 `do`** (`09-for-do.scala`); **9.3 What a for-comprehension means** (`09-for-desugared.scala`: the rewrite to `flatMap`/`withFilter`/`map` of DESIGN §3.4, and that guards run lazily, interleaved with the body, as in Scala); **9.4 Patterns and value definitions** (`09-for-patterns.scala`: tuple destructuring, `case` filtering generators, `y = expr`); **9.5 Beyond lists** (`09-for-option.scala`); **9.6 Placeholders** (`09-for-placeholders.scala`: `_ + 1`); **9.7 Bridges** (Python list comprehensions and JS `flatMap`/`filter`/`map` chains, with `02-python-js-comprehension.scala` cross-referenced).

- [ ] **Step 5: Chapters 2, 3, 5, 14 and the index**

- `02-for-the-python-or-javascript-developer.md`: new sections **2.9 Classes and traits** (`02-python-js-classes.scala`), **2.10 Pattern matching** (`02-python-js-match.scala`), **2.11 Comprehensions** (`02-python-js-comprehension.scala`); renumber "Where to go next" to 2.12 and point it at chapters 6, 7 and 9.
- `03-for-the-scala-developer.md`: update the implementation-status box (classes, objects, traits, case classes, `match`, `for` run; still missing: collections beyond Phase 2's `List`, interpolation, exceptions, `enum`, extension methods, actors); add a subsection **Provisional deviations (Phase 2)** with D28–D34, each with its fixture (`03-scala-dev-d28-…` … `d34-…`) and one paragraph; extend D5 and D10 with their Phase 2 meaning (§ Task 17); update §3.3 "What is missing".
- `05-functions-and-closures.md`: the status box no longer says `map`/`filter` arrive in Phase 3 — `List` has `map`, `flatMap`, `filter`, `withFilter`, `foreach`, `tail`, `drop` and `::` since Phase 2 (chapter 9), the rest arrives in Phase 3.
- `14-repl-and-tooling.md`: a new section **14.8 Classes at the REPL** with a transcript (the first nine lines of `tests/cli/repl-classes.sh`'s input and their echoes); extend `tests/cli/tutorial-repl.sh` to replay those lines and check the echoes (`// defined case class Point`, `val p = Point(1,2)`, `val res… = Point(1,5)`, `// defined trait Shape`, `// defined class Sq`, `val res… = 9.0`).
- `docs/TUTORIAL.md`: link chapters 6, 7, 9; the status note becomes "What runs today (protoScala 0.2.0)" and lists classes, objects, traits, case classes, pattern matching and for-comprehensions; "How to use this tutorial" mentions chapters 6, 7, 9 for both audiences.

- [ ] **Step 6: Verify and commit**

Run: `ctest --test-dir build_release -R 'tutorial' --output-on-failure` → PASS. Check by eye that every snippet in the three chapters equals its fixture (`diff <(sed -n '/```scala/,/```/p' docs/tutorial/06-classes-objects-and-traits.md) ...` is optional; the fixture file is the source of truth).

```bash
git add docs/TUTORIAL.md docs/tutorial tests/conformance/tutorial tests/cli/tutorial-repl.sh
git commit -m "docs: tutorial chapters 6, 7 and 9; chapters 2, 3, 5 and 14 extended for Phase 2

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

---

### Task 17: Status documents, deviations, version 0.2.0

**Files:**
- Modify: `CMakeLists.txt` (version), `docs/STATUS.md`, `docs/LANGUAGE.md`, `docs/ROADMAP.md`, `docs/DECISIONS-LOG.md`, `CHANGELOG.md`, `README.md`

**Interfaces:**
- Consumes: Tasks 1–16.
- Produces: the Phase 2 release state (ROADMAP "Done when" satisfied and documented).

- [ ] **Step 1: Version**

`CMakeLists.txt`: `project(protoScala VERSION 0.2.0 LANGUAGES CXX)`. `ctest -R 'Smoke|cli/version'` → PASS.

- [ ] **Step 2: STATUS.md**

- Header: "Phase 2 complete (0.2.0)", the new test totals from `ctest --test-dir build_release -N` (unit / conformance / CLI / benchmark smoke) and the date; "All green, also under `PROTOCORE_HEAP_LIMIT_CELLS=20000`" only after running `PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release` and seeing it pass.
- **Implemented**: LANGUAGE §3 items delivered (classes with `val`/`var`/plain parameters, auxiliary constructors, `extends`/`with`, `override`/`abstract`/`final`/`sealed`/`open`, `private` as D5, objects, companions, case classes, case objects, traits with concrete and abstract members, trait parameters, `super.m` including stackable traits, `this`, `isInstanceOf`/`asInstanceOf`), tuples, `Option`, the Phase 2 `List` surface, `match` with every DESIGN §5.3 pattern, for-comprehensions, placeholder syntax, universal `apply`, `update`, setters, method values.
- **Not yet implemented**: nested/local/anonymous classes, multiple constructor parameter lists, `super[T]`, `enum`, extension methods, named/default arguments for Scala methods, the Phase 3 collections, interpolation, exceptions, actors, UMD.
- **Opcode table**: rows 64–78 from this plan's table; "78..95 reserved" becomes "79..95 reserved".
- **Provisional deviations (Phase 2)** — pending maintainer decision, each naming its plan question:

| Id | Deviation | Plan question |
|---|---|---|
| D28 | Instances of classes without `var` fields are immutable and rebuilt field by field during construction: a reference to `this` that escapes before the last field is initialised denotes an earlier version of the object (it lacks the later fields and is not `==`/`eq` to the result) | Q1 |
| D29 | Type tests follow the runtime representation: `Int`, `Long`, `Short`, `Byte` and `BigInt` are one integer type (D1), `Float` is `Double` (D2), so `3L.isInstanceOf[Int]` is `true`; `asInstanceOf` never converts numbers (`(1: Any).asInstanceOf[Double]` throws); `null.asInstanceOf[Int]` is `null`; an extractor's `unapply` is called without the type test its parameter type implies | Q10 |
| D30 | Reading a field of the instance under construction before its initialiser has run raises `NoSuchMethodError` (Scala reads the default value `0`/`null`) | Q11 |
| D31 | Methods cannot be overloaded (a second definition of a name in one template is an error); constructors may be overloaded by number of parameters only | Q11 |
| D32 | Tuples have at most 22 elements (Scala 3 has `TupleXXL`) | — (Scala 3.9 behaviour differs only above 22) |
| D33 | `Option.getOrElse` evaluates its default eagerly (by-name parameters are not supported yet) | Q7 |
| D34 | A pattern-matching function literal `{ case ... }` takes exactly one argument (Scala 3 also accepts it where a function of several parameters is expected) | Q12 |

Extend **D5** ("Access modifiers advisory except `private`: a private member is stored under a class-qualified key, reachable only from code of its class and companion; an access from elsewhere fails at run time with `NoSuchMethodError`; `protected`, `override` and member `final` are not checked") and **D10** ("… also `obj.m` for a method with parameters is a function value (eta-expansion), and `obj.m` calls `def m()`").
- **Known issues / platform dependencies**: R6 now used (`super` walks the linearization per call); a new note on protoCore `isInstanceOf`'s traversal limits (Design note 5; Open question Q2) and that protoScala uses marker attributes instead; List `hashCode` values differ from the JVM's (consistent with `==`).

- [ ] **Step 3: LANGUAGE.md, ROADMAP.md, DECISIONS-LOG.md**

- LANGUAGE §2: placeholder syntax, for-comprehensions and `match` delivered in Phase 2; §3: mark what Phase 2 delivered and move "nested/local/anonymous classes" to a later phase (per the maintainer's answer to Q6; until then "not supported"); §5: add D28–D34 rows and the D5/D10 extensions (identical text to STATUS).
- ROADMAP: `## Phase 2 — Object model, apply, for, match ✅ (<date>)` with the plan link; under Phase 4 note that `super` in stackable traits already works (Open question Q5) and that `super[T].m` remains.
- DECISIONS-LOG: one row — "Phase 2 plan Q1–Q14: the plan's provisional behaviours adopted as written; D28–D34 recorded as provisional | agent, pending review | plans/2026-09-22-phase-2-object-model.md" — plus a row per question the maintainer has already answered by the time this task runs (with "maintainer" as the author).

- [ ] **Step 4: CHANGELOG.md and README.md**

`CHANGELOG.md` — a new section above `[0.1.0]`:

```markdown
## [0.2.0] - <date of this commit, date +%F>

Phase 2: object model, apply, for, match. Built against protoCore `<hash from Preflight step 2>`.

### Added

- Classes: `val`/`var`/plain constructor parameters, fields, methods, auxiliary constructors,
  `extends`/`with`, abstract members, `override`, `final`, `sealed`; `private` enforced as a
  lookup restriction (D5).
- Traits with Scala's linearization, installed as protoCore parent chains (DESIGN §4.3), trait
  parameters, `super` calls including stackable traits (DESIGN §4.4).
- Objects (lazy singletons), companions, case classes and case objects with `apply`, `unapply`,
  `equals`, `hashCode` (equal to the JVM's), `toString`, `copy` (positional and named),
  `productArity`, `productElement`, `productPrefix`, `_1`..`_N`.
- Tuples `Tuple2`..`Tuple22` as case classes (never protoCore tuples, DESIGN §4.6).
- The universal `apply` rule, `update`, setters, method values.
- Pattern matching: literals, wildcards, variables, typed patterns, constructor and tuple
  patterns, `::`, `List(a, rest*)`, alternatives, binders, stable identifiers, custom
  extractors, guards, `MatchError`; pattern `val`s; `{ case ... }` literals;
  `isInstanceOf`/`asInstanceOf`.
- For-comprehensions (generators, guards, value definitions, patterns; `yield` and `do`) over
  `List`, `Option` and any class with `map`/`flatMap`/`withFilter`/`foreach`; lazy `withFilter`.
- Placeholder syntax (`_ + 1`).
- The prelude (`lib/prelude.scala`): `Option`, `Some`, `None`.
- `List(...)`, `Nil`, `::`, `map`, `flatMap`, `filter`, `withFilter`, `tail`, `drop`.
- REPL: class, trait, object and case-class definitions, redefinition by shadowing.
- Benchmarks: `attr_lookup` (twin of protoPython/protoST) and `object_tree` (a deep immutable
  object graph); GC-pressure checks for object graphs.
- Tutorial chapters 6, 7 and 9; chapters 2, 3, 5 and 14 extended.

### Deviations (provisional, pending maintainer review)

- D28–D34 (STATUS.md); D5 and D10 extended.
```

`README.md`: the status line ("Phase 2 complete (version 0.2.0)…"), the feature list, the "Performance" section (add the Phase 2 report's rows for `attr_lookup` and `object_tree` next to the existing table, with the report link; remove `attr_lookup` from the pending list).

- [ ] **Step 5: Full verification**

Run, from `protoScala/`:

```bash
cmake -B build_release -S . && cmake --build build_release 2>&1 | grep -iE 'warning|error' ; echo "build: $?"
ctest --test-dir build_release
PROTOCORE_HEAP_LIMIT_CELLS=20000 ctest --test-dir build_release
benchmarks/cold-start.sh build_bench/protoscala 21
```

Expected: no warning lines (the `grep` prints nothing), `100% tests passed` twice, both cold-start medians below 20 ms in the Release build. Update the counts in STATUS.md/README.md with the numbers printed. If anything fails, fix it (it is a Phase 2 defect) and re-run everything.

- [ ] **Step 6: Commit and push**

```bash
git add CMakeLists.txt docs CHANGELOG.md README.md
git commit -m "Release 0.2.0: Phase 2 complete (object model, apply, for, match)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git push origin main
```

Tagging `v0.2.0` is the maintainer's decision; do not create the tag.

---

## Self-review against DESIGN.md and ROADMAP Phase 2

ROADMAP Phase 2 "Done when":

| Criterion | Where it is satisfied |
|---|---|
| Classes | Tasks 1, 7: parser, `ClassInfo`, `MAKE_CLASS`/`NEW`/constructors/setters; fixtures `07-classes/*`, `09-apply/*` |
| Objects, companions | Task 7 (`compileObjectHolder`, `linkCompanions`, private access through companions); fixtures `object-singleton-*`, `companion-apply`, `class-private-companion-access` |
| Traits with Scala linearization, unit-tested against `scalac`-documented examples | Task 4 `test_linearizer.cpp` (SLS 5.1.2 `Iter`, *Programming in Scala* `Cat` and stackable queue, DESIGN §4.3, diamonds, deep chains); Task 5 `ObjectModel.*` pins the protoCore chain facts; Task 7 fixtures `trait-linearization-*`, `trait-init-order`, `stackable-traits` with the outputs `scalac` 3.9.0 printed |
| Case classes with all synthesised members | Task 3 (companion `apply`/`unapply`), Task 8 (`toString`, `equals`, `hashCode` bit-identical to the JVM's, `copy` positional and named, `productArity`, `productElement`, `productPrefix`, `_1`..`_N`), case objects; fixtures `08-case-classes/*` |
| Universal `apply` | Task 6 (`callMember`, bound methods), Task 7 (`C(args)`), Task 9 fixtures |
| For-comprehensions over `List` | Task 2 (parser, three syntaxes), Task 3 (rewrite), Task 10 (`List` methods, lazy `withFilter`), Task 13 fixtures (including `Option` and user classes) |
| Pattern matching, all DESIGN §5.3 patterns | Task 12: literals, wildcards, variables, typed, constructor, tuples, `::`, alternatives, binders, sequence patterns with `_*`, guards, custom extractors, `MatchError`; fixtures `11-pattern-matching/*` |
| Fixtures pass, STATUS updated, tutorial chapters, suite green | Tasks 16–17 |

DESIGN coverage: §1 positioning (no performance work beyond what the design implies; `object_tree` measures the deep-object-graph workload; cold start re-checked with the prelude, Task 11); §1.1 P1 (rooting rules in every allocating helper, GC-pressure fixtures in Tasks 7, 10, 12, 15), P2 (child contexts per helper and per list element), P3/P5 (no protoCore change; the `isInstanceOf` limitation raised as Q2), P4 (D28–D34), P6 (immutable class prototypes, Design note 2); §3.4 desugar rows for `for`, `apply`, `a(i) = v`, `x op= y`, `match` (Tasks 3, 12); §3.5 opcodes `SEND_SUPER`, `TEST_TYPE`, `TEST_PROTO`, `UNAPPLY_FIELDS` in the reserved range, mirrored in STATUS (Tasks 5, 17); §4.2 classes as prototypes, instances by `newChild`, mutable when a `var` exists, methods on the prototype with `getAttribute` dispatch, `object` as a lazy singleton in the globals, companions (the `__companion__` attribute is replaced by a compile-time link, Q3); §4.3 frontend linearization installed with `setParents`, parent chains frozen at creation; §4.4 `super` by identity walk of `getParents` (Q5); §4.5 case-class members, case objects with identity equality; §4.6 tuples as case classes; §5.1 universal `apply` (compiled Scala methods short-circuit in the VM, native ones through `callNative`); §5.2 named arguments for native methods through the keyword `ProtoSparseList` (Q9); §5.3 the cascade; §6 `List` = raw `ProtoList`, `::` prepends and `h :: t` extracts `getAt(0)` and the tail slice; §10 testing (unit, conformance with brace/indent variants, CLI, GC pressure, self-reporting benchmarks, TDD order in every task); §10.1 documentation (Task 16).

Placeholder scan: every code step shows code; every fixture has its full content and expected line; the only values left to the executor are measurements, dates, commit hashes and test totals, which cannot be known in advance.

Type consistency: `Modifiers`, `TemplateKind`, `ParentRef`, `TemplateDef`, `New` (T1); `Pattern`, `CaseDef`, `Match`, `Enumerator`, `For`, `clonePattern`, `cloneSimpleExpr` (T2); `cloneType`, `patternVariables` (T3, moved to AST in T12); `linearize`, `ClassKind`, `MemberKind`, `MemberInfo`, `ClassInfo`, `builtinTypes`, `kAnyKey`/`kAnyRefKey`/`kProductKey`/`kSerializableKey`/`kPrimaryCtorKey`/`kMaxTupleArity`, `privateKey`/`setterName`/`auxCtorKey`/`tupleTypeKey`, `GlobalTable::declareType/defineType/defineBuiltinType/findType/findTypeByKey/mutableTypeByKey`, `BindingKind::Object` (T4); `Op::MAKE_CLASS..SEND_KW`, `TypeCode`, `BytecodeModule::ConstKind::{Names,ClassSpec,SuperSite,KwSendSite}`, `ClassSpecData`, `ClassFlag`, `isMethod/setMethod`, `RuntimeLayout` additions, `hashing::*` (T5); `ExecutionEngine::construct/showTopLevel/dispatch/callMember/callWithReceiver/bindMethod/forceMember/instantiate/makeClass/makeTuple/superSend/sendKeywords/testType/throwMissingMember`, `isObjectCellFast`, `isScalaInstance`, `defaultToString`, `identityHash`, `scalaHash`, `EvalHarness::runModule` (T6); `Compiler::FnShape`, `RefKind::Member`, `TemplateScope`, `memberOf`, `selectKey`, `loadThis`, `sortTemplates`, `buildClassInfo`, `linkCompanions`, `resolveType`, `superclassOf`, `runtimeChain`, `ctorKeyFor`, `compileTemplate/Constructor/AuxConstructor/Setter/ObjectHolder/InitCall/New/NewOf/SuperSend/NamedSend/Stats`, `Op::NEW_SPREAD` (T7); `compileTuple`, `installProductPrimitives`, `PrimitiveSupport.h` (T8); `ListBuilder`, `callOne` (T10); `preludeSource`, `loadPrelude` (T11); `compileMatch/Pattern/Extractor/ListPattern`, `extractInto`, `emitProtoTest`, `bindPattern`, `checkNoVariables`, `compileTypeTest`, `compileInstanceOf` (T12).

Conformance directories added: `07-classes`, `08-case-classes`, `09-apply`, `10-lists`, `11-pattern-matching`, `12-for-comprehensions`, plus the tutorial fixtures.

---

## Open questions for the maintainer

Each question states the provisional behaviour this plan implements (execution is not blocked; every choice is reversible) and a recommendation. Provisional deviations are recorded in STATUS.md by Task 17.

- **Q1 — Construction of immutable instances (D28).** DESIGN §4.2 makes instances of classes without `var` fields immutable, so each field store during construction yields a new version of the object; a `this` that escapes before the last field (registered in a global, captured by a lambda in the class body, passed to a method that stores it) is an earlier, incomplete version. Options: (a) as described (provisional); (b) create every instance mutable and never freeze it (costs a mutables-tree entry per instance, P6 snapshot size); (c) make an instance mutable only when the compiler sees `this` escape from the constructor (a conservative syntactic analysis: `this` used other than as a receiver of field reads before the last field). *Recommendation:* (a) now; (c) if real programs hit D28.
- **Q2 — Class-membership tests use marker attributes, not protoCore `isInstanceOf`.** DESIGN §5.3 names `isInstanceOf`; protoCore's implementation stops after 50 visited objects and keeps 64 pending siblings (`core/ProtoObject.cpp:437-525`), which gives false negatives with flattened chains of about ten ancestors. Options: (a) a per-class marker attribute keyed by the type key, found with the cached `getAttribute` walk (provisional; exact, allocation-free, bounded by R3); (b) a new additive protoCore API that walks the receiver's own flattened chain (e.g. `ProtoObject::hasInChain`) — P3-compliant, needs a protoCore release and embedder rebuilds; (c) `getParents` + scan (allocates per test). *Recommendation:* (a) now, (b) as a platform item; also decide whether protoCore's `isInstanceOf` limits are a defect to fix for the other embedders.
- **Q3 — Class prototypes are immutable; companions are linked at compile time.** DESIGN §4.2 mentions a `__companion__` attribute; a two-way link between two immutable objects is impossible, and nothing at run time needs it (companion resolution happens in the compiler's type namespace). *Recommendation:* keep class prototypes immutable (Design note 2) and drop the runtime link; add a one-way `__companion__` on the object's class later if interop needs it.
- **Q4 — `private` by class-qualified member keys (refines D5).** Options: (a) mangled keys `Class::name`, no run-time checks (provisional); (b) run-time access checks on every send (cost on every dispatch); (c) advisory `private`. *Recommendation:* (a).
- **Q5 — `super` in Phase 2.** ROADMAP puts stackable traits in Phase 4, but a plain `super.m` in a class that overrides `m` is common and is the same DESIGN §4.4 algorithm. *Provisional:* `SEND_SUPER` in Phase 2 (stackable traits therefore work already, fixture `stackable-traits.scala`); `super[T].m` stays in Phase 4. *Recommendation:* accept; Phase 4 keeps `super[T]` and its done-when fixture.
- **Q6 — Where templates may appear.** *Provisional:* classes, traits and objects only at the top level of a file (no local classes in blocks, no classes nested in objects or classes, no anonymous classes `new T { ... }`), because a local class closes over locals and its methods would need captures. *Recommendation:* add nested templates in objects (common for ADTs, no captures needed) in Phase 4 together with `enum`; local and anonymous classes after that.
- **Q7 — A Phase 2 prelude written in protoScala.** `Some`/`None` are needed for idiomatic extractors and for-comprehensions, so `Option` moves ahead of Phase 3 as `lib/prelude.scala`, embedded in the binary and compiled at every start (cold start re-measured in Task 11); an internal global `__raise` stands in for `throw`; `getOrElse` is strict (D33). Options for start-up cost if needed: a lazily compiled prelude, or a serialized/precompiled one. *Recommendation:* accept; decide on precompilation only if the < 20 ms budget is threatened.
- **Q8 — A minimal `List` in Phase 2.** For-comprehensions over `List` and the `::`/`List(...)` patterns need `List(...)`, `Nil`, `::`, `map`, `flatMap`, `filter`, `withFilter`, `tail`, `drop`; the rest of `List` stays in Phase 3. `withFilter` is lazy (Scala's `WithFilter`), so guards and bodies interleave as on the JVM. *Recommendation:* accept.
- **Q9 — Named arguments only for native methods in Phase 2.** `SEND_KW` passes protoCore's keyword `ProtoSparseList` (DESIGN §5.2) to native methods, which makes `copy(y = 5)` work; a named argument to a Scala method raises `UnsupportedOperationException` until Phase 4. *Recommendation:* accept.
- **Q10 — Type tests follow the runtime representation (D29).** With D1/D2 there is one integer and one floating type, so `3L.isInstanceOf[Int]` is `true` (JVM: `false`), `asInstanceOf` never converts, `null.asInstanceOf[Int]` is `null`, and an extractor's parameter type is not tested before `unapply` runs. *Recommendation:* accept as a permanent consequence of D1/D2/D4.
- **Q11 — Uninitialised fields and overloading (D30, D31).** Reading a field before its initialiser raises `NoSuchMethodError` (Scala: `0`/`null`); methods cannot be overloaded, constructors only by arity. Options for D30: pre-set declared fields to `null` on the class prototype (Scala-like for references, not for `Int`). *Recommendation:* keep D30 (it surfaces an initialisation-order bug instead of a silent `null`); D31 permanent while types are erased.
- **Q12 — Lazy members, method values, `{ case ... }` (D10 extended, D34).** A `lazy val` member is a per-instance holder whose thunk is a method run with the receiver of the first access; `obj.m` for a method with parameters is a bound function (eta-expansion) and `obj.m` calls `def m()`; `{ case ... }` literals take one argument. *Recommendation:* accept.
- **Q13 — REPL redefinition of classes.** *Provisional:* a redefinition gets a fresh type key (`@C#1`), old instances keep the old class, a class and its companion must be defined in the same input (the Scala REPL's rule), echoes use the Scala 3 REPL's `// defined class C` wording. *Recommendation:* accept.
- **Q14 — Tutorial numbering.** The Phase 2 brief names chapter 8 for for-comprehensions; `docs/TUTORIAL.md` assigns chapter 8 to collections (Phase 3) and chapter 9 to for-comprehensions (Phase 2). *Provisional:* follow TUTORIAL.md (chapters 6, 7, 9). *Recommendation:* keep the published index; renumber only if the maintainer prefers chapter 8.

Recorded in STATUS.md without a question (small consequences of the choices above): D32 (tuples ≤ 22 elements); `List` hash codes differ from the JVM's while staying consistent with `==` (case classes, tuples and strings are bit-identical); the default `toString` of plain objects is `Name@<identity hash>` (objects print `O@…`, the JVM `O$@…`); refutable generator patterns are accepted with or without Scala 3.9's `case` keyword and are filtered with `withFilter`.
