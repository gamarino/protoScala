# protoScala Status

> Living tracker of the gap between [LANGUAGE.md](LANGUAGE.md) and the
> implementation. Update it with every change.
>
> **Current state (2026-09-23):** Phase 5 complete (0.3.0): everything Phases 1
> and 2 delivered, plus **actors with three priority bands, futures with a
> cooperative `await`, `Try`/`Success`/`Failure`, `Thread` and `System`**.
> Phases 3 and 4 are still ahead: Phase 5 was implemented out of order, so the
> minor version went from 0.2.0 to 0.3.0.
> **Tests:** 694 total (`ctest --test-dir build_release -N`) — 274 unit
> (GoogleTest, including the separate `unit/actors` binary), 388 conformance
> fixtures, 22 CLI checks, 10 benchmark smoke checks. All green, also under
> `PROTOCORE_HEAP_LIMIT_CELLS=20000` (the whole suite, unfiltered) and at
> `PROTOSCALA_ACTOR_WORKERS=1` and `=16`. Last verified 2026-09-23.

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

Delivered in Phase 5 (DESIGN §8), the concurrency block:

- [x] `Actor.spawn(state)(handler)`; a handler returns `(newState, reply)` or a
      bare `newState` when there is no reply (D45). Actors are mutable
      protoCore objects anchored in a registry
      pinned in a root slot, so the collector reaches every queued payload
      (D46).
- [x] `a ! msg` (tell), `a ? msg` (ask, a `Future` of the reply),
      `a.send(msg, priority)`, `a.ask(msg, priority)`, `a.value`,
      `Actor.isActor`, `Actor.stats` (`ActorStats(workers, messagesProcessed)`),
      the printed form `Actor(<state>)`.
- [x] `Priority.High` / `Medium` / `Low`: three per-actor mailboxes, drained
      in strict priority order, a batch of eight messages per turn.
- [x] The **single-method invariant**: one atomic per actor across claim, wake
      and suspension, so one actor never runs twice at once however many
      threads send to it.
- [x] A worker pool of protoCore threads (`PROTOSCALA_ACTOR_WORKERS`, default
      `max(2, cores − 2)` capped at 16), three lock-free ready stacks with
      ABA-tagged heads and a type-stable node pool, spin-before-park inside a
      `ProtoContext::UnmanagedScope`. The pool starts on the first
      `Actor.spawn`, so a script that uses no actor pays nothing.
- [x] `Future`: `await`, `isCompleted`, `value: Option[Try[T]]`, `map`,
      `flatMap`, `recover`, `onComplete`, `Future(e)` and `Future { … }` —
      the body is taken **by name** (D47) —
      `Future.successful`, `Future.failed`, the printed forms
      `Future(<pending>)` / `Future(10)` / `Future(<failed: …>)`.
- [x] **Cooperative `await` inside an actor**: the handler's call chain is
      snapshotted frame by frame, the worker is released and the actor stays
      claimed; the completion re-enqueues it and the chain is rebuilt
      (DESIGN §8.3). An `await` whose chain cannot be snapshotted is refused
      (D43). Outside an actor, `await` blocks on a condition variable inside
      an `UnmanagedScope`.
- [x] `Try` / `Success` / `Failure` and `RuntimeError(className, message)` in
      the prelude, moved up from Phase 3 (D44).
- [x] `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`,
      `System.currentTimeMillis()`, `System.getenv(name)` (D49).
- [x] The mailbox seam: protoCore's `ProtoMPSCQueue` when the linked protoCore
      provides it, a CAS'd `ProtoList` otherwise. **This build ships the
      fallback**: protoCore 2.0.0 carries no `newMPSCQueue`.
      `protoscala --version` names the backend in use.
- [x] The seven benchmark modes of DESIGN §8.5, each self-reporting and
      verified by the runner; tutorial chapter 13.

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
- UMD and packaging — Phase 6.
- Supervision trees, `ExecutionContext`, actor timeouts and
  `Await.result(f, duration)` — not scheduled. An `await` waits forever; the
  shutdown reports any actor still parked on a future that never completed.
- Actor mailboxes on protoCore's `ProtoMPSCQueue` — Phase P2 (the `Mailbox`
  seam is in place; switching is a one-file change once protoCore merges it).

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
| 38 | `FORCE_THUNK` | `[v] -> [v()]` | a read of a by-name parameter (D47): runs a zero-argument function, passes any other value through (D53) |
| 39..63 | reserved | | Phase 1 additions |
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
| 128..159 | reserved | | still reserved; Phase 5 shipped the actor surface as ordinary sends to native methods (plan Task 0 A0-11), so `SEND_ASYNC`, `ASK` and `AWAIT` were not needed. A dedicated opcode is a later optimisation to be justified by `perf stat -r 3` |

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
| D33 | `Option.getOrElse` evaluates its default eagerly: it is a method reached through a dynamic send, where a by-name parameter is not honoured (D53) | Q7 |
| D34 | A pattern-matching function literal `{ case ... }` takes exactly one argument (Scala 3 also accepts it where a function of several parameters is expected) | Q12 |
| D35 | A refutable pattern generator written without Scala 3's `case` keyword — `for (Some(v) <- os)` — is accepted and filters. scalac 3.9 rejects it ("pattern's type `Some[Int]` is more specialized than the right hand side expression's type `Option[Int]`") and asks for `case`. protoScala accepts the pattern with or without `case` and filters either way | — |
| D36 | For-comprehension desugaring differs where the result does not: `case` on an *irrefutable* generator pattern emits no `withFilter` step (scalac always inserts one), and `for (x <- xs; y = e)` emits two `map`s instead of dotty's single fused `map`. Both produce the values scalac produces; only the number of intermediate traversals differs | — |
| D37 | An intersection type cannot be tested at run time: `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time with "this type cannot be tested at run time", where scalac 3.9 accepts them and tests both components. (The unparenthesised `case v: A & B` is a syntax error in scalac and is rejected here too, with a different message.) A parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by scalac, and both test only the tuple's erasure | — |
| D38 | The default `toString` of an object with no user-written `toString` is `Name@<identity hash>`: a singleton `object O` prints `O@…` where the JVM prints `O$@…`, and printing a companion or a tuple companion directly (`println(Tuple2)`) prints `Tuple2@<hash>` | — |
| D39 | `List` hash codes differ from the JVM's, while staying consistent with `==` (equal lists have equal hash codes). Case classes, case objects, tuples and strings hash bit-identically to the JVM | — |
| D40 | Diagnostic wording: a `var` that redefines a concrete inherited `var` without `override` is reported as "cannot override a mutable variable", where scalac says it "needs `override` modifier". protoScala rejects `override` on a `var` outright, so the two messages describe the same rejected program from opposite ends | — |
| D41 | Scala 3's universal apply (a creator application: `C(args)` standing for `new C(args)`) is **not** synthesised for a plain class. `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac 3.9 compiles it and prints `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes and case objects are unaffected: their companion `apply` is synthesised, so `C(args)` works | — |
| D42 | A `MatchError` names the Scala class of the unmatched value: `MatchError: 5 (of class Int)`, where the JVM names the boxed class (`scala.MatchError: 5 (of class java.lang.Integer)`). Consistent with D14 (unqualified class names) and D29 (one integer type) | — |

### Provisional deviations (Phase 5) — pending maintainer decision

Decided by the implementing agent under the maintainer's standing
authorisation and recorded in [DECISIONS-LOG.md](DECISIONS-LOG.md) as
"agent, pending review".

| Id | Deviation | Track |
|---|---|---|
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction. Inside a native higher-order method (`map`, `foreach`, `withFilter`, a `Future` continuation), or under an instruction that calls back into Scala without being a call site (`==` reaching a user `equals`, a `MatchError`'s `toString`), it raises `UnsupportedOperationException` instead of suspending; the ask's future receives that failure and the actor stays alive | later |
| D44 | Until Phase 4 there are no exception values: a failed `Future` carries `RuntimeError(className, message)`, and `Try`/`Success`/`Failure` (moved up from Phase 3) wrap it. `await` on a failed future raises the same error on the awaiting thread, which no user code can catch yet | Phase 4 |
| D45 | An actor handler returns **either `(newState, reply)` or a bare `newState`**; the bare form means there is no reply to give and the ask's future completes with `()`. A `Tuple2` result is always read as the pair form, so an actor whose state is itself a pair returns it inside one. Only a handler that produces no value at all is rejected, with `IllegalArgumentException`. The actor keeps its previous state when the handler fails. **Overturned by the maintainer on 2026-09-23** (least surprise): the original D45 required the pair form strictly | (perm) |
| D46 | An actor lives as long as the session: it is anchored in a registry so the GC can reach it and everything it holds while it is only referenced by the (C++) ready stacks. `Future.apply` creates one actor per call | P1 |
| D47 | `Future.apply` takes its body **by name**: `Future(expr)` and `Future { … }`, as in Scala. By-name parameters are honoured where the compiler can name the callee (D53). **Overturned by the maintainer on 2026-09-23** (least surprise): the original D47 took a function, `Future(() => expr)` | (perm) |
| D48 | `map`/`flatMap`/`recover`/`onComplete` run their continuation on the thread that completes the future, or immediately on the caller when it is already complete — there is no `ExecutionContext`. A continuation may not `await` (D43) | later |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's: `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`, `System.currentTimeMillis()`, `System.getenv(name)` (`""` when unset). `nanoTime` is a monotonic clock; only differences are meaningful | (perm) |
| D50 | Awaiting a future that fails, inside an actor, abandons the rest of the handler and the message's own future inherits the failure (there is no `try`/`catch` to resume into until Phase 4) | Phase 4 |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a send that is still queued (protoClojure's `@actor`) | (perm) |
| D52 | `Priority.High` / `Medium` / `Low` are the integers `0` / `1` / `2` on an object, not an `enum` (enums arrive in Phase 4) | Phase 4 |
| D53 | A by-name parameter is honoured only where the compiler resolves the call site to the declaration: a call by name to a top-level or local `def` (any parameter list, including a curried one), a method of the template being compiled, a method of an `object`, a class's primary constructor, and a builtin whose by-name signature the runtime declares (`Future.apply`). At any other call site — a method reached through a dynamic send, or a `def` taken as a function value — the argument is evaluated once at the call and each read of the parameter yields that value. scalac resolves all of these statically, so it stays lazy where protoScala does not | later |

## Known issues / platform dependencies

See DESIGN §11 for the full table. Unchanged this phase: R2, R4, R5, R8.

- **Phase 5 / R1.** A C++ thread that polls an actor's state must reach a
  safepoint between polls, or a stop-the-world pause waits for it and every
  worker stalls; a Scala spin loop gets this for free because `JUMP_BACK`
  calls `ProtoContext::safepoint()` at every back-edge (Q21). The scheduler
  unit test's polling loop learned this the hard way under
  `PROTOCORE_HEAP_LIMIT_CELLS=20000`.
- **Phase 5 / cold start — at or just above target, not confirmed.** The
  < 25 ms budget (DESIGN §1) measured 24.20 ms (script) and 23.58 ms (REPL) at
  load average ~4, and 25.11 ms / 26.57 ms at load average ~8.7 on the same
  build. **The host was shared with three other builds all night, so this is
  not a clean measurement and the target is not claimed as met.** The cause is
  not eager worker creation — a script with no actor makes 2 `clone3` calls and
  one with an actor makes 12, so the pool really does start only at the first
  `Actor.spawn` — but the prelude did grow by the `Try`/`Success`/`Failure`,
  `RuntimeError` and `ActorStats` definitions plus six constructor `def`s
  (D44), and the prelude is compiled at every start-up. Re-measure on a quiet
  host before the release is called done.
- **Phase 5 / mailbox size.** With the CAS-list fallback, a backlog of N
  messages on one actor is an N-element `ProtoList` plus N envelopes, and every
  push rebuilds an O(log N) path. `tests/cli/actors-stress.sh` queues 200000
  messages into one actor and needs a ceiling between 1000000 and 1500000
  cells to complete; it therefore pins its own
  `PROTOCORE_HEAP_LIMIT_CELLS=8000000`, as `benchmarks/object_tree.scala` does,
  so the low-heap sweep stays meaningful for every other test. A real
  `ProtoMPSCQueue` (Phase P2) removes the rebuild.
- **Phase 5.** The ready stacks retain their node high-water mark for the
  session (the type-stable pool is never shrunk). An actor parked on a future
  that never completes keeps itself and its queued messages alive until exit,
  where the shutdown prints a diagnostic naming how many are parked. Actors are
  never collected (D46). `PROTOSCALA_ACTOR_WORKERS` above the physical core
  count measured no gain (`benchmarks/RESULTS.md`).
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
immediately afterwards. A first clean rebuild of protoScala against protoCore
2.0.0 (2026-09-23 01:0x) builds with no warnings and runs: **644 of 646 tests
pass**, and every behavioural test does — all 352 conformance fixtures, all 19
CLI checks, all 10 benchmark smoke checks, and every unit test but two. The two
failures are the unit tests that *pin protoCore's parent-chain contract*, and
both changed deliberately in 2.0.0:

- `ObjectModel.InstancesOfAnImmutableShapeWalkItsWholeChain` — `getParents` on
  an instance now returns 6 entries where the test pins 4, because `setParents`
  flattens the chain (explicit parents in order, then the missing ancestors
  appended). Every semantic assertion in that test still passes: the
  linearization order, `who` resolving to `T2`, the last parent still reachable
  and the membership marker are unchanged.
- `ObjectModel.SetParentsOnAMutableObjectIsInvisibleToItsChildren` — a child of
  a mutable prototype now *does* see a later `setParents`, which is protoCore
  2.0.0's C1 fix (`newChild` takes the chain from the mutable prototype's
  current snapshot). The test's own comment prescribes the follow-up: "If this
  starts failing, protoCore made parent chains live: update Design note 2 (the
  immutable-shape construction stays correct)."

Restating those two pins, and Design note 2 with them, belongs to the
embedder-migration step (DECISIONS-LOG, 2026-09-22: "rebuild, check and fix
protoPython, protoJS, protoST, protoClojure and protoScala") and has not been
done here. Note also that a build which picks up the new headers while linking
the old shared library crashes, as the ABI rule in CLAUDE.md warns: rebuild
**both** projects from clean, in that order. Once protoScala runs on protoCore
2.0.0, the marker-attribute workaround behind `TEST_PROTO` can be reconsidered
(R3 below).

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

None known. 694/694 tests pass (`ctest --test-dir build_release`), 694/694
under `PROTOCORE_HEAP_LIMIT_CELLS=20000`, and 693/693 at
`PROTOSCALA_ACTOR_WORKERS=1` and `=16`.

Not run in this phase, and therefore not claimed: the ThreadSanitizer build of
the plan's Task 8 Step 4. The machine was shared with other builds while
Phase 5 was implemented and a TSan run would have invalidated the benchmark
table measured on it; it is the first thing to run on a quiet host.

## History

See [CHANGELOG.md](../CHANGELOG.md).
