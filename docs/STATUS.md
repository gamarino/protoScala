# protoScala Status

> Living tracker of the gap between [LANGUAGE.md](LANGUAGE.md) and the
> implementation. Update it with every change.
>
> **Current state (2026-09-25):** Track F complete (file input and output);
> Phase 4 complete (**0.5.0**): everything
> Phases 1, 2, 3 and 5 delivered, plus **`try`/`catch`/`finally` and `throw` with
> pattern-matched handlers, the `Throwable` hierarchy and native error
> translation, `super[T].m`, `enum` and sealed hierarchies, named and default
> arguments for Scala-defined methods (and for every other callable protoCore can
> reach), extension methods, templates nested in an `object`, and multiple
> constructor parameter lists**. Phase 5 was implemented out of order, so the
> minor version went 0.2.0 → 0.3.0 (Phase 5) → 0.4.0 (Phase 3) → 0.5.0 (Phase 4)
> → 0.6.0 (Phase 6: modules, UMD and packaging). Packaging in fact landed in the
> installer phase; Phase 6 closed the `.tar.gz` half and the version.
> **Track F** (2026-09-25) adds **file input and output**: `scala.io.Source` for
> reading, and a four-operation `FileIO` object for writing (D97–D102).
> **Track X** (2026-09-25) is the first track driven by a measurement against
> someone else's tests rather than our own: the Scala 3 compiler's `tests/run`
> corpus (1654 single-file programs, dotty `a68b419c`). It adds the **Predef
> surface** — `assert`, `assume`, `require`, `???`, `AssertionError`,
> `NotImplementedError` and `App` (D103–D104) — and restores plain Scala's
> **member import** (`import Color.*`, D105), which Phase 6 had silently replaced
> with the module-loading form. See "The Scala 3 run-corpus measurement" under
> "Known issues" for the before/after numbers.
> **Tests:** 1299 total (`ctest --test-dir build_release -N`) — 371 unit
> (GoogleTest, including the separate `unit/actors` and `unit/modules` binaries
> and `umd/protost-interop`), 877 conformance fixtures, 24 CLI checks, 12
> benchmark smoke checks, 15 embedder-conformance rules. **All
> green**, and green at `PROTOSCALA_ACTOR_WORKERS=1` and `=16` (1248/1248
> including `umd/protost-interop` in all three). Under
> `PROTOCORE_HEAP_LIMIT_CELLS=20000` (the whole suite, unfiltered) 1247 of 1248
> pass: `Mailbox.EightProducersLoseNothingAndDuplicateNothing` aborts, which was
> verified to be **pre-existing** — it fails the same way on `main` at `bca0352`
> — and is recorded under "Open bugs". It is the only failure in that
> configuration, so it masks nothing, and it is what makes the low-heap sweep a
> usable rooting check for new native code: Track F's 44 fixtures, including one
> that round-trips a megabyte through a 64 KiB read loop, pass at a 20000-cell
> ceiling.
>
> `umd/protost-interop` is the 1248th case: it links protoST into a test
> executable and, since Track Y, is built by default whenever protoST is found
> beside this tree (`-DPROTOSCALA_PROTOST_INTEROP=OFF` restores a suite that
> refers to no other tree). It holds the cross-runtime import tests; see R5 under
> "Known issues".
> Last verified 2026-09-25 (Track F).

## Implemented

Per [LANGUAGE.md](LANGUAGE.md) §1–§2, the rows delivered in Phase 1:

- [x] Identifiers: alphanumeric, operator, mixed, backquoted.
- [x] Hard and soft keywords (§1's full lists, including `this`).
- [x] Literals: decimal/hex/binary integers with `_` and `L`; floating point;
      characters with escapes and `\u`; strings, triple-quoted strings;
      `s`/`f`/`raw` interpolators lexed as structured tokens (Phase 3
      executes them; see below).
- [x] Comments: `//` line, nested `/* ... */` block.
- [x] Significant indentation (offside rule) and braces, mixable; `end`
      markers.
- [x] `val`, `var`, `lazy val`, `def` (multiple parameter lists, varargs
      `xs: Int*`, default and named arguments — Phase 4).
- [x] `if`/`then`/`else`, `while`/`do`, blocks as expressions, `return`
      (methods only — D11).
- [x] Lambdas `x => e`, `(x, y) => e`.
- [x] Infix, prefix (`-x`, `!b`, `~n`) and postfix-free method application.
- [x] `import` — a real binding form since Phase 6: it loads a module and binds
      names, and its classes are usable as types (§3.2 of LANGUAGE.md, D90–D96).
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
- [x] `super.m`, including stackable traits (DESIGN §4.4); `super[T].m` arrived
      in Phase 4.
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
- [x] `Try` / `Success` / `Failure` in the prelude, moved up from Phase 3. Since
      Phase 4 `Failure` carries the `Throwable` itself and `RuntimeError` is gone
      (D44 retired).
- [x] `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`,
      `System.currentTimeMillis()`, `System.getenv(name)` (D49).
- [x] The mailbox seam: protoCore's `ProtoMPSCQueue` when the linked protoCore
      provides it, a CAS'd `ProtoList` otherwise. **This build ships the
      fallback**: protoCore 2.0.0 carries no `newMPSCQueue`.
      `protoscala --version` names the backend in use.
- [x] The seven benchmark modes of DESIGN §8.5, each self-reporting and
      verified by the runner; tutorial chapter 13.

### Phase 3 — collections, prelude and string interpolation

Per [LANGUAGE.md](LANGUAGE.md) §4.2, which lists the delivered surface method by
method. Every item below is covered by conformance fixtures, and every fixture
whose behaviour should match Scala was verified against
`tools/scala3-3.9.0` (`bin/scalac` plus `java -cp "$SCALA_HOME/lib/*:out"`).

- [x] **String interpolation executes.** `s"…"` and `raw"…"` lower to the new
      `CONCAT` opcode, which joins the pieces with `ProtoString::appendLast`, an
      O(log n) rope join, so `s"$a$b"` on two strings copies neither (D54).
      `f"…"` lowers to a call of the native `__fmt` with the specifiers as
      compile-time constants, so a malformed one is a compile error at the
      interpolation's position (D55). An interpolator that is not `s`, `f` or
      `raw` is lowered to `StringContext(<literals>).<name>(<args>)` since
      Phase 4, so a custom interpolator is an extension method on `StringContext`
      and D56 is retired. A hole is parsed by the same
      Lexer/Layout/Parser pipeline as the file, so it may hold any expression,
      including another interpolation.
- [x] **The full `List` surface** — 40 methods beyond Phase 2's, including
      `foldLeft`/`foldRight` (both the Scala `xs.foldLeft(z)(f)` spelling and
      the one-list `xs.foldLeft(z, f)`), `sorted`/`sortBy`/`sortWith` (a stable
      merge sort, D62), `groupBy`, `zip`, `zipWithIndex`, `partition`,
      `splitAt`, `distinct` and `flatten`.
- [x] **`Vector`**, an object holding a `ProtoList` under one attribute. `List`
      and `Vector` are *one* implementation installed on both prototypes, so
      the two surfaces cannot drift; a result is of the receiver's own kind.
- [x] **`Range`** (`0 until 5`, `1 to 5`, `… by step`): `length`, `apply`,
      `head`, `last`, `sum` and `contains` are O(1) arithmetic and it is never
      materialised except by `toList`/`toVector`/`toSet`/`mkString`. A bound
      must fit a `SmallInteger` (D61); `map`/`flatMap`/`filter` answer a `List`
      (D68); `reverse` answers a `Range`, as Scala's does.
- [x] **`Map` and `Set` on protoCore's `ProtoMap`**, read and written only
      through `proto::hashedPut`/`hashedGet`/`hashedRemove`/`hashedForEach` with
      one `proto::KeySemantics` whose callbacks are protoScala's own
      `scalaIsIdentityKey`, `scalaHash` and `valuesEqual` — so a `Map` can never
      disagree with `==`, and Scala's cooperative numeric equality makes `1`,
      `1L` and `1.0` one key. The identity/value classification is DESIGN §6.1's,
      decided by an exact pointer comparison against the single `equals` on
      `anyProto`; `Char` and parameterised `enum` cases are value keys (the
      maintainer's rulings C1 and C2 of 2026-09-23). Iteration is ascending-hash
      (D58).
- [x] **Cross-kind `Seq` equality and hashing**: `List(1,2) == Vector(1,2) ==
      (1 to 2)`, all three hash alike, and a `Map` keyed by one is found by
      another (D59). Decided once, in `valuesEqual` and `scalaHash`, over one
      allocation-free `SeqView`, so `==` and `.equals` cannot split.
- [x] **`Either`/`Left`/`Right`** (right-biased), an **extended `Option`**
      (`fold`, `toRight`, `toLeft`, `orNull`, `forall`, `count`, `zip`,
      `iterator`, `toSeq`) and an **extended `Try`** (`map`, `flatMap`,
      `foreach`, `recover`, `recoverWith`, `orElse`, `toEither`), with
      `Try { … }` taking its body by name. Neither `Either` nor `Try` has
      `withFilter` (D67). Since Phase 4 `Failure` carries the `Throwable` itself,
      as Scala's does; `RuntimeError` is gone and D44 is retired.
- [x] **The `String` surface**: `split` (literal separator, answering a `List`
      — D69, D70), `replace`, `stripMargin`, `stripPrefix`, `stripSuffix`,
      `lastIndexOf`, `capitalize`, `equalsIgnoreCase`, `compareTo`,
      `toBoolean`, `format` (through the same formatter as the `f`
      interpolator), and the collection-like `toList`, `head`, `last`, `init`,
      `take`, `drop`, `takeWhile`, `dropWhile`, `map`, `filter`, `foreach`,
      `mkString`.
- [x] `->` on `Any`, so `k -> v` is the `Tuple2` `(k, v)` — a case-class
      instance, never a `ProtoTuple` (DESIGN §4.6).
- [x] **Benchmark suite v1** (`fib`, `tak`, `sum_loop`, `list_ops`,
      `map_build`), each printing the work it did and verified by the runner
      before any rate is computed; recorded in
      [../benchmarks/RESULTS.md](../benchmarks/RESULTS.md).
- [x] Tutorial chapters 8 (Collections) and 10 (Strings and interpolation),
      with a conformance fixture per runnable snippet.

**Delivered beyond the phase's done-when:** the `unary_-`/`unary_!`/binary
operator symbols are no longer interned on every execution (a measured 10.0 %
cycle reduction on an operator-overload loop), `->` on `Any`, the `String`
surface above, and `O(args)` on an object now honouring `apply`'s by-name
parameters as `O.apply(args)` already did.

### Phase 4 — exceptions, `super[T]`, enums, arguments and extensions

Per [LANGUAGE.md](LANGUAGE.md) §2, §3 and §3.1. Every item is covered by
conformance fixtures, and every fixture whose behaviour should match Scala was
verified against `tools/scala3-3.9.0` (`bin/scalac` plus
`java -cp "$SCALA_HOME/lib/*:out"`).

- [x] **`try` / `catch` / `finally` / `throw`, with pattern-matched handlers.**
      Each `BytecodeModule` carries a handler table of
      `{startPc, endPc, handlerPc, stackDepth, slot, kind}` in innermost-first
      order, so a `try` costs nothing at run time until something throws and the
      table holds no `ProtoObject*` (DESIGN §4.6). `ExecutionEngine::runFrame` is
      a retry loop **outside** the C++ catch block: the handler body is entered by
      `continue`, so it runs with no live C++ handler, suspends cooperatively like
      any other bytecode, and — the load-bearing half — the frame can catch a
      **second** exception raised by its own handler, by a non-matching cascade's
      `RETHROW` or by a `finally`. A `catch` body is `compileMatch`'s own cascade
      with `RETHROW` instead of `MATCH_ERROR`, so pattern-matched handlers are not
      a second implementation of pattern matching. `finally` is a handler-table
      entry plus an inline copy on every normal exit and before every `return`.
- [x] **The `Throwable` hierarchy in the prelude** (twenty classes: `Throwable`,
      `Exception`, `Error` and the seventeen below them), so
      `case e: ArithmeticException` is the same per-class marker test as
      `case p: Point`.
- [x] **Native error translation.** Every failure the runtime raises becomes a
      real exception value of the class its name means, materialised **lazily** —
      the uncaught path allocates nothing, because it may be dying of
      `OutOfMemoryError`. `std::invalid_argument` and `std::out_of_range` are
      caught by their **exact** types and no `std::logic_error` clause exists, so
      a VM or compiler defect can never be masked by `case e: Throwable` (D74).
- [x] **Exceptions across an actor turn and a suspended `await`.** A handler
      exception fails that message with the exception value itself and leaves the
      actor alive; a throwing thread body reports and joins; `resumeFrames` runs a
      resumed frame **with its handler table active**; no `finally` runs on a
      suspension (D75); and a failed `await` raises **at its call site**, so
      `try { f.await } catch { … }` works across a suspension.
- [x] **`super[T].m`**: the search starts AT `T`, so `super[A].m` finds `A`'s own
      `m` when `A` defines it and the `m` `A` inherits when it does not. Plain
      `super.m` is unchanged (D76 for the direct-parent rule).
- [x] **`enum` and sealed hierarchies**, lowered entirely in the frontend to a
      sealed abstract class, one `case object` or `case class` per case inside the
      companion, and generated `values` / `valueOf` / `fromOrdinal` — whose
      messages are scalac's own, byte for byte. `ordinal` is a `val` the desugarer
      writes and `toString` is Product's, so `enum` needs **no native method and
      no new opcode**. `values` and `valueOf` exist only when every case is a
      singleton, as in Scala.
- [x] **Named and default arguments** for Scala-defined methods, constructors,
      case-class `apply`/`copy`, function values and local functions — bound in
      the **callee**, through protoCore's `keywordParameters` keyed by the address
      of the interned parameter-name symbol, with no special case for a foreign
      callee (`docs/INTEROP.md` §7). All three errors are loud and name the method
      and the parameter (D81). `CALL_KW` (80) is the one new opcode this needed.
- [x] **Extension methods**, installed as attributes of the receiver type's
      prototype, so dispatch is the ordinary prototype walk (D6) and a collision
      with an existing member is refused (D82, D83). Class prototypes are now
      **mutable**, which is what lets an instance created before the extension see
      it. `__installExtension` is the one new internal global.
- [x] **Custom string interpolators**, which extension methods bring:
      `name"…"` is lowered to `StringContext(<literals>).name(<args>)` over a new
      prelude `StringContext`, exactly as Scala lowers it. **D56 retired.**
- [x] **Templates nested in an `object`**, lifted to the top level with a
      qualified name (`@O.C`), resolvable as `O.C` from outside and as `C` inside
      `O`, with the simple name shown by `toString`. Nesting in a `class` or a
      `trait` is still rejected (D80).
- [x] **Multiple constructor parameter lists**, concatenated into one flat list
      (D84), which LANGUAGE §3 assigned to this phase; ROADMAP's done-when and
      this document's own "Not yet implemented" list have been corrected to agree.
- [x] Tutorial chapters 11 (Exceptions) and 12 (Enums and sealed hierarchies),
      with a conformance fixture per runnable snippet, plus new sections in
      chapters 2, 5, 6, 10 and 13.

**Delivered beyond the phase's done-when:** `Priority` became a real `enum`
(retiring D52), `Failure` carries a `Throwable` (retiring D44), a failed `await`
is catchable (retiring D50), custom interpolators work (retiring D56), the
keyword-argument convention is documented in `docs/INTEROP.md` with a
stand-in-callee fixture suite, and a default value may read an enclosing local as
well as an earlier parameter.

### Phase 6 — modules, UMD and packaging

Per [LANGUAGE.md](LANGUAGE.md) §1 and §3 and [INTEROP.md](INTEROP.md). Every item
is covered by conformance fixtures; the module forms that have a Scala meaning
were checked against that meaning, and the ones that do not (there are no
file-level modules in Scala) are recorded as D90-D96.

- [x] **`import` is a binding form.** It resolves at **compile time**, through a
      `ModuleLoader` seam the `Compiler` holds a pointer to, and that is the one
      place in the dialect that binds early rather than late. It is deliberate: a
      name bound at run time carries no `ClassInfo`, so an imported class could
      not be used as a **type**, and importing types is what a Scala programmer
      does with `import`. DESIGN §3.3 still holds — the loader returns plain C++
      descriptors and no `ProtoObject*` reaches the compiler or the AST.
- [x] **A module is an `object`** (D91). `desugarModule` wraps a module file's
      top-level statements in a synthetic `object <LastSegment>` and runs the
      ordinary desugarer, so Phase 4's nested-template lifting does all the work:
      the classes get their qualified names and their companions, the `def`s
      become members, and no second object model is introduced.
- [x] **The five import forms**: the module itself, `as` rename, a selector list
      with per-name renames (`as` and `=>`), a wildcard (`*` and `_`), and a
      selector that names a **type** — which makes `new Point(1, 2)`,
      `case p: Point` and `case Point(x, y)` compile. 27 fixtures in
      `tests/conformance/24-modules/` and a REPL check in `tests/cli/modules.sh`.
- [x] **`ScalaModuleProvider`** — alias `scala`, GUID `protoScala-source-v1`,
      registered once per process with `std::call_once`, resolving its session
      through a **`ProtoSpace`-keyed** registry rather than a thread-local (a
      thread-local answers "module not found" on every actor worker, which is the
      bug protoST's header records). `provider:scala` is **prepended** to the
      space's resolution chain, never substituted for it. A miss is `PROTO_NONE`
      and never an exception, so the chain continues to the next provider.
- [x] **Exactly-once loading per canonical absolute path**, with cycle detection
      (`cyclic module import: <path>`), a failed load deliberately **not** cached
      so a fixed file can be imported again, and a waiting importer parked inside
      `UnmanagedScope` so the collector never waits for it.
- [x] **Prefix routing** for `py.`, `js.`, `st.` and `clj.`, from a **closed**
      four-name list — "any registered alias is a prefix" would make
      `import util.Strings` hijackable by a plug-in aliased `util`, and a
      program's meaning must not depend on which plug-ins are installed. A
      prefixed import calls the named provider's `tryLoad` **directly** and does
      not go through `getImportModule`, because protoCore's `SharedModuleCache`
      is keyed by logical path with no `ProtoSpace` component.
- [x] **Provider plug-ins**, `dlopen`'d from `PROTOSCALA_PROVIDERS` and from
      `<prefix>/lib/protoscala/providers`, exporting `protoScalaProviderABI` and
      `protoScalaRegisterProviders`. protoScala ships **none**; the only one built
      is the test double of `tests/unit/probe_provider.cpp`, which answers
      `probe` and never a real library's data. `--version` reports what it found.
- [x] **The mandatory boundary catch shape** (`src/umd/ForeignBoundary.h`): six
      clauses in ROADMAP's order plus `catch (const std::logic_error&) { throw; }`
      before the `std::exception` arm, which ROADMAP's list omits and D74
      requires. One unit test per clause; removing any one turns exactly one red.
      `ExecutionEngine::callNative` gained the last-resort clause it lacked, so a
      native from a plug-in that throws a non-`std::exception` is a catchable
      Scala `RuntimeException` instead of a terminated process.
- [x] **A named argument across a real UMD boundary**, in
      `tests/conformance/25-interop/`, with a sibling whose parameter name is too
      long to embed in a pointer word — the case where a key built with anything
      but `createSymbol` fails silently.
- [x] **The prelude is compiled at build time.** `protoscala-precompile` emits
      static tables that `src/runtime/PreludeImage.cpp` walks, with no lexer,
      parser, desugarer or compiler in the start-up path. It is a build product
      with a CMake `DEPENDS` on `lib/prelude.scala`, not a cache, so it cannot go
      stale; a format version and an FNV-1a-64 of the source guard a hand-copied
      file and **fall back** to the source rather than failing.
- [x] **Packaging**: both a `.deb` and a `.tar.gz` at 0.6.0, each extracted and
      run under `env -u LD_LIBRARY_PATH`, with `ldd` confirming libprotoCore
      resolved from inside the package.
- [x] Tutorial chapter 15 and the worked example, with a conformance fixture per
      runnable snippet, and two "protoScala in 10 minutes" sections in the README.

### Track F — file input and output

Reading imitates `scala.io.Source`, which is real Scala standard library, and
every behaviour that can be checked was checked against **scalac 3.9.0** rather
than assumed. Writing is protoScala's own explicit surface, because Scala's writer
is `java.io.PrintWriter` and there is no Java here (D102). 44 conformance fixtures
(`tests/conformance/26-file-io/`, `tests/conformance/tutorial/16-files-*`, and two
README snippets), each
one shown to be capable of failing by a mutation of the implementation.

- [x] **`Source.fromFile(path)`**, **`Source.fromFile(path, enc)`** (UTF-8 only,
      D99) and **`Source.fromString(s)`**, answering a `BufferedSource` with
      `mkString`, `getLines()`, `close()`, `isOpen` and `toString`.
- [x] **`getLines()`** splits on `\n`, `\r\n` and a lone `\r`, strips the
      terminator, adds no final empty line for a trailing terminator, and answers
      no lines at all for an empty file. Every row of that table was verified
      against scalac (D100). One splitter serves `fromFile` and `fromString`.
- [x] **UTF-8 decoding is strict**, as the JVM's is: an overlong form, a surrogate
      code point, a value above U+10FFFF and a sequence truncated at end of input
      are all `MalformedInputException` rather than replacement characters. The
      three forms a lenient decoder lets through are pinned by committed files of
      exactly those bytes.
- [x] **`FileIO.write` / `append` / `exists` / `delete`** (D102), each one call,
      each naming its path in every failure.
- [x] **Every failure mode raises**, with the class the JVM uses and a message
      naming the path (D97, D98): a missing file, a directory where a file was
      expected, no read or write permission, a missing parent directory on write,
      invalid UTF-8, a read of a closed source, `delete` on a directory, and a
      path containing a NUL byte — which is refused rather than truncated at the
      NUL, since truncating would silently operate on a **different** file.
- [x] **Every syscall's result is checked**, including `close` on the write path,
      where some filesystems report a failed write for the first time; a short
      `write` loops rather than being mistaken for a whole one.
- [x] The worked example (`examples/log-report/`) **opens `sample.log`**. The
      duplicate copy it used to carry, and the diff that kept the two in step, are
      both gone; `tests/cli/examples.sh` now checks the property that matters, by
      running the program against an edited copy and demanding a different report.
- [x] Tutorial chapter 16, with a fixture per runnable snippet and a check that
      each snippet is, verbatim, its fixture's body.

### Track X — what the Scala 3 run corpus found

Every one of protoScala's other 1263 tests was written here, so they encode our
beliefs and cannot detect a misunderstanding we share with them. Track X is the
first work driven by tests nobody here wrote: the Scala 3 compiler's own
`tests/run` corpus. Both items below were verified against **scalac 3.9.0**
(`bin/scalac`, then `java -cp "$SCALA_HOME/lib/*:out"`), and the actual outputs
compared are quoted in [DECISIONS-LOG.md](DECISIONS-LOG.md).

- [x] **The Predef surface**: `assert`, `assume`, `require`, `???`, and the two
      exception classes they raise (`AssertionError`, `NotImplementedError`).
      Scala's exception type and Scala's exact message text for each, including
      that a call with **no** message gets the bare prefix (`assertion failed`,
      not `assertion failed: assertion failed`) and that a message of `null` is
      reported as `null`. The message is by-name, so an assertion that holds never
      builds it. Written in the prelude, not as natives (D103).
- [x] **`App`**: `object Main extends App` runs the object's body as the program,
      as in Scala. Deprecated in Scala 3 in favour of `@main`, which protoScala
      already supported; the restrictions protoScala adds are one App object per
      file, not both an App object and an `@main`, and none in a module (D104).
- [x] **Plain Scala's member import** is back, alongside Phase 6's module-loading
      form (**D105**): `import Color.*`, `import Obj.{a, b}`, `import Obj.a as b`
      and `import Obj as O`, on an `object`, a companion or an `enum` in scope.
      `docs/LANGUAGE.md` §3.2 documented `import` **only** as file-modules and
      never said the ordinary form had gone; that section is now corrected and
      names both forms and the rule that tells them apart. Phase 6 recorded no
      deviation claiming the member import was absent by design, so there was no
      deviation to retract — the omission was silent, which is worse.
- [x] 15 conformance fixtures in `tests/conformance/27-predef/` and 17 in
      `tests/conformance/28-member-imports/`, plus 2 tutorial fixtures, each one
      shown to be capable of failing by a named mutation of the implementation:
      11 mutations for the Predef half and 12 for the import half, every fixture
      turned red by at least one. Plus 4 tutorial/README fixtures.

## Not yet implemented

- A `class`, `trait` or `object` nested in a **`class`** or **`trait`**, a local
  class inside a block, and the anonymous-class form `new T { ... }` (D80). Each
  captures the enclosing instance, which needs a per-instance class; ROADMAP
  records them for a later phase. Nesting in an **`object`** is implemented
  (Phase 4).
- Exhaustiveness checking for `match` (D4: types are erased).
- **Anything about files beyond reading and writing one whole text file.**
  Track F delivered `scala.io.Source` (`fromFile`, `fromString`, `mkString`,
  `getLines()`, `close()`) and `FileIO` (`write`, `append`, `exists`, `delete`);
  see D97–D102. Still absent: **directories** (no `mkdir`, no listing, no
  rename, no `java.nio.file.Path`), **binary files and random access** (no
  `InputStream`, no `FileChannel`, no byte arrays), **any encoding but UTF-8**
  (D99), **an `Iterator`**, so nothing streams — a file is read whole into memory
  (D100, D101) — **stdin** (no `Source.stdin`, no `readLine`), and
  **`java.io` / `java.nio` of any kind** (D8). A file larger than memory cannot
  be processed, and that limit is a consequence of having no `Iterator` rather
  than of anything in the file layer.
- A **`py`, `js` or `clj` provider**. protoScala routes all four family prefixes
  and reports `no provider registered for '<alias>'`; no runtime in the family
  registers those three aliases. `st` is the one prefix with a provider behind it
  (R5 below). For `py` the blockers are measured in INTEROP §6.1: no `py` alias, an
  environment resolved from a `thread_local`, a `ProtoSpace` that is a process
  singleton by design, and no `numpy` in protoPython at all. ROADMAP **Track Y**.
- A **wildcard import of a foreign module** (D92) and **lexical import scoping**
  (D96), which is the same question as scoping extensions (D82).
- **A cross-runtime CALL.** `import st.<module>` works and its values cross with
  no copy (R5 below), but *calling* a protoST method from protoScala does not: a
  protoST method is an object carrying `__bc_ptr__` that protoST's own engine
  interprets on SEND, not a `proto::ProtoMethod`. A foreign callable has to be a
  protoCore method. INTEROP §6 lists the other limits of what does work (one
  thread, one protoST runtime, a namespace snapshot).
- Supervision trees, `ExecutionContext`, actor timeouts and
  `Await.result(f, duration)` — not scheduled. An `await` waits forever; the
  shutdown reports any actor still parked on a future that never completed.
- Actor mailboxes on protoCore's `ProtoMPSCQueue` — Phase P2 (the `Mailbox`
  seam is in place; switching is a one-file change once protoCore merges it).
- `collect` and `PartialFunction` (D63), `Seq`/`Iterable` as traits (D65),
  `Ordering` (D62), `SortedMap`/`ListMap`, `Array` (D69) and regular
  expressions (D70) — see the Phase 3 deviations for what each would cost.

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
| 39 | `CONCAT` | `[v1 .. vn] -> [str]` | operand: n >= 2; each `vi` is converted with `toScalaString` and the pieces are joined with `ProtoString::appendLast`, an O(log n) rope join that copies neither side. `s"…"` and `raw"…"` lower to this (D54) |
| 40..63 | reserved | | Phase 1 additions |
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
| 80 | `CALL_KW` | `[f a1..an v1..vm] -> [r]` | operand: a KwSendSite constant (positional count, keyword names); `f(x = 1)` on a function value, a global `def` or a local one, which `SEND_KW` cannot express because it has no receiver. The keyword `ProtoSparseList` is keyed by the ADDRESS of the interned parameter-name symbol — protoCore's own convention (DESIGN §5.2) |
| 81..95 | reserved | | object model |
| 96 | `THROW` | `[v] -> throws` | `v` must answer the `@Throwable` marker; `null` raises `NullPointerException` |
| 97 | `RETHROW` | `[] -> throws` | operand: the local slot a `Finally` handler saved the in-flight value in. Also what a `catch` cascade emits when no case matches, so the exception continues outward rather than being replaced |
| 98..127 | reserved | | exceptions |
| 128..159 | reserved | | still reserved; Phase 5 shipped the actor surface as ordinary sends to native methods (plan Task 0 A0-11), so `SEND_ASYNC`, `ASK` and `AWAIT` were not needed. A dedicated opcode is a later optimisation to be justified by `perf stat -r 3` |

**Phase 6 added no opcode.** An import is resolved at compile time and binds a
name to a global that already exists, so the import site emits no instruction at
all: `import util.Strings` produces nothing, and a later use of `Strings` is the
`PUSH_GLOBAL` + `FORCE` an `object` read already compiles to. A member alias
compiles to the `SEND_APPLY` its qualified spelling compiles to. The lowest free
opcode is still **40**. A reader who has just read a UMD changelog will look for a
module opcode; there is none, and that is the design rather than an omission.

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

### Phase 1 deviations — approved by the maintainer on 2026-09-23

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

### Phase 2 deviations — approved by the maintainer on 2026-09-23

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
| D35 | With `case`, a generator filters exactly as Scala 3 does, for every pattern. **Without** `case`, a refutable pattern generator — `for (Some(v) <- os)` — is accepted and also filters, where scalac 3.9 rejects it ("pattern's type `Some[Int]` is more specialized than the right hand side expression's type `Option[Int]`") and asks for `case`. The one pattern the no-`case` form does not filter is a tuple pattern, which is trusted to meet tuples (types are erased): `for ((a, b) <- List(1, (1, 2)))` raises `MatchError` on the `1`. scalac rejects that program too, so there is no runtime answer to diverge from | — |
| D36 | For-comprehension desugaring differs where the result does not: `for (x <- xs; y = e)` emits two `map`s instead of dotty's single fused `map`. It produces the values scalac produces; only the number of intermediate traversals differs | — |
| D37 | An intersection type cannot be tested at run time: `case v: (A & B)` and `x.isInstanceOf[A & B]` are rejected at compile time with "this type cannot be tested at run time", where scalac 3.9 accepts them and tests both components. (The unparenthesised `case v: A & B` is a syntax error in scalac and is rejected here too, with a different message.) A parenthesised *tuple* type, `case v: (Int, Int)`, is accepted here and by scalac, and both test only the tuple's erasure | — |
| D38 | The default `toString` of an object with no user-written `toString` is `Name@<identity hash>`: a singleton `object O` prints `O@…` where the JVM prints `O$@…`, and printing a companion or a tuple companion directly (`println(Tuple2)`) prints `Tuple2@<hash>` | — |
| D39 | `List` hash codes differ from the JVM's, while staying consistent with `==` (equal lists have equal hash codes). Case classes, case objects, tuples and strings hash bit-identically to the JVM | — |
| D40 | Diagnostic wording: a `var` that redefines a concrete inherited `var` without `override` is reported as "cannot override a mutable variable", where scalac says it "needs `override` modifier". protoScala rejects `override` on a `var` outright, so the two messages describe the same rejected program from opposite ends | — |
| D41 | Scala 3's universal apply (a creator application: `C(args)` standing for `new C(args)`) is **not** synthesised for a plain class. `class C(val a: Int); C(1)` fails with `Not found: C`, where scalac 3.9 compiles it and prints `1`. Write `new C(1)`, or give `C` a companion with an `apply`. Case classes and case objects are unaffected: their companion `apply` is synthesised, so `C(args)` works | — |
| D42 | A `MatchError` names the Scala class of the unmatched value: `MatchError: 5 (of class Int)`, where the JVM names the boxed class (`scala.MatchError: 5 (of class java.lang.Integer)`). Consistent with D14 (unqualified class names) and D29 (one integer type) | — |

### Phase 5 deviations — approved by the maintainer on 2026-09-23

Decided by the implementing agent under the maintainer's standing authorisation
and reviewed by the maintainer on 2026-09-23
([DECISIONS-LOG.md](DECISIONS-LOG.md)). All are approved as recorded except
**D45** and **D47**, which the maintainer **overturned** on the ground of least
surprise for the Scala programmer; the rows below carry the replacement
behaviour. **D53** was added by the D47 ruling. **D44** is superseded: Phases 3
and 4 are to be completed, and D44's version implication is to be revisited when
they land. **D46** stands, and its stated precondition is now met — `ProtoMap`
shipped in protoCore 2.0.0 and is merged and released, so unanchoring is ready to
revisit; the behaviour is unchanged.

| Id | Deviation | Track |
|---|---|---|
| D43 | `await` suspends only a chain of protoScala frames each stopped at a call instruction. Inside a native higher-order method (`map`, `foreach`, `withFilter`, a `Future` continuation), or under an instruction that calls back into Scala without being a call site (`==` reaching a user `equals`, a `MatchError`'s `toString`), it raises `UnsupportedOperationException` instead of suspending; the ask's future receives that failure and the actor stays alive | later |
| D45 | An actor handler returns **either `(newState, reply)` or a bare `newState`**; the bare form means there is no reply to give and the ask's future completes with `()`. A `Tuple2` result is always read as the pair form, so an actor whose state is itself a pair returns it inside one. Only a handler that produces no value at all is rejected, with `IllegalArgumentException`. The actor keeps its previous state when the handler fails. **Overturned by the maintainer on 2026-09-23** (least surprise): the original D45 required the pair form strictly | (perm) |
| D46 | An actor lives as long as the session: it is anchored in a registry so the GC can reach it and everything it holds while it is only referenced by the (C++) ready stacks. `Future.apply` creates one actor per call | P1 |
| D47 | `Future.apply` takes its body **by name**: `Future(expr)` and `Future { … }`, as in Scala. By-name parameters are honoured where the compiler can name the callee (D53). **Overturned by the maintainer on 2026-09-23** (least surprise): the original D47 took a function, `Future(() => expr)` | (perm) |
| D48 | `map`/`flatMap`/`recover`/`onComplete` run their continuation on the thread that completes the future, or immediately on the caller when it is already complete — there is no `ExecutionContext`. A continuation may not `await` (D43) | later |
| D49 | `Thread` and `System` are runtime facilities, not the JVM's: `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`, `System.currentTimeMillis()`, `System.getenv(name)` (`""` when unset). `nanoTime` is a monotonic clock; only differences are meaningful | (perm) |
| D51 | `actor.value` reads the state without sending a message, so it may observe a state older than a send that is still queued (protoClojure's `@actor`) | (perm) |
| D53 | A by-name parameter is honoured only where the compiler resolves the call site to the declaration: a call by name to a top-level or local `def` (any parameter list, including a curried one), a method of the template being compiled, a method of an `object`, a class's primary constructor, and a builtin whose by-name signature the runtime declares (`Future.apply`). At any other call site — a method reached through a dynamic send, or a `def` taken as a function value — the argument is evaluated once at the call and each read of the parameter yields that value. scalac resolves all of these statically, so it stays lazy where protoScala does not | later |

**D44, D50 and D52 are retired by Phase 4** and their rows are deleted rather
than left to mislead. All three were provisional behaviours that existed only
because exceptions and `enum` had not landed: a failed `Future` carried a
`RuntimeError` case class rather than a `Throwable` (D44), awaiting a failed
future abandoned the rest of the handler (D50), and `Priority` was three integers
on an object (D52). `Failure` now carries the `Throwable` itself, a failed `await`
raises at its call site so an enclosing `try` catches it, and `Priority` is a
prelude `enum` whose ordinals are the scheduler's own band indices. Their ids are
not reassigned.

### Phase 3 deviations — recorded 2026-09-23, pending review

Decided by the implementing agent under the maintainer's standing authorisation
([DECISIONS-LOG.md](DECISIONS-LOG.md), "Phase 3"). The plan
([plans/2026-09-23-phase-3-collections.md](plans/2026-09-23-phase-3-collections.md))
escalated nothing: six of its items were **ruled** by the maintainer on
2026-09-23 and sixteen were **decided on cost** under the standing rule *follow
Scala where matching is cheap; document the divergence where matching would cost
real machinery and only unusual code could notice*. The "How" column says which.

| Id | Deviation | Plan item | How | Track |
|---|---|---|---|---|
| D54 | `s"…"` and `raw"…"` are compiled directly to the `CONCAT` opcode, so a user-defined `StringContext` is never consulted. The same input produces the same string; only a program that redefines `StringContext.s` sees a difference. (Every *other* interpolator does go through `StringContext` since Phase 4 — D56.) | A0-1 | ruled (R4) | (perm) |
| D55 | The `f` interpolator supports `%s %b %c %d %o %x %X %e %E %f %g %G %%`, the flags `-`, `+`, space, `0`, `,` (ASCII grouping) and `#`, and `width.precision`. `%n` is **not** supported — write `\n` — and there is no locale support, so the decimal separator is always `.`. Anything else is a compile error at the interpolation's position | A0-2 | on cost (C2): a locale means a locale database | later |
| D56 | **Retired by Phase 4.** It recorded that an interpolator other than `s`, `f` or `raw` was a compile error. Since extension methods landed, any other interpolator is lowered to `StringContext(<literals>).<name>(<args>)`, exactly as Scala lowers it, and supplied as an extension method on a prelude `StringContext`; an undefined one is a run-time `NoSuchMethodError` naming the member it looked for (D4). The id is kept with this note rather than reassigned | A0-3 | on cost (C3), then delivered | — |
| D57 | **Unused.** It was reserved for "a `Char` key and a numerically equal `Int` key are distinct". The maintainer's ruling C1 of 2026-09-23 removed that divergence — a `Char` is a value-equality key and `Map[Any, Int]('a' -> 1, 97 -> 1)` has one key, as in Scala — so there is nothing to record. The id is left unused rather than reassigned, because ids are stable references | A0-5 | ruled (R2) | — |
| D58 | `Map`/`Set` iteration — `toString`, `foreach`, `keys`, `values`, `toList`, `mkString` — yields **ascending-hash order**: deterministic for a given key set and across runs, and unrelated to insertion order or to Scala's. D7 already said the order is unspecified; this is its concrete consequence. Every conformance fixture that prints more than one entry sorts first, so the suite pins behaviour and never pins the order | A0-6 | on cost (C5): Scala guarantees no order either, so there is nothing to match; `SortedMap`/`ListMap` are a different type | (perm) |
| D59 | `Vector.hashCode` equals `List.hashCode` for the same elements, and a `Range`'s equals both — required for `Map(List(1) -> 1)(Vector(1))` to work. All three differ from the JVM's (D39, unchanged) | A0-7 | on cost (C6) | (perm) |
| D60 | **Unused.** It was reserved for `(0 until 3) == List(0, 1, 2)` being `false`. Decided on cost and reversed: an allocation-free `SeqView` — which *replaces* the helper the `Vector` work needed anyway — makes it `true`, as scalac answers, in about 58 lines with an O(1) length short-circuit. The id is left unused rather than reassigned | A0-8 | on cost (C1) | — |
| D61 | `to`, `until` and `by` require bounds that fit a `SmallInteger` and raise `IllegalArgumentException: a Range bound must fit a 54-bit integer` otherwise | A0-9 | on cost (C7): matching means boxing both bounds, an allocation on every `Range` method, to serve ranges of more than 2^53 elements | (perm) |
| D62 | `sorted` orders with the runtime's own comparison: numbers by exact value across `Int`/`BigInt`/`Double`, `Char` by code point, strings by content, booleans `false < true`. Any other pair raises `IllegalArgumentException: sorted needs comparable elements; use sortWith`. `sortBy(f)` orders by `f`'s results and `sortWith(lt)` takes the comparator; all three are **stable** (merge sort), as Scala's are | A0-10 | on cost (C8): matching needs `Ordering`, i.e. implicits (D3) — a whole subsystem, for an answer that is identical on numbers and strings | later |
| D63 | `collect` is not provided: `xs.collect(…)` fails with `NoSuchMethodError`. A `PartialFunction` needs the compiler to emit a second entry point per `{ case … }` literal. Phase 4 delivered the pattern-matched `catch` by REUSING `compileMatch`'s cascade rather than introducing a `PartialFunction`, so this is still open | A0-11 | ruled (R5) | later |
| D64 | **Unused.** It was reserved for `Try.apply` taking a function, `Try(() => expr)`. By-name parameters landed on `main` on 2026-09-23 (`bca0352`), so `Try { risky() }` works as in Scala and there is no divergence. The id is left unused rather than reassigned. Note that `Try(() => expr)` is still accepted and means what Scala means by it — a `Try` of a function value, `Success(<function0>)` | A0-12 | on cost (C9) | — |
| D65 | `Seq` and `Iterable` are **not** provided and are not scheduled: `case xs: Seq[_]` and `x.isInstanceOf[Seq[_]]` are rejected at compile time (`Not found: type Seq`), and `List`, `Vector`, `Range`, `Map` and `Set` share no common ancestor below `AnyRef`. LANGUAGE §4 used to promise them; the promise was removed rather than left to contradict the code | A0-14 | ruled (R6): ROADMAP's done-when governs | later |
| D66 | `%e`, `%f` and `%g` convert their argument to a `Double` first, so an integer above 2^53 prints rounded. `%d` is exact for a promoted `LargeInteger` (D1) and is what an integer wants | Task 4 | on cost (C16): matching means a bignum decimal formatter | later |
| D67 | Neither `Either` nor `Try` has `filter`/`withFilter`, so `for (x <- e if p)` over one fails with `NoSuchMethodError`. Scala's `Either.withFilter` needs a `Left` to fall back to; `Try`'s needs only an exception value, which Phase 4 now has, so the `Try` half is merely unimplemented rather than blocked | Task 5 | on cost (C15) | later |
| D68 | `Range.map`, `flatMap` and `filter` answer a `List`, where Scala answers an `IndexedSeq` (rendered `Vector(…)`). The elements and their order are Scala's. `Range.reverse` *does* answer a `Range`, as Scala's does, because that was cheap | Task 6 | on cost (C14): `IndexedSeq` is a `Seq` trait, which R6 keeps out of this phase | later |
| D69 | `String.split` answers a `List[String]`, not an `Array[String]`. There is no `Array` type (D12 already routes varargs to `List`) | Task 11 | on cost (C13) | (perm) |
| D70 | `String.split` splits on a **literal** separator, not a regular expression: `"a.b".split(".")` yields `List(a, b)` where Scala, reading the argument as a regex, yields `List()`. `split(",")` — the common case — is identical | Task 11 | on cost (C12): matching means a regex engine, a dependency this dialect does not have | later |
| D71 | A key that overrides `equals` but **not** `hashCode` is a value key that hashes by identity, so two `==` instances land in different slots and never find each other. Scala's `Map1`..`Map4` compare by `==` alone and so hide the classic `equals`/`hashCode` defect for the first four entries; protoScala is hashed from the first entry and exposes it at once. **At five entries and beyond scalac answers exactly what protoScala answers** (verified: a six-entry map of such keys gives `size=6` and a miss in both). Not foreseen by the plan, which expected no divergence here | Task 9 | on cost: matching means a second, unhashed small-map representation `ProtoMap` does not offer, to preserve a behaviour Scala itself loses at five entries and that only the classic bug can observe | (perm) |

**Three ids in the D54–D70 block are unused — D57, D60 and D64 — and stay that
way.** Two of the three were withdrawn because a ruling removed the divergence
they described and the third because by-name parameters landed. They are not
renumbered to close the gaps: ids are stable references, and three short notes
cost less than a renumber that invalidates every document already citing them.
D71 continues the numbering past the block.

**What did *not* diverge, and is worth stating because a reader will look for
it:** `Map` and `Set` keys follow Scala on every kind DESIGN §6.1 classifies,
including `Char` and parameterised `enum` cases (rulings C1 and C2); `Seq`
equality and hashing are cross-kind, so `List(1,2) == Vector(1,2) == (1 to 2)`
and a `Map` keyed by one is found by another; and `Try { … }` takes its body by
name. All three were verified against `tools/scala3-3.9.0`.

### Phase 4 deviations — recorded 2026-09-24, pending review

Decided by the implementing agent under the maintainer's standing authorisation
([DECISIONS-LOG.md](DECISIONS-LOG.md), "Phase 4"). The plan
([plans/2026-09-23-phase-4-exceptions-enums.md](plans/2026-09-23-phase-4-exceptions-enums.md))
escalated four items (E1–E4) to the maintainer; the maintainer was unavailable, so
each was decided here, implemented, and recorded in the decisions log with the
argument, how it was proved and what reversing it would cost. Everything else was
decided on cost under the standing rule *follow Scala where matching is cheap;
document the divergence where matching would cost real machinery and only unusual
code could notice*.

The plan reserved D71–D89 on the assumption that Phase 3 had taken D54–D70. Phase
3 in fact reached **D71**, so the whole block is shifted one place: Phase 4 uses
**D72 onwards**. **D78 is deliberately unused** — the plan proposed accepting an
`enum` case without its qualifier, and matching Scala costs nothing, so no
deviation was taken.

| Id | Deviation | Plan item | Track |
|---|---|---|---|
| D72 | A `finally` body that itself throws **replaces** the in-flight exception. Scala does the same and warns about it; protoScala has no warnings (D4) | A0-4 | (perm) |
| D73 | Exception class names are unqualified (`ArithmeticException`, never `java.lang.ArithmeticException`) and the hierarchy is the twenty-class one of §3.1 of LANGUAGE.md, not the JVM's. `catch { case e: java.io.IOException => }` does not compile: there is no `java` namespace (D8) | A0-6 | (perm) |
| D74 | A `std::logic_error` from a compiler or VM defect is deliberately **not** translated and **not** catchable: it reaches `main.cpp` as `protoscala: internal error: …`, so a bug in protoScala can never be masked by `catch { case e: Throwable => }`. `std::invalid_argument` and `std::out_of_range` are caught by their exact types, and no `std::logic_error` clause exists anywhere in the engine | A0-6 | (perm) |
| D75 | An actor suspended on a future that never completes never runs the `finally` of the `try` it suspended inside; the shutdown diagnostic reports how many actors are parked, and that is the only notice. Scala has no equivalent situation, and making it otherwise would mean running arbitrary user code during shutdown | A0-7 | (perm) |
| D76 | `super[T].m` accepts **any ancestor** in the receiver's linearization, where scalac 3.9 requires `T` to be a **direct** parent and rejects `super[A].m` for a grandparent ("A does not name a parent of class C", verified). `ClassInfo` stores the flattened linearization and no direct-parent list. Permissiveness, not a semantic mismatch: no program scalac accepts behaves differently here | A0-8 | (perm) |
| D77 | `enum` `values` answers a `List`, where Scala answers an `Array`. The elements and their order are identical; matching Scala needs an `Array` type this dialect does not have (D69) | A0-9 | later |
| D78 | **Unused.** It was reserved for "an `enum` case resolves unqualified as well as qualified". Scala requires `Colour.Red` unless the case is imported, and matching that costs nothing, so the divergence was not taken: `Red` on its own is `Not found: Red`, exactly as in Scala, and resolves unqualified only inside the enum's own body and its companion. The id is left unused rather than reassigned | A0-9 | — |
| D79 | A `derives` clause on an `enum` is parsed and **ignored**, as on every other template (D3): there are no type classes to derive. `enum` type parameters are parsed and erased like every other type, and an `enum` case that overrides a member of the enum class is not supported | A0-9 | later |
| D80 | A `class`, `trait` or `object` nested in a **`class`** or **`trait`**, a local class inside a block, and the anonymous-class form `new T { … }` are rejected with "classes, traits and objects must be defined at the top level of a file or in an object". Each captures the enclosing instance, which needs a per-instance class. Nesting in an **`object`** is implemented. This is the one item here a reasonably common Scala idiom touches, so ROADMAP records it for a later phase rather than leaving it only as a deviation | A0-10 | later |
| D81 | Because a named argument is bound in the **callee**, a typo in a parameter name is a **run-time** `IllegalArgumentException` (`f has no parameter named 'z'`) where scalac rejects it at compile time; likewise an argument given twice (`f received parameter 'a' twice`) and one left unfilled (`f is missing argument 'b'`). The platform is late-binding even where Scala is not, and that is what makes a named argument work at a call site the compiler cannot resolve. Late **detection** is accepted; every message names the method and the parameter, so silent failure is not | A0-11 | (perm) |
| D82 | An extension is **global and session-wide**: it is visible to every piece of code that runs after its definition, with no import scoping, where Scala's extensions are scoped like any other member. Scoping needs an import mechanism, which arrives with UMD in Phase 6 (escalation E4) | A0-13 | Phase 6 |
| D83 | An extension on a builtin type mutates that prototype for the whole session, so two units cannot define conflicting extensions of the same name on the same type. A collision with a member the type **already** has is refused (`extension: String already has a member named 'length'`), where scalac allows the shadowing and simply never reaches the extension: silently shadowing a builtin method would be unrecoverable within a session. Re-running the same definition replaces it | A0-13 | (perm) |
| D84 | `class C(a: Int)(b: Int)` has **one flat parameter list**, so `new C(1)(2)` and `new C(1, 2)` are the same call and `C.curried`-style partial application of a constructor does not exist. scalac accepts only the curried spelling | A0-14 | (perm) |
| D85 | `catch someFunction` is accepted and rewritten to `case e => someFunction(e)`, which is what Scala's `catch` of a **total** function means. A genuine `PartialFunction` that is not defined for the exception rethrows in Scala and raises `MatchError` here, so prefer `case` clauses | Task 2 | (perm) |
| D86 | `Throwable.getClass` answers the class's simple name as a `String`. Matching Scala needs `Class[_]` values — a new type with no other use in this dialect — and `.getClass` on an exception is almost always fed to string concatenation | Task 4 | (perm) |
| D87 | The cleanup a `return` inlines before its `RETURN` is **excluded** from its own `try`'s handler ranges: the `try` has already been left, so re-entering its own handler would re-raise a value it never saved. That fixes the reachable case (`finally-throws-on-return.scala`, verified against scalac). The **multi-level** variant is not implemented: with two or more nested `try` constructs, an inner handler may see an exception raised by an outer cleanup during a `return`. Matching exactly needs a per-construct hole stack keyed by nesting depth, for an interaction no reasonable program reaches | Task 3 | later |
| D88 | A default value may read a parameter of the **same** parameter list (`def f(a: Int, b: Int = a + 1)`), which scalac rejects — it requires the referenced parameter to come from a **previous** list, and that curried spelling works here too. Matching the restriction means tracking parameter-list boundaries Desugar has already folded into lambdas, to reject a program a reader finds perfectly clear | Task 9 | (perm) |
| D89 | A named argument on a **function value** binds against the names written in the function literal, where scalac rejects it because `Function2.apply`'s parameters are called `v1` and `v2`. Binding in the callee makes the literal itself the callee, so its own names are what a reader expects. Permissiveness only | Task 9 | (perm) |

**What did *not* diverge, and is worth stating because a reader will look for
it:** `throw` accepts only a `Throwable`, as in Scala, with no deviation id; the
`catch` cascade propagates an unmatched exception outward rather than replacing it;
`finally` runs on the normal, exceptional and `return` paths in Scala's order,
innermost first, with the returned expression evaluated before the cleanups;
`enum` ordinals count every case in declaration order and `values` covers only the
singletons, both as in Scala; arguments are evaluated at the call site in source
order however the names reorder them; a `case` of an `enum` requires its
qualifier; and the generated `valueOf` and `fromOrdinal` messages are scalac's own
sentences. All of it was verified against `tools/scala3-3.9.0`.

### Phase 6 deviations — recorded 2026-09-24, pending review

Decided by the implementing agent under the maintainer's standing authorisation
([DECISIONS-LOG.md](DECISIONS-LOG.md), "Phase 6"). The plan
([plans/2026-09-24-phase-6-umd-packaging.md](plans/2026-09-24-phase-6-umd-packaging.md))
escalated three items (E5–E7) to the maintainer; the maintainer was unavailable,
so each was decided here, implemented, and recorded in the decisions log with the
argument and what reversing it would cost.

The highest id in use before this phase was **D89**, so Phase 6 uses **D90
onwards**. **D57, D60, D64 and D78 remain deliberately unused** and were not
recycled.

| Id | Deviation | Plan item | Track |
|---|---|---|---|
| D90 | A module's top level runs **when it is imported**, during the importing unit's compilation, not lazily on first member access. Scala has no file-level modules, so there is nothing to diverge from; Python and JavaScript both run a module at import, and a module that exists for its effects (`import mylib.Setup`) would otherwise never run. A module stays loaded even when the importing unit then fails to compile or throws: its values live under keys that are never reused, so the leftover is unreachable, and Python behaves the same way | A0-1, A0-3 | (perm) |
| D91 | A module **is an `object`**: `util/Shapes.scala` becomes `object Shapes`, its classes are `Shapes.Point` with their companions, and its name comes from the **file** rather than from anything written inside it. A module may not define an `@main`, which is refused (`a module may not define an @main method`) rather than ignored, because a silently ignored `@main` would be a trap. An `import` and an `extension` written in a module stay outside that synthetic object — neither is a member of anything — so an extension in a module sees the module's globals and not its members | A0-2 | (perm) |
| D92 | A **wildcard import of a foreign module** (`import py.numpy.*`) is refused: a foreign object's attribute names cannot be enumerated through the API this runtime uses, and guessing a name set would fail silently later. The message names the working spelling (`import numpy.{a, b}`). Named selectors work | A0-4 | later |
| D93 | `given` selectors (`import M.given`, `import M.{given T}`, `import M.{a => _}`) are parsed and **ignored**, as every other given is (D3): there are no type classes to resolve, so nothing is bound and nothing is an error | A0-6 | later |
| D94 | A **foreign module binds no types**: `new`, a type pattern and `isInstanceOf` on a class reached through `py.`, `js.`, `st.` or `clj.` are unavailable, because a foreign value carries no `ClassInfo`. Its members resolve by name at run time, which is what DESIGN §5's type-mapping table already says ("other objects → dynamic objects") | A0-4 | (perm) |
| D95 | `--disassemble` **resolves imports**, and therefore runs the top level of every module the file imports: a file cannot be compiled without its imports, and an import is resolved by loading (D90) | A0-1 | (perm) |
| D96 | An `import` is **hoisted to its compilation unit**. A top-level import is processed before every other declaration and is visible for the whole unit; an import written inside a block or a template body is compiled where it is found and its binding **outlives that block**, so it is visible from there to the end of the unit. Scala scopes an import lexically. Lexical scoping needs a scope-aware name resolver the compiler does not have, and D82's extensions have exactly the same shape, so the two are scoped together or not at all | A0-6, A0-11 | later |
### Track F deviations — recorded 2026-09-25, pending review

Decided by the implementing agent under the maintainer's standing authorisation,
and under the maintainer's two rulings for this track: **be faithful to Scala
when reading**, because `scala.io.Source` is real Scala standard library, and
**do not simulate `PrintWriter` when writing**, because it would drag in half a
stream hierarchy for nothing. Each row states what Scala does, what protoScala
does, and the cost that decided it. `Track F` in the "plan" column means there was
no written plan step: the decision is the agent's, and it is recorded with its
argument in [DECISIONS-LOG.md](DECISIONS-LOG.md).

| id | Deviation | Plan | Revisit |
|---|---|---|---|
| D97 | The exceptions file I/O raises are the JVM's **without the `java.io.` and `java.nio.charset.` prefixes** (D8), in the JVM's own shape: `IOException` extends `Exception`, `FileNotFoundException` and `CharacterCodingException` extend `IOException`, and `MalformedInputException` extends `CharacterCodingException`. `IOException` is deliberately **not** under `RuntimeException`: on the JVM an I/O failure is checked, and a Scala programmer writes `catch case e: IOException` expecting it to see a missing file *and* a bad byte. Which class each failure raises follows the JVM's rule, which is simpler than it looks: a failure of `open` is a `FileNotFoundException` whatever its errno (this is why the JVM reports "Permission denied" and "Is a directory" through that class), and a failure after the descriptor exists is an `IOException`. Verified against scalac 3.9.0 for a missing file, a directory, a file with no read permission and a file of invalid UTF-8 | Track F | (perm) |
| D98 | A failure **message** keeps the JVM's `<path> (<reason>)` shape, but the reason is spelled out in **English** from a table of errnos instead of taken from `strerror`, which is localised — on the development machine the JVM reports `sample.log (No existe el archivo o el directorio)`. An errno outside the table keeps `std::strerror` and is therefore locale-dependent; the table covers every errno these operations can produce. `MalformedInputException`'s message also diverges: it names the path and the byte offset (`bad.bin: malformed UTF-8 input at byte 1`) where Scala's says only `Input length = 1`, because a message that does not name the file is of no use when several were read | Track F | (perm) |
| D99 | **UTF-8 is the only encoding.** `Source.fromFile(path, enc)` accepts the second argument so the common Scala spelling compiles, and accepts only a name for UTF-8 (`UTF-8`/`UTF8`, any case); any other name — including one the JVM supports, such as `ISO-8859-1` — raises an `UnsupportedOperationException` naming it. Refusing is the point: decoding Latin-1 bytes as if they were UTF-8 hands the program plausible nonsense. Decoding is **strict**, as the JVM's is: an overlong form, a surrogate code point, a value above U+10FFFF and a sequence truncated at end of input are all `MalformedInputException`, never replacement characters. There is no implicit `Codec` because there are no implicits (D3) | Track F | later |
| D100 | **`getLines()` answers a `List[String]`**, not an `Iterator[String]`: protoScala has no `Iterator` at all. `getLines().toList` therefore also works and means the same thing, since `toList` on a `List` is the identity. Consequences: a `Source` is not an `Iterator[Char]` either, so `src.next()` and the character-wise `src.toList` are unavailable; and nothing streams — the file is read whole, so a file larger than memory cannot be processed. How lines are split matches Scala exactly and was verified row by row against scalac 3.9.0: on `\n`, `\r\n` and a lone `\r`, the terminator stripped, a trailing terminator adding no final empty line, and an empty file answering no lines rather than one empty one | Track F | later |
| D101 | **A `Source` may be read again.** Scala's is consumed as it is read: `src.mkString` followed by `src.getLines()` answers `List()` on the JVM (verified against scalac 3.9.0), because the underlying iterator is exhausted. protoScala reads the file when `fromFile` opens it and answers the same thing however often it is asked. Cost of matching Scala: a cursor and a consumed-ness flag, to reproduce a behaviour that is a reliable source of bugs — strictly more programs work this way, and only a program relying on exhaustion can tell. What does **not** change is that a **closed** source is closed: `close()` could have been a no-op, since nothing is held open, and it is not, because a program that reads a source it has already closed has a bug and should be told (`IOException`, message `<origin> (Stream Closed)`, where Scala's says only `Stream Closed`) | Track F | (perm) |
| D102 | **Writing is protoScala's own surface, not a simulated `PrintWriter`.** Scala's canonical writers are `java.io.PrintWriter` and `java.nio.file.Files`; protoScala has no Java interop and will not have one (D8), so there is nothing to imitate, and imitating a `PrintWriter` would mean inventing a `Writer`, a stream hierarchy, a `flush` and a buffering policy so that one line of user code could look familiar. Instead: `FileIO.write(path, text)`, `FileIO.append(path, text)`, `FileIO.exists(path)`, `FileIO.delete(path)` — four operations, one call each. `write` replaces the whole file; the text is encoded as UTF-8, which is what `Source.fromFile` reads back; neither `write` nor `append` creates parent directories. `exists` answers `true` for a directory and `false` for a dangling symbolic link, as `java.io.File.exists()` does. `delete` answers `true` when it removed a file and `false` when there was nothing at the path, and **raises** for every other failure — a permission denial or a directory reported as `false` would be a swallowed error rather than an answer | Track F | (perm) |

Two rules that are *not* deviations but decide what a program means, so they are
recorded here rather than left to be discovered:

- **An imported member is consulted after locals and members and before
  globals.** An inner scope wins over an import, as in Scala; an import shadows an
  outer binding, also as in Scala.
- **A name this unit declares and an import also binds is an error**, not a silent
  shadow: `'area' is both imported from Shapes and defined here; rename one of
  them`. Which of the two would otherwise win depends on the compiler's lookup
  order, and that is not a thing a program's meaning may rest on.

**What did *not* diverge, and is worth stating because a reader will look for
it:** the five import forms have Scala's meaning; `as` and `=>` are both accepted
as renames; `*` and `_` are both accepted as wildcards; a selector that names
nothing is an error rather than a silent no-op (`Strings has no member named
'nope'`); and the longest-dotted-prefix rule — `import a.b.C` tries module `a.b.C`,
then module `a.b` with member `C` — is what Scala's package-or-object resolution
means, with the miss message naming every path that was tried.

### Track X deviations — recorded 2026-09-25, pending review

Decided by the implementing agent under the maintainer's standing authorisation.
Track X's rule is narrower than Track F's, because Track X is driven by a
measurement against Scala's own tests: **where scalac's behaviour could be run,
it was run, and protoScala matches it**; a row below exists only where protoScala
*cannot* match, and says what the gap costs. The highest id in use before this
track was **D102**, so Track X uses **D103 onwards**; D57, D60, D64 and D78 remain
deliberately unused and were not recycled.

| id | Deviation | Plan | Revisit |
|---|---|---|---|
| D103 | **`assert`, `assume` and `require` are ordinary methods, not macros.** In Scala they are `inline` in `Predef`, so `-Xdisable-assertions` removes `assert` and `assume` from the bytecode entirely (it never removes `require`, which validates a caller's argument). protoScala has no macros and no `inline`, so there is no flag that elides them and an assertion always costs a call and a by-name thunk. What this changes for a program: nothing it can observe, except that an assertion cannot be compiled away, so a hot loop pays for one. What it changes for a *build*: `-Xdisable-assertions` has no analogue and is not accepted. Consequence of having no overloading (D31): each is **one** method with a default message rather than Scala's two overloads, and the default is a distinguished object, not `null`, so `assert(false, null)` still reports `assertion failed: null` as Scala's does | Track X | (perm) |
| D104 | **`App` is the entry point, with three restrictions Scala does not have.** `object Main extends App` runs the object's body, as in Scala (verified against scalac 3.9.0, including an object extending a trait that extends `App`), and protoScala runs it *after* the file's top-level statements, which Scala has none of (D9). The restrictions: (a) **one App object per file** — Scala allows several because a JVM launcher picks one by class name, and a script has nothing to pick with; (b) **not an App object and an `@main` in the same file** — the same argument; (c) **no App object in a module**, refused for the reason D91 refuses an `@main` there, because a module is imported and never run and a silently ignored entry point is a trap. All three are compile errors that name both candidates. `App` is deprecated in Scala 3 and is kept because it is what a decade of Scala teaching material writes | Track X | (perm) |
| D105 | **An `import` is a member import when its longest in-scope prefix names an object, a companion or an `enum`, and a module load otherwise.** Both halves are Scala-conformant; the *rule* is protoScala's, because Scala has no module-loading form to disambiguate against. Scala's own resolution has the same shape — a definition in scope shadows a package of that name — so a file defining `object util` and writing `import util.Shapes` gets its own object in either language. Two places this is narrower than Scala. (a) A **`val` cannot be a member-import prefix**: a wildcard has to enumerate the prefix's members and a dynamic value has no static type to enumerate (D4), so `import someVal.*` falls through to the module loader and its `ImportError` rather than guessing a member set. (b) A member import of a prefix **this same file declares** is resolved after the file's templates are described, so a class in that file cannot name, as a **parent**, a type reached only through such an import — `class Sub extends Base` after `import Holder.*` in the same file; `extends Holder.Base` always works. Everything else is Scala's: the four forms, `as` and `=>` as renames, `*` and `_` as wildcards, an `enum`'s cases and an object's nested templates under their simple names, an import losing to a local and shadowing an outer global, and a selector that names nothing being an error | Track X | (perm) |

### Track S deviations — the silent wrong answers the Scala 3 corpus found

A full run of the Scala 3 compiler's own `tests/run` corpus (1654 single-file
tests, corpus commit `a68b419c`) turned up 257 disagreements with scalac that no
deviation anticipated. Most were fixed; the rows below are what could not be,
and each one exists because protoScala's model cannot express what Scala's does.
Every row was measured against **scalac 3.9.0** with `bin/scalac -d out` and
`java -cp "$SCALA_HOME/lib/*:out"`, never `bin/scala`, and every row has a
fixture that prints both answers. Decided by the implementing agent under the
maintainer's standing authorisation. D106 and D107 are reserved by the Phase 7
plan, so this track uses **D108 onwards**.

| id | Deviation | Plan | Revisit |
|---|---|---|---|
| D108 | **An `override val` constructor parameter is invisible to an ancestor that declares the member in its *body*.** `class B { val y: Int = 10; println(this.y) }` with `class C(override val y: Int) extends B`: scalac prints 20, protoScala prints 10 and settles on 20 once the chain returns. A public member's attribute key is its plain name, so `B.y` and `C.y` are **one** slot, where scalac gives each class a field and overrides the accessor. Parameter fields, which Scala assigns before the superclass initialiser, are stored with `STORE_FIELD_IF_NEW` and therefore survive (`class B(val y: Int)` above prints 20, as scalac does), but the store of a *body* `val` cannot be guarded the same way: a subclass's own body `val` runs after the ancestor's and has to be able to overwrite it. The faithful fix is one field per class plus a virtual accessor for every `val`, which is a change to the object model, not to a constructor. Fixture: `tests/conformance/07-classes/override-val-over-ancestor-body-val.scala` | Track S | later |

## Known issues / platform dependencies

See DESIGN §11 for the full table. Unchanged this phase: R2, R4, R8. **R5 was
exercised for the first time in Phase 6** — see the three entries below it.

- **An imported member cannot be an assignment target — pre-existing, and now
  easier to hit.** `object Box { var counter = 0 }` then `import Box.*` and
  `counter = 5` fails with `Not found: counter`, because a selector import binds a
  name that rewrites to a *read* of the member (`Compiler::importedTermSelect`
  builds a `Select`) and the assignment path does not consult the import table.
  Verified to be **pre-existing**, not introduced by Track X: the identical
  program through Phase 6's module form — a module with a `var`, `import
  util.Box.*`, then `counter = 5` — fails the same way on the tree before this
  work. Track X only makes it reachable more often, because member imports of a
  same-file object are now the common case. The fix is to consult
  `importedTerms_` in the assignment path and emit the setter send, and it is not
  attempted here: it widens Phase 6's rewrite mechanism and belongs with D96's
  scoping work. One corpus test (`traits-initialization`) stops here.

- **`import someVal.*` is refused where Scala accepts it.** Scala allows any
  stable identifier as an import prefix, including a `val` of a known type;
  protoScala requires an `object`, a companion or an `enum`, because a wildcard
  has to enumerate the prefix's members and an erased `val` has no type to
  enumerate (D4, D105). One corpus test (`triple-quoted-expr`) stops here, and
  the message is the module-loader's `ImportError`, which names the paths it
  tried rather than saying "not an object" — clear, but not as pointed as it
  could be.

- **The Scala 3 run-corpus measurement, and what it says about our own tests.**
  The instrument is the Scala 3 compiler's own `tests/run` corpus (1654 single-file
  programs, dotty `a68b419c`), run one file per process against
  `build_release/protoscala`, scored by dotty's rule (match the `.check` file, or
  exit 0 when there is none). Two harness adaptations, neither a change to
  protoScala: a driver line is appended to call the test's `object X { def main }`,
  because protoScala runs a file rather than a class; and the corpus is triaged
  into three buckets, of which **bucket 3 (n = 601) is the in-scope set** — the
  files in which no rule finds a construct protoScala does not claim to have
  (Java interop, reflection, implicits/givens, `Array`, `Seq`, `Iterator`, …).
  Every rule names the construct it matched, and nothing is excluded from the run.

  | measurement | in-scope rate |
  |---|---|
  | 0.6.0, before Track X | **75/601 = 12.5 %** |
  | after the Predef surface (D103–D104) | **178/601 = 29.6 %** |
  | after member imports as well (D105) | **183/601 = 30.4 %** |

  Zero regressions: no test that passed anywhere in the 1654-file corpus before
  this work fails after it. Across the whole corpus, in-scope and out, 82 → 206.
  The member-import half is worth 5 in-scope tests on its own, which is small and
  was expected — its case is that it is the first thing a new user hits, not that
  it moves a number — and it also cut the in-scope `no module found` failures from
  45 to 37, the rest being `import scala.*` (D90/D91) and `Double.NaN`.

  **What this says about the suite:** all 1263 of protoScala's other tests were
  written here. 257 of the corpus's disagreements with real Scala were anticipated
  by no document in this repository, and the single largest of them — six missing
  Predef names — cost 103 in-scope tests and was invisible to a suite that had
  never needed them. A suite written by the implementer measures faithfulness to
  the implementer's model, not to Scala. The remaining 423 in-scope failures are
  attributed one reason each; the largest groups are syntax the parser does not
  accept (96), further stdlib names (52), `C(...)` on a plain class (D41, 34),
  classes nested in a class (D80, 34) and the absent `scala.*` namespace (D90/D91,
  33). Reproducing the measurement needs the corpus checked out, so it is not part
  of `ctest`.

- **An uncaught failure raised inside a prelude method reports the prelude's line
  number against the user's file name.** Found by Track F, pre-existing, not
  introduced by it, and not fixed here. Running the worked example without its log
  prints

  ```text
  examples/log-report/Main.scala:240: error: FileNotFoundException: sample.log (No such file or directory)
  ```

  Line 240 is `lib/prelude.scala`'s, where `Source.fromFile` calls the native; the
  **name** comes from the outermost compilation unit and the **line** from the
  innermost VM frame, and the two need not belong to the same file. The class, the
  path and the reason — everything a reader needs — are right; only the `file:line`
  prefix is misleading. The same mismatch has always applied to a failure raised
  inside an imported module (it reports the importing file's name with the module's
  line), so nothing about the diagnostic changed; Track F made it **common**,
  because `Source.fromFile` failing is the ordinary case rather than an unusual
  one. Fixing it means changing what D14's `file:line` prefix reports for every
  cross-unit frame, which is a change to a public surface and outside this track's
  brief. Recorded rather than left to be rediscovered; the fixtures that pin these
  failures match on the message and not on the prefix, deliberately.

- **R5 / two runtimes in one process: a cross-runtime import now works.**
  Phase 6 measured co-residency working and `import st.counter_lib` missing, and
  diagnosed the cause correctly: `ModuleProvider::tryLoad(path, ctx)` receives the
  *caller's* context, and protoST resolved its own runtime from `ctx->space` — a
  space it does not own when the caller is another runtime, since each runtime owns
  its own. Phase 6 then concluded the fix had to change the UMD contract. **That
  conclusion was wrong, and Track Y closed it with no protoCore change.** A
  `ModuleProvider` is an object with its own state, so a provider takes its runtime
  from that state and uses `ctx` only to allocate the result in the caller's
  context. protoST `e82682b` does exactly that; `provider:scala` still resolves
  through the space-keyed `moduleHostForSpace` and so still answers only
  protoScala's own callers — the same shape applies the day a foreign runtime
  imports a `.scala` module.

  What Track Y verified, in `tests/unit/protost_interop.cpp`
  (`umd/protost-interop`) and in protoST's own
  `tests/unit/test_cross_runtime_provider.cpp`:

  - `import st.counter_lib as lib` from a protoScala program resolves and runs;
  - `import st.counter_lib.Counter` binds the member through protoScala's
    `bindForeignMember`, and a member the module does not define is refused;
  - **no copy at the boundary**: the same protoST class read out of the namespace
    protoScala received and out of protoST's own globals is the same address with
    the same `getHash` from either side, with both addresses printed;
  - it survives a forced collection in *each* space, with the cycle counters
    asserted so a run that collected nothing fails rather than passing vacuously;
  - an import from a thread other than the one that constructed the protoST
    runtime is refused with a message (protoST D26), and two `STRuntime`s in one
    process make the choice ambiguous and are likewise refused.

  A second per-space trap was found and fixed on the way: an attribute key is the
  address of an interned symbol and protoCore interns **per `ProtoSpace`**, so the
  module namespace has to be rebuilt with keys interned in the caller's space.
  Only the mapping is rebuilt — the values are the foreign objects themselves.
  Because protoCore embeds a short string in the pointer word, a 5-byte member name
  matched across spaces by accident and a 7-byte one missed silently, which is why
  the tests use `Counter`.

  What is still *not* demonstrated: a cross-runtime **call**, imports from more
  than one thread, more than one protoST runtime, and a namespace that changes
  after import. INTEROP §6 states each. R5 itself — whether co-residency is
  supported — remains the maintainer's call; this is evidence for it, not a ruling.
  `-DPROTOSCALA_PROTOST_INTEROP=OFF` builds the suite with no reference to the
  protoST tree.
- **R5 / design note: a module is a process-level entity, and the anchor that
  makes cross-space sharing safe.** The maintainer ruled on 2026-09-24 that a
  module is loaded **once per process**, that the module list is **global and
  therefore perennial**, and that a module anchors its own contents through its
  variables — so a module's classes and values are reachable from a perennial
  root wherever their cells happen to live, and a loaded module is not owned by
  a space. There is no cross-space GC edge to reason about; the anchor *is* the
  mechanism. Recorded here because the implementation is a **local approximation
  of that platform rule**, and the difference is what a future host could break.
  Read from the code on 2026-09-24:

  - **protoST's side is per-runtime, not global.** `STRuntime::importModuleFile`
    anchors the module with `registryAdd`, which stores it in `liveRegistry` — a
    mutable object pinned once in the `ProtoRootSet` the runtime creates **on its
    own `ProtoSpace`** (`space.createRootSet("protoST-async")`) — and the classes
    it declares also live in the runtime's `globals`. Both are roots for the life
    of the `STRuntime`, which is why the sharing is safe today.
    `STRuntime::Impl::moduleCache` is a `std::map` of raw pointers and is **not** a
    root: it provides identity (one load per canonical path), not retention.
  - **protoCore's global list is global but is not itself a root.**
    `SharedModuleCache` (`core/ModuleCache.cpp`) is a process-wide
    `std::map<std::string, const ProtoObject*>`, and the collector's root
    collection (`core/ProtoSpace.cpp`, Phase 2) does not scan it — it scans
    `space->moduleRoots`, the per-space root sets, the prototypes, the threads and
    the contexts. Perenniality is delivered instead by `getImportModule` pushing
    the module into the **calling** `space->moduleRoots`, once per importing
    space. So "global list" holds; "and therefore perennial" is implemented today
    as a set of per-space anchors rather than one perennial global root.
  - **A prefixed import reaches neither.** `Session::loadForeign` calls the named
    provider's `tryLoad` directly, so `sharedModuleCacheInsert` never runs and
    nothing is added to any `space->moduleRoots`; the module is not in the
    process-global list at all. Its retention is entirely protoST's two
    per-runtime roots above, and protoScala's own pinned `globals` hold the
    namespace object.

  **What a host could break:** a host that destroys the `STRuntime` while a
  protoScala `Session` still holds imported values drops the only anchor. The
  tests construct the `STRuntime` first so it is destroyed last; nothing in the
  code enforces that order. Under the platform rule the anchor would not depend
  on a runtime's lifetime at all.

- **R5 / whether a prefixed import should go through `SharedModuleCache`** —
  re-examined under the ruling above, and **reported, not changed**, because the
  mechanism is protoCore's and P3 makes that a maintainer decision. Phase 6
  bypassed `getImportModule` for prefixed imports on the ground that the cache
  "is keyed by logical path with no `ProtoSpace` component", treating that as a
  hazard. **That ground is void:** a module is process-level, so the path is the
  correct identity and a second runtime answering from the same entry is the
  intended behaviour, not a collision. Going through `getImportModule` would also
  deliver the ruling's model directly — one load per process, plus rooting in
  every importing space. Three things stand in the way, all of them protoCore's:

  1. **There is no way to ask for a named provider.** `getImportModule` walks the
     *calling space's* resolution chain. To reach protoST through it, protoScala
     would have to append `provider:st` to its own chain, and then an unprefixed
     `import counter_lib` would reach protoST too — which is exactly the implicit
     shadowing the prefix exists to make explicit.
  2. **The key drops the prefix, so two languages collide.** protoScala strips
     `st.` and would ask for `"counter_lib"`. With a `counter_lib.scala` also
     present, a process-global path key makes the two **the same module**, first
     load winning for both `import st.counter_lib` and `import counter_lib`. Path
     alone is the right identity only if the provider is part of it;
     `Session::loadForeign`'s own cache already keys on
     `providerSpec + "/" + logicalPath` for this reason. This is the one point
     worth a ruling.
  3. **It would not remove the per-space re-keying.** `getImportModule` builds its
     wrapper in the caller's context, but the module object inside still carries
     keys interned in the owning space, so the provider must re-key regardless.
     Making interning **global**, as the maintainer has proposed, is the change
     that would delete that workaround rather than relocate it.
- **Phase 6 / no runtime in the family registers `py`, `js` or `clj`.** protoScala
  routes all four family prefixes and reports
  `ImportError: no provider registered for '<alias>'` when the alias is absent,
  which is what a user sees for `import py.numpy as np` today. protoPython
  registers `native`, `python_stdlib`, `compiled` and `hpy`; protoJS and
  protoClojure register none; protoST registers `st` and is subject to the
  space-keyed limit above — which Track Y removed, so `st` is now reachable from a
  protoScala importer. The remaining cross-repository work is ROADMAP's **Track Y**,
  and `tests/conformance/23-named-arguments/foreign-python-keyword.scala` and
  `foreign-python-open-encoding.scala` stay `XFAIL` with their expected output
  recorded and their directives naming the measured blockers (INTEROP §6.1). The keyword convention
  itself **is** now exercised across a real provider boundary, by
  `tests/conformance/25-interop/stand-in-provider-keyword.scala` and its
  `-long-keyword` sibling.
- **Phase 4 / class prototypes are mutable.** An extension method is installed on
  the receiver type's prototype after the class exists, and every instance already
  created must see it, so `MAKE_CLASS` now builds a **mutable** shape. Measured
  with `perf stat -r 3` on this host: `object_tree` (131071 case-class objects)
  costs 1.393 vs 1.355 Gcycles, **+2.8 %**, with instructions +1.4 %;
  `attr_lookup` is 140.4 vs 138.1 Mcycles, inside its own ±6 % error bars. Both
  are within the phase's 3 % gate, and class *creation* is cheaper because the
  members now mutate one object instead of copying per member. Recorded rather
  than hidden: it is the one read-path cost this phase adds.
- **Phase 4 / the per-frame retry loop is free.** Measured the same way, by
  bypassing `runFrame` and comparing: `fib30` 1.2505 vs 1.3016 Gcycles (the retry
  loop is **3.9 % faster**), `attr_lookup` 122.1 vs 128.8 Mcycles (**5.2 %
  faster**), `tak` 91.4 vs 88.7 Mcycles (3.1 % slower). All three inside the
  noise of a busy host, and none of them a regression: one C++ `try` region per
  frame is free on the non-throwing path of a zero-cost-exceptions ABI, as
  expected.
- **Phase 4 / `finally` does not nest perfectly with `return`.** See D87: with two
  or more nested `try` constructs, an inner handler may see an exception raised by
  an outer cleanup during a `return`. The single-level case, which is the one a
  program reaches, is correct and verified against scalac.

- **Phase 3 / one entry cell per value-equality key.** DESIGN §6.1's scheme
  stores an identity key as the `ProtoMap` slot key itself, but a value key as a
  two-element `ProtoList` entry inside the slot (and a list of entries on a
  genuine hash collision). A `Map` of value keys therefore costs one extra list
  per entry over a hypothetical flat one — the price of Scala's `==`/`hashCode`
  semantics on a map whose keys the collector traces. `map_build` (50000 String
  keys) is the workload that shows it: 354.9 ms against CPython's mutable
  `dict` at 80.0 ms.
- **Phase 3 / `Map` and `Set` iteration is ascending-hash (D58).** Deterministic
  for a given key set and across runs, and unrelated to insertion order or to
  Scala's. Every conformance fixture that prints more than one entry sorts
  first, so the suite pins behaviour and never pins the order; a program that
  depends on the order is depending on something neither dialect promises.
- **Phase 3 / cold start — inside the budget on a load-3.9 host, and at the
  line.** Measured on the suite-v1 run: 24.43 ms (script) and 24.24 ms (REPL)
  for the RelWithDebInfo build, 22.71 ms and 23.72 ms for the Release build, all
  21/21 verified, at load average 3.90. That is inside DESIGN §1's < 25 ms, and
  it supersedes the Phase 5 entry below. It is not comfortable: an earlier
  measurement during this phase read 27.05 ms at load average 4.03, after the
  prelude grew by `Either` and the extended `Option`/`Try`. The figure is only
  meaningful with its load average beside it, and the prelude is compiled at
  every start-up, so each phase that grows it should re-measure.
- **Phase 5 / R1.** A C++ thread that polls an actor's state must reach a
  safepoint between polls, or a stop-the-world pause waits for it and every
  worker stalls; a Scala spin loop gets this for free because `JUMP_BACK`
  calls `ProtoContext::safepoint()` at every back-edge (Q21). The scheduler
  unit test's polling loop learned this the hard way under
  `PROTOCORE_HEAP_LIMIT_CELLS=20000`.
- **Cold start — MET at 0.6.0, and met by the precompiled prelude image.** Three
  rounds interleaved in one window, image and source path alternating, all twelve
  cases `verified=21`, load average 2.97 at the start and 2.67 at the end:
  **0.6.0 with the image, script 23.73 ms and REPL 23.89 ms**, against 25.65 and
  26.01 ms for the same binary with `PROTOSCALA_PRELUDE_NO_IMAGE=1`.
  `benchmarks/cold-start.sh` exited 0 in all three image rounds and 1 in all three
  source rounds, on both cases, which *is* the check. Both paths live in one
  binary, so the source row is what proves the image — and not anything else in
  the release — moved the number; the source path is 0.5.0's, still missing by
  0.65 ms. The packaged `Release` build (what the `.deb` and the `.tar.gz` ship,
  a different binary from the canonical RelWithDebInfo one) was measured
  separately and also passes: 23.28 ms script, 23.73 ms REPL.

  **What this does not claim:** the worst sample is still above the target.
  `run_benchmarks.py` applies a stricter per-sample verdict and records all four
  0.6.0 cases as **STRADDLES** — median below 25 ms, spread crossing it — on a
  daily-driver desktop. The budget is met on the measure DESIGN §1 and the
  done-when use; the tail is not yet quiet, and that is recorded rather than
  rounded away.

  What the image cannot remove is `linkSymbols` (342 µs) and *running* the
  compiled prelude (177 µs), by construction: the tables hold strings and PODs, so
  the symbols must be interned into a `ProtoSpace` and
  `MAKE_CLASS`/`MAKE_FN`/`STORE_GLOBAL` must execute. Removing those needs a
  **protoCore space image**, which does not exist — no object-graph serialisation,
  no image and no snapshot API in `headers/protoCore.h` or `proto_internal.h`,
  and the only caching facility is the in-process `SharedModuleCache`, which holds
  live objects and cannot cross a process. That stays a P3, maintainer-owned
  question (escalation **E5**), no longer as a blocker: the budget is met with
  about 1.3 ms of headroom on the script case, which is what the next twenty
  prelude classes would spend. Full tables in `benchmarks/RESULTS.md`.

- **Cold start after Track X — the delta is below the noise floor, and the budget
  could not be re-certified on this host.** Track X adds four prelude declarations
  that carry start-up work: `AssertionError`, `NotImplementedError`,
  `object __NoMessage` and `trait App`. At the recorded marginal cost of ~60 µs per
  prelude class that is ~240 µs, and the measurement cannot see it. Three rounds
  interleaved in one window, alternating a binary built from `b7f6afd` (the tree
  before Track X) with the complete Track X binary, both from `build_release` so
  nothing but the code differs, all twelve cases `verified=21`, load average 5.47
  at the start and 6.45 at the end:

  | | before (`b7f6afd`) | after (Track X) |
  |---|---|---|
  | script, median of three medians | 25.99 ms | 25.30 ms |
  | script, best of three minima | 21.90 ms | 21.60 ms |
  | repl, median of three medians | 26.15 ms | 25.83 ms |
  | repl, best of three minima | 22.74 ms | 22.82 ms |

  The Track X binary measures *faster* on three of the four statistics, which is
  not a claim that it is faster — it is what "below the noise floor" looks like.
  Within-binary spread across rounds is 1.2–2.5 ms, an order above any difference
  between the two. So Track X did not spend the 1.3 ms of headroom the 0.6.0
  measurement recorded, in any way this instrument can see.

  **What this does not claim, and must not be read as:** the 25 ms budget was
  *not* re-verified. `benchmarks/cold-start.sh` exits **1 for both binaries** in
  most rounds, including the unmodified `b7f6afd` tree that the recorded 23.73 /
  23.89 ms figures came from — the host was running a second agent's build
  throughout, at load average 5.5–6.5 against 2.97 for the 0.6.0 measurement, and
  every median is 1–3 ms above the recorded one. The honest statement is
  therefore: **the budget is neither confirmed nor refuted here, and Track X is
  not what would have broken it.** Re-certifying it needs a quiet host, and until
  someone runs it on one the 0.6.0 verdict above stands unrefreshed. Raw samples:
  `../.agent_scratch/predef-import/coldstart-interleaved-final.txt` (and
  `coldstart-interleaved.txt` for the Predef half alone, measured at load 5.6).

  The 0.5.0 history is kept below, because the row it explains is still in the
  table and because the attribution is what told this phase what to build.

  The < 25 ms budget (DESIGN §1) was re-measured at 0.5.0 with the 0.4.0 and
  0.5.0 binaries **interleaved in one window** — 0.4.0 built from `941f577` in a
  scratch worktree against the same protoCore — three rounds of 21 verified runs
  each, plus a third binary that is **not shipped**: 0.5.0 with twenty more
  exception classes of the same shape in its prelude.

  | binary | script median | REPL median |
  |---|---:|---:|
  | 0.4.0 (`941f577`) | 23.91 ms | 24.46 ms |
  | 0.5.0 (shipped) | 25.22 ms | 25.58 ms |
  | 0.5.0 + 20 probe prelude classes | 26.41 ms | — |

  Per-round script medians: 0.4.0 24.21 / 23.91 / 23.83, 0.5.0 25.53 / 24.98 /
  25.22, probe 26.60 / 26.05 / 26.41 — monotone in all three rounds. Phase 4 cost
  **+1.31 ms** and twenty further classes **+1.19 ms**, so at roughly **60 µs per
  prelude class** the prelude's growth from 156 to 200 lines (twenty exception
  classes, `StringContext` and the `Priority` enum) accounts for essentially the
  whole regression. That **rules out the engine's exception machinery**: the
  per-frame retry loop measured free with `perf stat -r 3`, and mutable class
  prototypes make class creation cheaper. The prelude is parsed, desugared,
  compiled and run at every start-up with nothing cached between runs, which is
  exactly what Phase 6's image removed. The earlier uninterleaved figures (26.31 / 26.98 ms against
  24.96 / 24.80, measured hours apart) had the direction right and the magnitude
  about twice too large.
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

**One, pre-existing.** `Mailbox.EightProducersLoseNothingAndDuplicateNothing`
aborts under `PROTOCORE_HEAP_LIMIT_CELLS=20000`: at Track F, 1247 of 1248 pass in
that configuration. It is **not** a Phase 3 or Phase 4 regression — it fails the same
way on `main` at `bca0352`, which was checked before the first Phase 3 commit and
again before the first Phase 4 one. It is Phase 5 code and was left undiagnosed
rather than fixed outside either phase's scope, but it is recorded here rather
than left to be rediscovered. Every other configuration is fully green: at
Track F, 1248/1248 plain, 1248/1248 at `PROTOSCALA_ACTOR_WORKERS=1` and at `=16`.

Not run in this phase either, and therefore still not claimed: the
ThreadSanitizer build of the Phase 5 plan's Task 8 Step 4. Phase 4 adds no new
concurrency mechanism — it extends the existing one — so it neither needs nor
supplies that evidence; it remains the first thing to run on a quiet host.

## History

See [CHANGELOG.md](../CHANGELOG.md).
