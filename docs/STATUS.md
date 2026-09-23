# protoScala Status

> Living tracker of the gap between [LANGUAGE.md](LANGUAGE.md) and the
> implementation. Update it with every change.
>
> **Current state (2026-09-23):** Phase 2 complete (0.2.0): everything Phase 1
> delivered, plus classes, objects, companions, traits with Scala's
> linearization, case classes and case objects, tuples, the universal `apply`
> rule, `match` with every DESIGN §5.3 pattern, for-comprehensions, placeholder
> syntax, an `Option` prelude written in protoScala and a minimal `List`.
> **Tests:** 646 total (`ctest --test-dir build_release -N`) — 265 unit
> (GoogleTest), 352 conformance fixtures, 19 CLI checks, 10 benchmark smoke
> checks. All green, also under `PROTOCORE_HEAP_LIMIT_CELLS=20000` (the whole
> suite, unfiltered). Last verified 2026-09-23.

## Implemented

Per [LANGUAGE.md](LANGUAGE.md) §1–§2, the rows delivered in Phase 1:

- [x] Identifiers: alphanumeric, operator, mixed, backquoted.
- [x] Hard and soft keywords (§1's full lists, including `this`).
- [x] Literals: decimal/hex/binary integers with `_` and `L`; floating point;
      characters with escapes and `\u`; strings, triple-quoted strings;
      `s`/`f`/`raw` interpolators lexed as structured tokens (execution
      arrives in Phase 3).
- [x] Comments: `//` line, nested `/* ... */` block.
- [x] Significant indentation (offside rule) and braces, mixable; `end`
      markers.
- [x] `val`, `var`, `lazy val`, `def` (multiple parameter lists, varargs
      `xs: Int*`); default and named arguments are Phase 4.
- [x] `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return`
      (methods only — D11).
- [x] Lambdas `x => e`, `(x, y) => e`.
- [x] Infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application.
- [x] `import` (parsed, ignored until UMD in Phase 6).
- [x] Top-level definitions (no wrapping `object`), `@main` methods.
- [x] Closures with per-activation captures; recursion, including deep and
      mutual recursion, with `StackOverflowError` instead of a crash.
- [x] `println`, `print`, and the methods of `Int`, `Double`, `Boolean`,
      `Char`, `String`, `List` (varargs) and functions (DESIGN §5.1 universal
      `apply`).
- [x] REPL: readline history, multi-line continuation (braces and
      line-by-line indented input), shadowing redefinitions (D25), `:help`,
      `:quit`, `:load`.
- [x] Value discarding for `Unit` (D26), `if` without `else` yields `()`,
      Scala's block forward-reference rule, `return` in curried methods.
- [x] `--disassemble`; conformance, unit and CLI test suites; tutorial
      chapters 1–5 and 14.

Per LANGUAGE.md §2–§3, the rows delivered in Phase 2:

- [x] `class` with `val`, `var` and plain constructor parameters, fields,
      methods, auxiliary constructors `def this(...)`, `extends` / `with`,
      abstract members, `override`, `abstract`, `final`, `sealed`, `open`
      (accepted); `private` enforced as a lookup restriction (D5).
- [x] `object` (a lazily initialised singleton), companion objects (linked at
      compile time, DESIGN §4.2), `case class`, `case object`.
- [x] `trait` with concrete and abstract members and trait parameters;
      Scala's linearization installed as a protoCore parent chain
      (DESIGN §4.3).
- [x] `super.m`, including stackable traits (DESIGN §4.4). `super[T].m` is
      Phase 4.
- [x] `this`, self-type aliases (`self =>`), `isInstanceOf[T]`,
      `asInstanceOf[T]` (D29).
- [x] Case-class members: `apply`, `unapply`, `equals`, `hashCode` (bit-equal
      to the JVM's), `toString`, `copy` (positional and named),
      `canEqual`, `productArity`, `productElement`, `productPrefix`,
      `_1`..`_N`.
- [x] Tuples `Tuple2`..`Tuple22` as case classes, never protoCore tuples
      (DESIGN §4.6, R2).
- [x] The universal `apply` rule for a receiver that *has* an `apply`
      member — `C(args)` where `C` is a case class or an `object`/companion
      with an `apply`, `obj(args)`, `f(args)` — plus `update` (`a(i) = v`),
      generated setters (`c.value = v`, `c.value += 3`) and method values
      (eta-expansion, D10). `C(args)` on a **plain** class with no companion
      `apply` is not rewritten to `new C(args)`: D41.
- [x] `match` with every DESIGN §5.3 pattern: literals, wildcards, variables,
      typed patterns, constructor and tuple patterns, `::`,
      `List(a, rest*)`, alternatives `|`, binders `x @ p`, stable
      identifiers, custom extractors, guards, `MatchError`; pattern `val`s;
      `{ case ... }` function literals (D34).
- [x] For-comprehensions: generators, guards, value definitions and patterns,
      `yield` and `do`, in all three syntaxes (parentheses, braces,
      indentation), over `List`, `Option` and any class supplying
      `map`/`flatMap`/`withFilter`/`foreach`; `withFilter` is lazy.
- [x] Placeholder syntax (`_ + 1`).
- [x] The prelude (`lib/prelude.scala`, embedded in the binary): `Option`,
      `Some`, `None`.
- [x] `List(...)`, `Nil`, `::`, `map`, `flatMap`, `filter`, `withFilter`,
      `foreach`, `length`, `tail`, `drop`, `mkString`.
- [x] REPL: class, trait, object and case-class definitions, redefinition by
      shadowing (Q13).
- [x] Benchmarks `attr_lookup` and `object_tree`; GC-pressure checks for
      object graphs; tutorial chapters 6, 7 and 9 (chapters 2, 3, 5 and 14
      extended).

## Not yet implemented

- Nested, local and anonymous classes (`new T { ... }`) — Q6; nested
  templates inside objects arrive in Phase 4 with `enum`.
- Multiple constructor parameter lists.
- `super[T].m` — Phase 4.
- `enum`, extension methods, named and default arguments for Scala-defined
  methods (native methods already accept them, Q9) — Phase 4.
- Exceptions (`try`/`catch`/`finally`/`throw`) — Phase 4.
- The Phase 3 collections (`Vector`, `Map`, `Set`, `Range`, `Either`, `Try`,
  the full `List` surface) and string-interpolation execution — Phase 3.
- Actors and futures — Phase 5. UMD and packaging — Phase 6.

See [ROADMAP.md](ROADMAP.md).

## Opcode table (mirrors `src/compiler/Opcodes.h`, DESIGN §3.5)

One 32-bit word per instruction: opcode in the low 8 bits, unsigned 24-bit
operand in the high bits. `EXTEND` supplies bits 24..47 of the next
instruction's operand. The numbering is fixed; later phases only append in
their reserved ranges.

| # | Name | Stack effect | Notes |
|---|---|---|---|
| 0 | `NOP` | `[] -> []` | |
| 1 | `EXTEND` | — | operand: high 24 bits of the next instruction's operand |
| 2 | `PUSH_CONST` | `[] -> [consts[operand]]` | |
| 3 | `PUSH_UNIT` | `[] -> [()]` | |
| 4 | `PUSH_NULL` | `[] -> [null]` | |
| 5 | `PUSH_TRUE` | `[] -> [true]` | |
| 6 | `PUSH_FALSE` | `[] -> [false]` | |
| 7 | `POP` | `[v] -> []` | |
| 8 | `DUP` | `[v] -> [v v]` | |
| 9 | `PUSH_LOCAL` | `[] -> [slot[operand]]` | |
| 10 | `STORE_LOCAL` | `[v] -> []` | `slot[operand] = v` |
| 11 | `MAKE_CELL` | `[] -> []` | `slot[operand] = new Cell(null)` |
| 12 | `PUSH_CELL` | `[] -> [slot[operand].value]` | |
| 13 | `STORE_CELL` | `[v] -> []` | `slot[operand].value = v` |
| 14 | `PUSH_GLOBAL` | `[] -> [globals.name]` | operand: a Symbol constant |
| 15 | `STORE_GLOBAL` | `[v] -> []` | operand: a Symbol constant |
| 16 | `MAKE_FN` | `[c1..cn] -> [fn]` | operand: block index; n = its captureCount |
| 17 | `CALL` | `[f a1..an] -> [r]` | operand: n |
| 18 | `CALL_SPREAD` | `[f a1..an list] -> [r]` | operand: n; list elements follow a1..an |
| 19 | `SEND` | `[recv a1..an] -> [r]` | operand: SendSite constant (name, n) |
| 20 | `RETURN` | `[v] -> returns v` | |
| 21 | `MAKE_LAZY` | `[thunk] -> [lazy]` | |
| 22 | `FORCE` | `[v] -> [forced v]` | evaluates a lazy once; other values pass |
| 23 | `JUMP` | control flow | offset in words, from the next instruction |
| 24 | `JUMP_IF_FALSE` | `[b] -> []` | b must be a Boolean |
| 25 | `JUMP_IF_TRUE` | `[b] -> []` | |
| 26 | `JUMP_BACK` | control flow | backward; also a GC safepoint (D-none; Q21, R1) |
| 27 | `ADD` | `[a b] -> [r]` | SmallInteger fast path, protoCore fallback |
| 28 | `SUB` | `[a b] -> [r]` | |
| 29 | `MUL` | `[a b] -> [r]` | |
| 30 | `LT` | `[a b] -> [Boolean]` | |
| 31 | `LE` | `[a b] -> [Boolean]` | |
| 32 | `GT` | `[a b] -> [Boolean]` | |
| 33 | `GE` | `[a b] -> [Boolean]` | |
| 34 | `EQ` | `[a b] -> [Boolean]` | Scala `==` |
| 35 | `NE` | `[a b] -> [Boolean]` | Scala `!=` |
| 36 | `NEG` | `[a] -> [-a]` | |
| 37 | `NOT` | `[b] -> [!b]` | |
| 38..63 | reserved | | Phase 1 additions |
| 64 | `MAKE_CLASS` | `[p1..pk m1..mn] -> [cls]` | operand: a ClassSpec constant |
| 65 | `NEW` | `[cls a1..an] -> [obj]` | operand: SendSite (constructor key, n) |
| 66 | `INVOKE_INIT` | `[cls this a1..an] -> [this']` | operand: SendSite (constructor key, n); returns the rebuilt instance (D28) |
| 67 | `STORE_FIELD` | `[v] -> []` | `slot[0] = slot[0].setAttribute(key, v)`; operand: Symbol |
| 68 | `SET_FIELD` | `[obj v] -> []` | the receiver must be mutable; operand: Symbol |
| 69 | `SEND_SUPER` | `[this a1..an] -> [r]` | operand: a SuperSite constant (DESIGN §4.4) |
| 70 | `TEST_TYPE` | `[v] -> [Boolean]` | operand: TypeCode (the built-in types, D29) |
| 71 | `TEST_PROTO` | `[v] -> [Boolean]` | operand: Symbol (the class's marker key, DESIGN §5.3) |
| 72 | `UNAPPLY_FIELDS` | `[v] -> [f1..fn]` | operand: a Names constant (attribute keys) |
| 73 | `UNCONS` | `[list] -> [head tail]` | the list must be non-empty |
| 74 | `MATCH_ERROR` | `[v] -> throws MatchError` | |
| 75 | `CAST_FAIL` | `[v] -> throws ClassCastException` | operand: String constant (the type name) |
| 76 | `MAKE_TUPLE` | `[a1..an] -> [tuple]` | operand: n (2..22) |
| 77 | `SEND_KW` | `[recv a1..an v1..vm] -> [r]` | operand: a KwSendSite constant (named arguments to native methods, Q9) |
| 78 | `NEW_SPREAD` | `[cls a1..an list] -> [obj]` | operand: SendSite (constructor key, n); list elements follow a1..an |
| 79 | `SEND_APPLY` | `[recv a1..an] -> [r]` | operand: SendSite (name, n); `recv.m(args)` written with an argument list: calls the member when it is a method, else applies its value (D10) |
| 80..95 | reserved | | object model |
| 96..127 | reserved | | exceptions, Phase 4: `THROW` (+ per-module handler table) |
| 128..159 | reserved | | actors, Phase 5: `SEND_ASYNC`, `ASK`, `AWAIT` |

## Intentional deviations

| Id | Deviation | Track |
|---|---|---|
| D1 | `Int`/`Long` promote to arbitrary precision instead of wrapping; integer literals are not range-checked either: `0xFFFFFFFF` is `4294967295` (Scala: `-1`), and `2147483648` or `99999999999999999999` are accepted without `L` (Scala: a compile error) | (perm) |
| D2 | `Float` is `Double` | (perm) |
| D3 | No implicits / givens resolution | later |
| D4 | No static type checking or exhaustiveness checks | (perm) |
| D5 | Access modifiers advisory except `private`: a private member is stored under a class-qualified key, reachable only from code of its class and companion; an access from elsewhere fails at run time with `NoSuchMethodError`; `protected`, `override` and member `final` are not checked. The "weaker access privileges" check that rejects a `private` member implementing an abstract one applies only to a `private` the user wrote, never to a synthesised member | (perm) |
| D6 | Extension methods dispatch on runtime prototype | (perm) |
| D7 | `Map`/`Set` iteration order unspecified | (perm) |
| D8 | No Java interop | (perm) |

### Provisional deviations (Phase 1) — pending maintainer decision

Each entry names the plan question it answers
([plans/2026-09-22-phase-1-core-language.md](plans/2026-09-22-phase-1-core-language.md),
"Open questions for the maintainer").

| Id | Deviation | Plan question |
|---|---|---|
| D9 | Script mode: top-level statements run in order; top-level vals are initialised eagerly before `@main` (Scala 3 initialises a file's top-level definitions on first access) | Q2 |
| D10 | A bare reference to `def f()` (empty parameter list) evaluates to the function (Scala 3 requires `f()` unless a function type is expected). Also: `obj.m` for a method **with** parameters is a function value (eta-expansion), and `obj.m()` on a parameterless `def m` whose result is a function applies that result (`obj.m.apply()`), as Scala does | Q3, Q12 |
| D11 | `return` inside a lambda (non-local return) is rejected | Q5 |
| D12 | Varargs arrive as a `List` (`toString` prints `List(...)`, Scala prints `ArraySeq(...)`) | Q15 |
| D13 | String length and indices count code points, not UTF-16 units; case mapping (`toUpperCase`/`toLowerCase`, `Char.toUpper`/`toLower`) is ASCII-only | Q11 |
| D14 | Runtime errors use unqualified class names (`ArithmeticException`, no `java.lang.`) and the format `file:line: error: Class: message`; arity mismatches raise `IllegalArgumentException` | Q9 |
| D15 | `>>>` raises `UnsupportedOperationException` (integers have no fixed width, D1) | Q12 |
| D16 | Indentation mixing tabs and spaces is rejected | Q13 |
| D17 | Inside `(...)`, a multi-statement lambda body needs braces (no indentation region inside parentheses) | Q1 |
| D18 | A `:` at the end of a line always opens an indentation region (`val x:` + newline + type is rejected) | Q14 |

The following were identified while implementing Phase 1 and were not covered
by a numbered plan question; they follow the same rule (small implementation
detail, not silently decided — recorded here for maintainer review):

| Id | Deviation | Where |
|---|---|---|
| D19 | A negative (or overflowing) shift amount to `<<`/`>>` raises `IllegalArgumentException` rather than being masked to a word width (consistent with D1: no fixed integer width) | `Primitives.cpp::shiftAmount` |
| D20 | `Int.toChar` outside the Unicode code-point range (`< 0` or `> 0x10FFFF`) raises `IllegalArgumentException` | `Primitives.cpp::int_toChar` |
| D21 | `Double.toInt`/`toLong`/`round` of `+Infinity`/`-Infinity` raise `ArithmeticException` (`NaN` converts to `0`, matching the JVM) | `Primitives.cpp::integralToInt` |
| D22 | `Char` predicates (`isDigit`, `isLetter`, `isUpper`, `isLower`, `isWhitespace`) test the ASCII range only, consistently with the ASCII-only case mapping of D13 | `Primitives.cpp` (`asciiDigit`/`asciiUpper`/`asciiLower`) |
| D23 | `case` clauses at the same indentation as their enclosing `match` are not supported; a `case` must be indented deeper than `match` (Scala 3 special-cases the equal-indentation form) | `Layout.cpp` (`lineBreak`: a region only opens on `w > width`) |
| D24 | An operator identifier whose first character is a non-ASCII (Unicode, byte `>= 0x80`) code point gets letter precedence (the lowest, as for `unary_-`-style word operators), rather than a precedence derived from Unicode operator-symbol classification | `Parser.cpp::precedence` (`isLetterStart`) |
| D25 | Within one file (or one REPL input), a repeated top-level definition replaces the earlier one (Scala 3 rejects duplicate top-level definitions); a duplicate name inside a block is a compile error. Across REPL inputs a redefinition shadows, as in the Scala REPL: each definition gets its own global key (`x`, then `x#1`, ...) resolved at compile time, so earlier code keeps the binding it saw, a change of kind (`def` → `val`, `val` → `lazy val`) never breaks it, and an input that fails at compile or run time defines nothing. A class redefined in the REPL gets a fresh type key (`@C#1`): old instances keep the old class, and a class and its companion must be defined in the same input (the Scala REPL's rule) | `GlobalTable.h`, `Compiler.cpp::compileUnit`, `Session.cpp::evaluate` |
| D26 | Value discarding (`Unit` expected type) applies only where `Unit` is written on the definition (`def f(): Unit`, `return` in it, `val v: Unit`, `(e: Unit)`, `if` without `else`); an expected type `Unit` that comes from a function type (`val f: Int => Unit = x => x + 1`, a lambda passed to an `A => Unit` parameter) does not discard, so the lambda returns its last value (D4: no type checking) | `Desugar.cpp::discardValue` |
| D27 | Typed `@main` parameters (`@main def m(n: Int, s: String)`, parsed from the command line in Scala 3 through `FromString`) are rejected; an `@main` method takes no parameters or one `String*` parameter | `Compiler.cpp::compileUnit` |

### Provisional deviations (Phase 2) — pending maintainer decision

Each entry names the plan question it answers
([plans/2026-09-22-phase-2-object-model.md](plans/2026-09-22-phase-2-object-model.md),
"Open questions for the maintainer"); `—` means the deviation was collected
while implementing the phase and has no numbered question.

| Id | Deviation | Plan question |
|---|---|---|
| D28 | Instances of classes without `var` fields are immutable and rebuilt field by field during construction: every field store returns a *new* object and `new` yields the last one. A reference to `this` that escapes before the last field is initialised — stored in a field, passed to another object, or **captured by a closure created in the constructor, including the initialiser of a `val` member** — therefore denotes an earlier version of the object: it lacks every field stored after that point (reading one raises `NoSuchMethodError: value <f> is not a member of <C>`) and it is neither `==` nor `eq` to the finished instance. A class that declares a `var`, itself or through an ancestor, builds mutable instances, where stores happen in place and neither problem arises. Scala has neither restriction | Q1 |
| D29 | Type tests follow the runtime representation: `Int`, `Long`, `Short`, `Byte` and `BigInt` are one integer type (D1), `Float` is `Double` (D2), so `3L.isInstanceOf[Int]` is `true`; `asInstanceOf` never converts numbers (`(1: Any).asInstanceOf[Double]` throws); `null.asInstanceOf[Int]` is `null`; an extractor's `unapply` is called without the type test its parameter type implies | Q10 |
| D30 | Reading a field of the instance under construction before its initialiser has run raises `NoSuchMethodError` (Scala reads the default value `0`/`null`) | Q11 |
| D31 | Methods cannot be overloaded (a second definition of a name in one template is an error); constructors may be overloaded by number of parameters only | Q11 |
| D32 | Tuples have at most 22 elements (Scala 3 has `TupleXXL`) | — (Scala 3.9 behaviour differs only above 22) |
| D33 | `Option.getOrElse` evaluates its default eagerly (by-name parameters are not supported yet) | Q7 |
| D34 | A pattern-matching function literal `{ case ... }` takes exactly one argument (Scala 3 also accepts it where a function of several parameters is expected) | Q12 |
| D35 | A refutable pattern generator written without Scala 3's `case` keyword — `for (Some(v) <- os)` — is accepted and filters. scalac 3.9 rejects it ("pattern's type `Some[Int]` is more specialized than the right hand side expression's type `Option[Int]`") and asks for `case`. protoScala accepts the pattern with or without `case` and filters either way | — |
| D36 | For-comprehension desugaring differs where the result does not: `case` on an *irrefutable* generator pattern emits no `withFilter` step (scalac always inserts one), and `for (x <- xs; y = e)` emits two `map`s instead of dotty's single fused `map`. Both produce the values scalac produces; only the number of intermediate traversals differs | — |
| D37 | An intersection type cannot be tested at run time: `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time with "this type cannot be tested at run time", where scalac 3.9 accepts them and tests both components. (The unparenthesised `case v: A & B` is a syntax error in scalac and is rejected here too, with a different message.) A parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by scalac, and both test only the tuple's erasure | — |
| D38 | The default `toString` of an object with no user-written `toString` is `Name@<identity hash>`: a singleton `object O` prints `O@…` where the JVM prints `O$@…`, and printing a companion or a tuple companion directly (`println(Tuple2)`) prints `Tuple2@<hash>` | — |
| D39 | `List` hash codes differ from the JVM's, while staying consistent with `==` (equal lists have equal hash codes). Case classes, case objects, tuples and strings hash bit-identically to the JVM | — |
| D40 | Diagnostic wording: a `var` that redefines a concrete inherited `var` without `override` is reported as "cannot override a mutable variable", where scalac says it "needs `override` modifier". protoScala rejects `override` on a `var` outright, so the two messages describe the same rejected program from opposite ends | — |
| D41 | Scala 3's universal apply (a creator application: `C(args)` standing for `new C(args)`) is **not** synthesised for a plain class. `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac 3.9 compiles it and prints `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes and case objects are unaffected: their companion `apply` is synthesised, so `C(args)` works | — |
| D42 | A `MatchError` names the Scala class of the unmatched value: `MatchError: 5 (of class Int)`, where the JVM names the boxed class (`scala.MatchError: 5 (of class java.lang.Integer)`). Consistent with D14 (unqualified class names) and D29 (one integer type) | — |

## Known issues / platform dependencies

See DESIGN §11 for the full table. Unchanged this phase: R2, R4, R5, R8.

- **R1** — Allocation-free loops have no GC poll: a tight `while` can stall a
  stop-the-world pause. Phase 1 makes a provisional choice (Q21): `JUMP_BACK`
  calls the existing public `ProtoContext::safepoint()` at every loop
  back-edge, where every live value of the frame is in a slot. This is a
  working default, not a resolution of R1, which DESIGN still assigns to the
  maintainer (an agreed back-edge poll API across embedders).
- **R2** — Every `ProtoTuple` is interned and perennial; protoScala never
  maps transient data to it (Scala tuples are case-class instances of
  `Tuple2`..`Tuple22`, DESIGN §4.6; varargs, captures and argument packs are
  `ProtoList`), so R2 is a protoClojure/platform-track concern, not a
  protoScala one.
- **R3 / protoCore `isInstanceOf`** — protoCore's `isInstanceOf` stops after
  50 visited objects and keeps 64 pending siblings
  (`core/ProtoObject.cpp:437-525`), which gives false negatives on chains of
  about ten ancestors — exactly the flattened chains protoScala installs for
  traits. Phase 2 therefore tests class and trait membership with a
  **per-class marker attribute** (opcode `TEST_PROTO`) found through the
  cached `getAttribute` walk, which is exact and allocation-free
  (Q2, DESIGN §5.3). The maintainer's fix,
  [platform/ISINSTANCEOF-FIX.md](platform/ISINSTANCEOF-FIX.md), removes that
  cap and the 500-step `getAttribute` cap; the markers stay until it is merged
  and every embedder has been rebuilt.
- **R4** — `createSymbol` leaks for strings longer than 6 bytes (protoClojure
  known issue; shared protoCore code path).
- **R5** — One runtime per process (process-global UMD module cache).
- **R6** — Now exercised: `SEND_SUPER` walks the receiver's linearization on
  every `super.m` call (DESIGN §4.4). The cost is O(linearization length);
  no protoCore change was needed.
- **R8** — Tagged-pointer budget: 37 of 64 pointer tags and 11 of 16
  embedded types are free today; P1 and P2 take one tag each (35 left).

**Platform state at the time of this release.** 0.2.0 was built and verified
against protoCore `e43fa2e4` (the 646/646 figures above are from that
pairing). protoCore **2.0.0** — the `ProtoMap` type plus the parent-chain
lookup fixes, with `SOVERSION 2` — was released in the sibling repository
immediately afterwards. protoScala has **not** been rebuilt from clean against
it yet: that is the scheduled embedder-migration step (DECISIONS-LOG,
2026-09-22: "rebuild, check and fix protoPython, protoJS, protoST,
protoClojure and protoScala"). Until it runs, a protoScala build that picks up
the new headers while linking the old shared library crashes, as the ABI rule
in CLAUDE.md warns; rebuild **both** projects from clean, in that order. Once
protoScala runs on protoCore 2.0.0, the marker-attribute workaround behind
`TEST_PROTO` can be reconsidered (R3 below).

Smaller notes:

- `flatMap` on a receiver that is neither a `List` nor an `Option` reports
  `NoSuchMethodError` (the missing `flatMap`), not `ClassCastException`.
- `tests/cli/gc-pressure.sh` pins its own `PROTOCORE_HEAP_LIMIT_CELLS` for
  every sub-invocation, so it does not run at the sweep's low ceiling;
  object-graph coverage under a low heap comes from the `08`, `10`, `11` and
  `12` conformance fixtures, which the unfiltered sweep runs.
- `benchmarks/comparable/object_tree.scala` keeps 131071 objects alive at
  once, so its CTest case pins `PROTOCORE_HEAP_LIMIT_CELLS=2000000`
  (`tests/CMakeLists.txt`) and the low-heap sweep runs unfiltered.

## Open bugs

None known. 646/646 tests pass (`ctest --test-dir build_release`), and 646/646
under `PROTOCORE_HEAP_LIMIT_CELLS=20000`.

## History

See [CHANGELOG.md](../CHANGELOG.md).
