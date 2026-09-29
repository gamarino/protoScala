# Changelog

All notable changes to protoScala are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- **The actor scheduler no longer uses `std::counting_semaphore`.** libstdc++ 13's
  implementation can lose a wakeup: `_M_release` notifies only when the count was 0,
  so a waiter that lost the compare-and-swap for a permit can sleep on a stale
  non-zero count and miss every later release. In protoST (S19) this left a worker
  parked with a permit available and the runtime waiting on its join, about once
  per 1,000 launches under load. The scheduler's `work_` is now
  `runtime/Semaphore.h`, which sleeps only on 0 and notifies on every release.
  No hang was observed in protoScala; the hazard was the same code path.
  Tests: `Semaphore.*` in the unit suite.

### Added

- **The cross-runtime call, demonstrated rather than argued.** `proto_module_init` now
  returns a **module object** carrying one `proto::ProtoMethod` cell per exported
  top-level function, so another runtime on the same object kernel calls into a
  transpiled protoScala module with three protoCore calls and no protoScala anything:
  `createSymbol`, `getAttribute`, `asMethod`. `tests/interop/foreign_caller.cpp` is that
  caller — it includes `protoCore.h` and one shim header whose only `#include` is
  `protoCore.h`, and the harness **greps** it for the name `protoScala` and fails if it
  appears, so its independence is checked and not described. The host side is
  `Session::withModule`, which closes its guards **before** handing the object over: a
  caller that needed a protoScala guard must fail rather than pass for the wrong reason,
  and `gen::enterMethod` installs the linking host itself for the duration of each call.

  The exported cells are a *second* view of the same block, not a replacement — the
  function objects the module's own top level binds still carry the arity, method-ness
  and capture slots that nineteen places inside the runtime read. Only a `def` that
  captures nothing is exported, because a bare cell is called with no `self` and a
  capturing block reads its captures from `self`; a top-level `def` captures nothing by
  construction. Four calls are checked end to end — two `Int` returns, a `String`, and
  one that raises and crosses the boundary as a catchable C++ exception — against the
  values the **interpreter** produces from the same source, and four mutations
  (`tests/mutations/apply.py X1`–`X4`) each turn the case red.

  Two limits are on the record rather than left to be discovered. Exported cells mean
  **one `Session` per process**: a cell is a bare function pointer with nowhere to carry
  its host, so `gen::linkModule` refuses a second host instead of choosing between two
  preludes. And no *other runtime* has taken the call up yet — protoPython, protoJS and
  protoST would each need a change in their own repository. What is proven is the claim
  the phase set out to prove: the call needs protoCore and nothing else.
  ([`docs/INTEROP.md`](docs/INTEROP.md) §8)

- **`try` / `catch` / `finally` transpiles (D120): the frame has its retry loop.** One
  loop per **block**, not per `try`, with the resume `switch` inside the `try` — C++
  forbids jumping *into* a try block and permits jumping within one — so the handler body
  is re-protected by the same table, exactly as `ExecutionEngine::runFrame`'s `continue`
  re-protects it. That is what lets a frame catch a **second** exception: one raised by
  its own handler body, by a non-matching cascade's `RETHROW`, or by a `finally`.
  `20-exceptions/nested-try.scala` is that case and it runs transpiled; the deviation
  entry had measured the alternative at 11 fixtures red.

  The classification lives once, in `gen::handleCaught`, which re-raises the in-flight
  exception with a bare `throw;` and reproduces what `runLoop` and `runFrame` do together.
  So a generated file writes exactly **one** `catch (...)` — and a block with no protected
  region writes **none**, which `tests/cli/transpiler-cli.sh` checks in both directions.
  The `std::logic_error` arm that keeps D74 sits **after** `std::invalid_argument` and
  `std::out_of_range`, which derive from it and *are* translated by the interpreter;
  putting it first would make them escape as defects, which is mutation `R1`. The handler
  search runs **before** `materialise`, so the uncaught path allocates nothing, as an
  interpreted frame's does not. `pc` is assigned before every instruction that can raise
  and nowhere else, because the search reads it and a stale `pc` finds the wrong region.

  Fixtures: **704** of 922 now run transpiled (was 626), 157 excluded (was 235). Corpus:
  **176** of the 191 the interpreter passes (was 158), **95** checkfile-verified (was 91),
  still **0** divergences. What remains is 9 corpus tests for named arguments and defaults
  (D121) and 6 for imports (D123).

  One arithmetic note, because the numbers do not move monotonically: each reason code
  names the **first** refusal found, so closing D120 raised **D113 from 31 to 39** — eight
  of the 86 are `await` inside a `try`. The harness found them by failing, naming each one
  as "refused and not on the exclusion list", which is the bidirectional guard doing
  exactly its job.

- **A compiled module is importable: `CompiledModuleProvider` (alias `compiled`, GUID
  `protoScala-compiled-v1`).** `import util.Strings` finds `util/Strings.so` under
  `PROTOSCALA_MODULE_PATH` and then `<prefix>/<libdir>/protoscala/modules`, and
  `protoscala --version` prints the list it searched, because that list is the whole
  surface and without it a missing module and a mis-set path look the same.

  It is installed **after** `provider:scala`, so a `.scala` beside a `.so` still wins and
  installing a compiled module cannot change the meaning of an import that already
  resolved. It loads any conforming `.so`, generated or hand-written — only
  `proto_module_init` is required, the version accessor is optional — and refuses one that
  exports `proto_module_main`, because a script is not a module (D8), with the wording D91
  already uses on the source path. Under P3 its own GUID makes a compiled `util.Strings`
  and a source `util.Strings` two modules rather than two names for one.

  Two things it needed that the plan did not foresee. `proto_module_init` reaches
  `gen::currentContext`, which needs an active protoScala call context, and a compiled
  module is loaded from two places that have none — the **compiler**, while resolving an
  `import`, and protoCore's own resolver, when another runtime asks. The provider
  therefore carries its host's engine and layout and installs them for the initializer,
  which is the same handover `gen::enterMethod` makes for a foreign call and carries the
  same consequence: one protoScala Session per process while compiled modules are in use.
  And the handles are never `dlclose`d, because a module object published under P3 is
  rooted for the life of the process and its methods are code inside those libraries.

  **What it binds is a foreign module: late binding.** The `ExportsRec` tables that would
  carry a `ClassInfo` across and keep early type binding are **not built**, so a pattern
  match or a `new` against a compiled module's class does not compile. That is the one
  respect in which a compiled module is less than a source module, and it is in
  `PROTOSCALAC_SPECIFICATION` §8 rather than left to be discovered.

  Six behaviours in `cli/compiled-provider`, five in `unit/Provider.*`, and four
  mutations (`apply.py P1`–`P4`). One of the four is on the record because it found a
  hole: reversing the chain order left the shell test green, since `Session::load`
  consults the source loader itself and reaches the chain only on a miss — so the order,
  which decides for every *other* runtime, was untested until
  `Provider.CompiledComesAfterSourceInTheChain` asserted it on the chain's contents.

- **Classes, traits, objects, case classes and enums transpile (D118), and `super`
  with them (D122).** `gen::constFrom` rebuilds a `ClassSpec` from the generated static
  tables and the interpreter's own `makeClass`, `instantiate` and `superSend` do the
  work, so "one implementation, two consumers" still holds — nothing about class
  semantics is written twice. `object Main extends App` is supported through
  `gen::runApp`: for such a unit the program **is** the object's initialisation (D104),
  and calling an `@main` that does not exist had made four fixtures run their top level
  and print nothing.

  The effect on the two differentials is the reason this was the next task and not one
  of several. Fixtures: **626** of 922 now transpile, compile and run (was 360), with
  235 excluded (was 500). Corpus: of the 191 in-scope Scala 3 `tests/run` tests the
  interpreter passes, **158** pass transpiled (was 5) and **91** of those are verified
  against the corpus's own checkfile (was 1) — with **zero** divergences, in either
  direction, before and after. One change moved the fixture count 1.7× and the corpus
  count 31×, which is the clearest statement available that a fixture-only differential
  measures the fixtures.

- **`protoscalac`, a transpiler from protoScala to C++, and `libprotoScala.so` for
  it to link against (Phase 7, first cut — NOT the whole phase; see *Known
  limitations* below).** `protoscalac foo.scala --build-so` runs protoScala's own
  lexer, parser, desugarer and compiler and then emits C++ from the resulting
  `BytecodeModule` tree instead of running it; `make` builds it into a `module.so`;
  `protoscala --run-module module.so` loads and runs it. `ldd` names
  `libprotoScala.so.1` and `libprotoCore.so.3`, which is the honest statement of what
  loading such a module costs.

  **What it is for, so it is not mis-read.** A transpiled module's functions are
  `proto::ProtoMethod`s, and a `proto::ProtoMethod` is callable by any runtime in the
  family; protoScala bytecode is not. Track Y had already proved that *values* cross a
  runtime boundary without copying and that a foreign runtime **cannot call** a
  bytecode method. Cross-runtime *calls* are the capability this adds. It is **not** a
  performance feature — the generated C++ calls the runtime dynamically, so it removes
  the front end and the dispatch loop's `switch` and removes neither dynamic dispatch
  nor the cost of a send — and it is **not** justified by start-up, where the prelude
  image already took parse + desugar + compile (82 %).

  **One semantics, two consumers.** Every opcode body lives once, in
  `src/runtime/OpcodeOps.h`, and both `ExecutionEngine::runLoop` and the generated C++
  call it. The transpiler cannot drift from the interpreter because there is nothing to
  drift from, and a second implementation inside the emitter would be a defect
  regardless of whether it were correct.

  **P1 is structural here, not remembered.** The emitter tracks the operand-stack depth
  at emit time, so every stack access is a constant index into the frame's traced
  automatic locals and there is **no expression-temporary mechanism at all**;
  `tests/cli/transpiler-cli.sh` greps the generated output for a C++ local holding a
  value, and for a `catch`, and finds neither. `JUMP_BACK` emits `gen::safepoint(C)`,
  without which a generated loop reclaims nothing while looking healthy (P4 rule 1).
  `#line` directives point at the `.scala` file, so g++, GDB and LLDB report Scala
  lines.

  **The decision that made a correct first cut possible came from four measured wrong
  answers, not from reasoning.** A transpiled function object the runtime cannot
  recognise is a wrong answer with **no error**: `Try.apply` refused it; a `Map`'s
  arity-deciding `map` read its arity through `compiledModuleOf` and defaulted to 1, so
  `map { (k, v) => … }` was called with one argument; a by-name parameter was never
  forced, so a `Function` reached arithmetic; and eta-expansion reads `arity()` too.
  The fix is not a second callable shape: **a transpiled block IS a `BytecodeModule`**,
  one with no code words, carrying the real arity, variadic, method, paramless and
  capture-slot metadata, whose `nativeEntry()` is the block's `proto::ProtoMethod` and
  which `ExecutionEngine::execute` calls in one added branch. All nineteen
  `compiledModuleOf` call sites are then correct **unchanged**, because the metadata is
  where all nineteen already look.

  **Two harnesses, and the second exists because the first has a blind spot.** The
  fixture differential runs every one of the **921** registered conformance fixtures
  through transpile → `make` → `--run-module` and judges it against the fixture's own
  first-line directive: **921 pass, 0 fail**, of which **360** transpiled, compiled and
  ran, **61** were correctly rejected at compile time, and **500** are excluded as
  refusals with a reason code. Its three anti-rot guards are that a C++ compile failure
  is always a FAIL, that a refusal is a FAIL unless the fixture is listed, and that a
  **listed fixture which now works is a FAIL naming itself**.

  The corpus differential runs the Scala 3 `tests/run` corpus down both paths, because
  the fixture differential proves the transpiler agrees with the **interpreter**, not
  with Scala. Over the 601 in-scope tests the interpreter passes **191 (31.8 %)**, and
  of those the transpiler **refuses 186**, runs **5** — one checkfile-verified — and
  **diverges on none**, in either direction. **Zero divergences and almost no
  coverage**: the rule is satisfied vacuously, because a `tests/run` test *is*
  `object X { def main … }` and a class is exactly what the first cut refuses. So
  **the 360 is what our fixtures are made of, not a third of Scala** — which is the
  finding the fixture harness could not have produced.

  `docs/PROTOSCALAC_SPECIFICATION.md` is the specification of record. The build gains
  one shared library with `SOVERSION 1` and exactly **one** installed header,
  `include/protoScala/GeneratedModule.h`, which is the whole published surface.

- **`import Obj.*` works again (Track X), and that was a regression we made.**
  Phase 6 turned `import` into a module-loading form and, without recording it
  anywhere, removed plain Scala's member import with it: `enum Color: case Red,
  Green` followed by `import Color.*; println(Red)` failed with
  `ImportError: no module found for 'Color'`. `docs/LANGUAGE.md` §3.2 documented
  `import` only as file-modules and never said the ordinary form had gone. It is
  idiomatic Scala, it is what a first user hits in their first file, and it is
  back — with `import Obj.{a, b}`, `import Obj.a as b` and `import Obj as O`, on
  an `object`, a companion object or an `enum` as the prefix.

  **The rule that tells the two kinds of `import` apart:** the longest dotted
  prefix of the path that names something already **in scope** wins, and the
  import reads its members; if no prefix does, the path is a module to load,
  exactly as in Phase 6; a family prefix (`py.`, `js.`, `st.`, `clj.`) wins over
  both. Scala's own resolution has the same shape — a definition in scope shadows
  a package of that name — so a file that defines `object util` and writes
  `import util.Shapes` gets its own object in either language, and says
  `ImportError: util has no member named 'Shapes'` rather than silently reading
  the file. "In scope" means a term whose class the compiler knows: a wildcard has
  to enumerate the prefix's members, and a `val` has no static type to enumerate
  (D4), so `import someVal.*` is not a member import and falls through to the
  loader and its message. A prefix that is neither still gets Phase 6's
  `ImportError`, naming every path it tried (**D105**).

  Two things the implementation turns on. An `enum`'s cases and a template nested
  in an object are lifted to top-level definitions with **dotted** names
  (`Color.Red`), so they are not members of the companion and a wildcard that only
  walked the companion's member map would bind nothing — `import Color.*` reads
  both the members and the lifted names. And with no selector list the **last**
  path segment is the name being imported, so the prefix search leaves it:
  `import B1.B2` binds `B2`, not the unspellable `B1.B2`. That second one was a
  real bug in the first version of this change, found by writing the fixture for
  it: `import Holder.Even` bound nothing at all, and an `unapply` reached that way
  could not be used as a pattern.

  Verified against **scalac 3.9.0** by running the same program under both: the
  wildcard, named, renamed, `enum` and companion cases print byte-identical lines.
  17 conformance fixtures in `tests/conformance/28-member-imports/` plus one
  tutorial fixture, with 12 named mutations built and run and every fixture turned
  red by at least one. In-scope corpus rate **178/601 → 183/601 = 30.4 %**, and
  zero regressions anywhere in the 1654-file corpus. Two gaps this exposed are
  recorded rather than fixed, both pre-existing and both in STATUS.md: an imported
  member cannot be an **assignment target** (the module form has always had this),
  and `import someVal.*` is refused where Scala accepts it.

  **Cold start.** Track X adds four prelude declarations, ~240 µs at the recorded
  60 µs/class, and the measurement cannot see them: three interleaved rounds
  against a binary built from `b7f6afd` put the Track X binary 0.69 ms *faster* on
  the script median and 0.32 ms faster on the REPL median, against a within-binary
  spread of 1.2–2.5 ms. What must be said plainly is that the **25 ms budget was
  not re-certified**: `benchmarks/cold-start.sh` exits 1 for both binaries on this
  host, including the unmodified tree the recorded 23.73 ms came from, because a
  second agent was building throughout at load average 5.5–6.5 against 2.97 for
  the 0.6.0 measurement. Neither confirmed nor refuted, and Track X is not what
  would have broken it. STATUS.md carries the table.

  **Settled since: the budget is MET**, re-certified at 0.55 busy CPUs, and this
  entry's "not re-certified" was the right call on the host it had. See the
  *Changed* entry below.

- **The Predef surface (Track X): `assert`, `assume`, `require`, `???` and
  `App`.** None of them existed. Every Scala program that checks an invariant or
  leaves a body unwritten failed at the first line with `Not found: assert`, and
  nothing in this repository said so — the gap was found by running the Scala 3
  compiler's own `tests/run` corpus (1654 single-file programs, dotty
  `a68b419c`), not by our own tests — every one of which was written here and
  therefore encodes the same blind spot. (The count is deliberately not quoted:
  the argument is that *all* of them share our authorship, and the total moves
  with every fixture added. `ctest --test-dir build_release -N | tail -1`.)

  Exception types and message texts were verified against **scalac 3.9.0** and
  match it exactly, including the two details a shim gets wrong: a call with **no**
  message gets the bare prefix (`assertion failed`, never
  `assertion failed: assertion failed`), and a message of `null` is still a message
  and is reported as `null`. `assert`/`assume` raise `AssertionError`, `require`
  raises `IllegalArgumentException`, `???` raises
  `NotImplementedError("an implementation is missing")`; the two new exception
  classes extend `Error`, as Scala's do, so `catch case e: Exception` does not
  swallow a failed assertion. The message is by-name, so an assertion that holds
  never builds it. Scala's `assert` is a macro that `-Xdisable-assertions` can
  elide; these are methods, and nothing elides them (**D103**).

  `object Main extends App` now runs the object's body as the program, the way
  Scala's `App` does, rather than being a marker trait that silently does nothing.
  It is deprecated in Scala 3 in favour of `@main` — which protoScala already
  supported — and is kept because it is what a decade of Scala teaching material
  writes. Three restrictions Scala does not have, each a compile error naming both
  candidates: one App object per file, not an App object *and* an `@main` in the
  same file, and none in a module (**D104**).

  All of it is written in `lib/prelude.scala`, in protoScala, on no new natives:
  an assertion is a condition test and a `throw`, so there is nothing below the
  language to reach for, and the decisions that matter — which exception, which
  text — are then readable where a reader looks for them, which is the same
  argument Track F used for `Source` and `FileIO`.

  **What it bought, measured on the same instrument before and after:** the
  in-scope corpus rate went from **75/601 = 12.5 %** to **178/601 = 29.6 %**.
  15 conformance fixtures in `tests/conformance/27-predef/`, and each one was shown
  to be capable of failing: 11 named mutations of the implementation were built and
  run, and every fixture is turned red by at least one of them.

- **File input and output (Track F): a program can read its own input.**
  Reading is `scala.io.Source` under Scala's own names — `Source.fromFile(path)`,
  `Source.fromFile(path, enc)`, `Source.fromString(s)`, and `mkString`,
  `getLines()`, `close()`, `isOpen`. Writing is `FileIO.write` / `append` /
  `exists` / `delete`: four operations, one call each, and deliberately **not** a
  simulated `java.io.PrintWriter`, because there is no Java interop to build one on
  and imitating one would mean inventing a `Writer`, a stream hierarchy, a `flush`
  and a buffering policy (D102).

  Every behaviour of the reading half that can be checked was checked against
  **scalac 3.9.0** rather than assumed: which exception class each failure raises,
  the `<path> (<reason>)` shape of its message, and every rule for splitting lines
  — on `\n`, `\r\n` and a lone `\r`, the terminator stripped, a trailing terminator
  adding no final empty line, and an empty file answering no lines rather than one
  empty one. Two divergences are deliberate and documented: `getLines()` answers a
  `List[String]` because there is no `Iterator` (D100), and a source may be read
  again because the file is read when it is opened, where Scala's is consumed as it
  is read (D101).

  Errors were the point of the exercise. Every syscall's result is checked,
  including `close` on the write path, where some filesystems report a failed write
  for the first time; a short `write` loops rather than being mistaken for a whole
  one. Every failure raises a prelude `Throwable` a Scala programmer would think to
  catch, with a message naming the path and saying what went wrong:
  `IOException`, `FileNotFoundException`, `CharacterCodingException` and
  `MalformedInputException` join the hierarchy in the JVM's own shape, with
  `IOException` under `Exception` and not `RuntimeException` (D97). The reason in a
  message is spelled in English from an errno table rather than taken from the
  localised `strerror` (D98). UTF-8 decoding is strict as the JVM's is — an overlong
  form, a surrogate, a value above U+10FFFF and a sequence truncated at end of input
  are all refused rather than turned into replacement characters (D99). A path
  holding a NUL byte is refused rather than silently truncated at the NUL, which
  would have operated on a different file than the program named.

  44 conformance fixtures, and each one was shown to be capable of failing: 22
  mutations of the implementation were built and run, and every fixture is turned
  red by at least one of them.

- **Tutorial chapter 16, "Reading and writing files"**, dual-audience, with a
  conformance fixture for every runnable snippet and a check that each snippet is
  verbatim its fixture's body. For the Python and JavaScript reader it puts reading
  a file beside `open()` and `fs.readFileSync`, with the four things that do not
  carry over, and a table of the writing operations in all three languages.

### Known limitations

- **Phase 7 is incomplete, and what it does not do is still a list.** `protoscalac`
  refuses three things at transpile time, each with a named message and a source
  position, and never mistranslates: `import` (**D123**, 87 excluded fixtures and 6 corpus
  tests), `await` (**D113**, 39) and named arguments and default values (**D121**, 31
  fixtures and 9 corpus tests). The first cut also refused classes (**D118**), `super`
  (**D122**) and `try`/`catch`/`finally` (**D120**); all three are now supported.

  **D121 is the most valuable thing left**, and not because of its 31 fixtures: the
  corpus differential's `full`-shim row reads **0** solely because the harness's own
  Predef shim declares `def assert(cond: Boolean, msg: Any = "assertion failed")`.

  Two parts of the phase are **not built**: the `ExportsRec` tables that would let a
  compiled module be imported with **early type binding** rather than as a foreign
  module, and the measurement task. The exports tables were deprioritised on a measured
  basis and the basis is stated: no fixture and no corpus test depends on them, where the
  retry loop (D120) is 86 fixtures and 18 corpus tests. The version is deliberately **not** bumped to
  0.7.0: the phase has not shipped.

### Changed

- **The cold-start verdict is MET, and the `MISSED` correction published earlier
  the same day is withdrawn as a load artefact.** Two corrections in one day is not
  a good look, so the mechanism is on the record rather than glossed: the `MISSED`
  reading gated on a load **average** of 1.84 while its own `mpstat` never put the
  foreign load below **2.3 of 12 busy CPUs**. Re-measured at **0.55 busy CPUs**
  (`mpstat` idle ~95 %), three interleaved rounds, 21 runs per cell, **378 runs all
  verified**, `benchmarks/cold-start.sh` exiting **0 in 24 of 24 cells**: shipped
  `Release` **21.63 ms** (script) / **22.32 ms** (repl), `RelWithDebInfo`
  indistinguishable at 21.73 / 22.20 (0.10–0.12 ms apart against 1.5–12.7 ms
  within-cell spreads).

  **The control that settles it is the same binary at two loads**, not a rebuild:
  `build_rwdi/protoscala`, untouched since the loaded window, read **28.37 / 27.97 ms**
  there and **22.16 / 22.02 ms** here. Load was the variable, and 0.6.0's 23.73 /
  23.89 ms both reproduces and is beaten.

  **The durable lesson, which outlasts the number: a cold-start claim states
  MEASURED IDLE, never a load average.** A load average counts runnable *and*
  uninterruptible tasks and lags by design. The two verdicts differ by nothing but
  the host, and the wrong one gated on the wrong metric.

  Two operands stay, and one shrank. The harness's own floor is **4.67 ms** and is
  **not simply additive** — 42 bare runs average **19.5 ms** against the harness's
  21.6 ms median, so only about **2 ms** of it shows and the ~5 ms must **not** be
  subtracted to claim 16.6 ms. It remains **stated, not deducted**: the done-when is
  the script's exit status. Start-up is still **kernel-bound**, 0.172 s user against
  0.669 s sys, sys 3.9× user — a direction, not a defect. **E5's headroom corollary
  is restored with its arithmetic corrected:** about **3.4 ms** of margin on the
  script case, not the 1.3 ms the 0.6.0 entry claimed, so a protoCore space image is
  open on its merits rather than as a blocker. Full write-up:
  `benchmarks/reports/2026-09-26-quiet-window.md` §1.

- **The worked example (`examples/log-report/`) opens `sample.log`.** It was
  written around the absence of file I/O and carried the log twice, once as the real
  file and once as a triple-quoted string in `report/Sample.scala`, with a diff in
  `tests/cli/examples.sh` keeping the two in step. All of that is gone.
  `report/Sample.scala` is three lines. The log's path is the program's first
  argument, defaulting to `"sample.log"`, and the fan width moves to the second;
  the documented invocation becomes `cd examples/log-report && protoscala
  Main.scala`, because a data path is resolved against the working directory here
  exactly as on the JVM. The diff that guarded the duplication is replaced by a
  check of the property that matters: the program is run against an edited copy of
  the log and must produce a **different** report.

- The conformance runner honours `PROTOSCALA_RUN_CWD`, so a fixture that
  demonstrates file I/O can name its files the way a reader would (`"notes.txt"`)
  and still never write into the source tree. Unset, nothing changes.

### Fixed

- **`cmake --install --component protoScala` did not install `libprotoScala.so`.** In
  `install(TARGETS)` an option after an artifact keyword belongs to that artifact group,
  so the single trailing `COMPONENT protoScala` bound only to `PUBLIC_HEADER` and the
  library landed in the `Unspecified` component. The command `docs/INSTALLATION.md` tells
  users to run — and the one CPack uses per component — therefore installed `protoscalac`
  and the header but not the library they link, and an installed `protoscalac` died with
  `error while loading shared libraries: libprotoScala.so.1`. Broken since the shared
  library was added, and invisible because nothing in this repository had ever installed
  and then run the result. `COMPONENT` is now on every artifact clause, with
  `NAMELINK_COMPONENT` for the development symlink.

- **`protoscalac` could compile a module against a stale `protoCore.h` and produce one
  that linked, loaded and crashed.** The generated `Makefile`'s `-I<prefix>/include`
  *adds* to the compiler's default search path rather than replacing it, so on a prefix
  with no protoCore headers `#include <protoCore.h>` resolved to
  `/usr/local/include/protoCore.h` — a February copy of a different protoCore. The module
  built and linked cleanly and then segfaulted in `ProtoObject::newChild`, called from
  `gen::makeFn`, with no diagnostic. It now **refuses** when `protoCore.h` or
  `protoScala/GeneratedModule.h` is not on its own include path, prints the directories it
  searched, and names `PROTOSCALAC_INCLUDE_DIRS`. This is the include-path twin of the
  standing `ldd` rule about `/usr/local`'s stale library: the same hazard one step
  earlier, and it produces a crash instead of a link error.

  Both were found by Task 14 Step 5, which installs into a scratch prefix and runs what
  it installed. The step is the whole reason they are not still there.

- **The actor scheduler's second data race is fixed: `finishTurn` no longer reads
  shared per-actor state after releasing the claim.** Its non-suspended path used to
  CAS `sched` 1→0 and *then* call `hasWork`/`highestPendingBand`, which read
  `ActorState::pendingIdx` and walk the actor's `__pend<n>__` list — while the worker
  that claimed the actor next was writing both. The single-method invariant was never
  broken (no two workers in one turn; protoClojure's session-19 hole is absent); the
  narrower guarantee that `ActorState` is touched only by the claim holder was. It now
  decides **under** the claim, and on the work-known path keeps the claim and hands the
  actor straight back, exactly as the `s == 2` branch already did.

  **`pendingIdx` deliberately stays a plain `unsigned`.** Making it atomic was the
  obvious change and the wrong one: it would have silenced the single line TSan could
  see and left both real hazards — the `__pend<n>__` attribute race and a P1 hazard —
  exactly as they were. The P1 half is the one that mattered: `hasWork` held that list
  in a bare C++ local across `asList(ctx)->getSize(ctx)` while the new owner replaced
  the attribute, leaving it reachable from no root, no attribute and no automatic
  local — the shape `Mailbox.cpp:50-54` documents as a crash under a small heap. It was
  **not observed to crash** in 6 runs under ceilings of 20,000–500,000 cells, and that
  is recorded as *not demonstrated* rather than as absent.

  **Two things had to be got right before the evidence meant anything, and both were
  wrong at the first attempt.** The workload: the 8 × 25,000-send stress case that
  produced the original report almost never reaches the window, because with senders
  hammering one actor `sched` is 2 at the end of nearly every turn and the re-check
  happens under the claim — 0 firings at 4, 8 and 16 workers. Six sender threads
  trickling one message at a time over 64 actors enter it ~18,000 times a run. The
  detector: a temporary `inTurn` mark sampled at *entry* to `hasWork` reported 0
  overlaps in ~20,000 window entries, every run; `hasWork` loops over three bands and a
  second worker can claim part way through, so widening it to span the whole read
  produced **2 firings in 6 runs** — and its "two workers in one turn" branch never
  fired, which is what confirmed the invariant. ~20,000 entries with zero overlaps is
  the shape a too-narrow detector produces, not a clean bill.

  The proof is therefore a **differential on a workload that reproduces the race**,
  because the original report appeared once in four runs and its absence would prove
  nothing. Under TSan at 12 workers: **pre-fix 4 and 4** reports, all
  `ActorScheduler.cpp:237 in nextMessage` (`pendingIdx[band] = 1`, the sibling write to
  the `:227` first reported); **post-fix 0, 0, 0**; both printing the verified total.
  The stress case is also 0 protoScala-sited in three runs, printing `200000`.

  The `SparseListAlgorithms.h` population **survives**, as predicted in writing before
  the run, because a control of 8 plain threads touching no actor already produced 13
  of them — so this fix reassigns nothing to or from protoCore. New fixture:
  `tests/conformance/13-actors/concurrent-sends-no-race.scala`, which **has no mutation
  that turns it red and says so in its header** — the pre-fix code printed the correct
  total every run, because the cursor half is benign. All instrumentation was removed;
  `ActorScheduler.h` is byte-identical to its pre-experiment state.

- **The `Thread.start` data race ThreadSanitizer found is gone, and the global it
  lived in is gone with it.** `ActiveCallContext g_threadBlueprint` was a plain
  non-atomic global: the spawning thread wrote it, the spawned thread read it, no
  happens-before edge, and one global shared by every `Thread.start`. The engine
  and the layout now travel in the spawned thread's own argument list as two
  tagged `SmallInteger` addresses beside the handle — the mechanism
  `ActorScheduler::ensureStarted` already uses for its workers. They are written
  before `ProtoSpace::newThread`, so **thread creation is the happens-before
  edge**, and each spawn carries **its own** pair, so two spawns with different
  engines or layouts are each correct. Making the global's fields atomic would
  have silenced the report and still installed whichever value was written last;
  that is why it was not the fix. `Thread.start` now raises
  `IllegalStateException` rather than publish a null blueprint if no runtime is
  active.

  The proof is the sanitizer differential, not a fixture, and the reason is
  stated rather than skipped: R5 gives a process one engine and one layout, so no
  Scala program can distinguish the two designs, and a fixture that cannot fail
  without its fix is not evidence. Under `-DPROTOSCALA_SANITIZER=thread` against
  an instrumented protoCore, on the 8 × 25,000-send stress case — **before**: 3
  reports whose site is under `protoScala/src` (`ActorPrimitives.cpp:431` twice,
  `ActorScheduler.cpp:227` once) out of 989; **after**: **0** in each of three
  runs, out of 727, 770 and 805 — every remaining report has a protoCore site —
  and all four runs printed `200000`.

  *Correction, later the same day:* those three are the **stress log alone**, which
  that sentence did not say. Across all five logs there are **5** protoScala-sited
  reports and **2** distinct races: the plain-threads control run reports the same
  `g_threadBlueprint` global twice more, summarised at `ActorPrimitives.cpp:407`
  (the read side) rather than `:431` (the write side), which each report's own
  `Location is global … g_threadBlueprint of size 16` line settles. **The fix covers
  all four.** The control run was not repeated after the fix, so its zero is
  reasoned from the mechanism being gone rather than measured — stated rather than
  claimed.

  Two records corrected while doing it: the earlier claim that exactly **one**
  race named a protoScala site, and the claim that the actor scheduler produced
  no race of its own. Both were wrong; the missed report is
  `ActorScheduler::nextMessage` writing `ActorState::pendingIdx[band]` against
  another worker's read. It reproduced once in one run and in none of the three
  post-fix runs, is **not** diagnosed, and is **not** claimed fixed.

- **A cross-runtime import works: `import st.<module>` loads a protoST module,
  binds its members and shares its values with no copy at the boundary.** Phase 6
  measured this as a miss and concluded it needed a change to protoCore's UMD
  contract. It did not, and protoCore is untouched: a `ModuleProvider` is an object
  with its own state, so a provider takes its runtime from that state instead of
  from the caller's `ctx->space` — which is a space it does not own when the caller
  is another runtime — and uses `ctx` only to allocate the result in the caller's
  context. The provider change is protoST `e82682b`.

  Two per-space facts came out of it. A provider must run a module's top level in
  its OWN space, or the module's literals intern in the caller's symbol table and
  the provider's own later lookups miss. And it must rebuild the module namespace
  with keys interned in the CALLER's space, because an attribute key is an interned
  symbol's address and protoCore interns per `ProtoSpace`: a short name matched
  across spaces by accident (protoCore embeds it in the pointer word) and a 7-byte
  one missed silently. Only the mapping is rebuilt; the values are the protoST
  objects themselves.

  `umd/protost-interop` prints both addresses of the same class read from both
  runtimes, with the same `getHash` from either side, and asserts that it survives
  a forced collection in each space with the cycle counters checked. It also
  asserts what is refused: an import from another thread, and a process holding
  two protoST runtimes. `PROTOSCALA_PROTOST_INTEROP` now defaults to ON, so the
  test runs whenever protoST is found beside this tree.

  Unchanged, and stated in `docs/INTEROP.md` §6: a cross-runtime **call** does not
  work (a protoST method is `__bc_ptr__` plus protoST's engine, not a
  `proto::ProtoMethod`); `py`, `js` and `clj` still have no provider, and §6.1
  records the four measured reasons `py` could not follow `st`, including that
  protoPython ships no numpy. Both `py` fixtures stay `XFAIL`.

### Changed

- **`Future` takes its body by name** — `Future(expr)` and `Future { … }`, as in
  Scala, instead of `Future(() => expr)`. The maintainer overturned D47 on
  2026-09-23 on the ground of least surprise for the Scala programmer.
- **An actor handler may return a bare `newState`** as well as
  `(newState, reply)`. With no reply the ask's future completes with `()`, so `?`
  on such an actor is a `Future[Unit]`. A `Tuple2` result is still read as the
  pair form. The maintainer overturned D45 on 2026-09-23, same ground: a handler
  that only updates state should not have to invent a reply.

### Added

- **By-name parameters** (`x: => T`) on a `def` (any parameter list, including a
  curried one), a method, and a plain constructor parameter. The call site
  compiles the argument into a thunk and the body forces it on every read, so an
  argument used twice evaluates twice and one never used never evaluates —
  verified against scalac 3.9.0. New opcode `FORCE_THUNK` (38).
- **D53**: a by-name parameter is honoured only where the compiler resolves the
  call site to the declaration. A method reached through a dynamic send, and a
  `def` taken as a function value, evaluate the argument once at the call.
  Scala resolves all of these from static types.

## [0.6.0] - 2026-09-24

Phase 6: modules, UMD, the precompiled prelude and packaging. Built against
protoCore `983bbf98` (2.1.0) — **Phase 6 needed nothing new from protoCore**
(P3), and the one place it might have is recorded as an escalation rather than
patched.

### Added

- **`import` is a binding form.** It resolves at **compile time**, through a
  `ModuleLoader` seam the compiler holds a pointer to, which is what makes an
  imported class usable as a **type**: `new Point(1, 2)`, `case p: Point` and
  `case Point(x, y)` all compile. No `ProtoObject*` crosses that seam, so the AST
  and the compiler still hold no protoCore pointer (DESIGN §3.3).
- **A module is a `.scala` file reached by its path**, desugared into a synthetic
  `object` named after the file (D91), so Phase 4's nested-template lifting gives
  its classes their qualified names and their companions with no new mechanism.
  Its top level runs **when it is imported** (D90), once per canonical absolute
  path, with cycle detection and a failed load deliberately not cached.
- **All five import forms**, with `as` and `=>` both accepted as renames and `*`
  and `_` both as wildcards; `given` selectors parsed and ignored (D93). Modules
  are searched in the importing file's directory, then `PROTOSCALA_PATH`, then the
  working directory, and a miss names every path it tried.
- **`ScalaModuleProvider`** — alias `scala`, GUID `protoScala-source-v1` — so
  protoCore's resolution chain and another runtime can load a protoScala module.
  Registered once per process; its session is found through a `ProtoSpace`-keyed
  registry, never a thread-local, which would answer "module not found" on every
  actor worker. `provider:scala` is **prepended** to the chain, never substituted
  for it.
- **Prefix routing** for `py.`, `js.`, `st.` and `clj.`, from a closed four-name
  list. A prefixed import calls the named provider's `tryLoad` directly and does
  not go through `getImportModule`, because protoCore's `SharedModuleCache` is
  keyed by logical path with no `ProtoSpace` component.
- **Provider plug-ins**, `dlopen`'d from `PROTOSCALA_PROVIDERS` and from
  `<prefix>/lib/protoscala/providers`, with a two-symbol C ABI. protoScala ships
  none; `--version` reports what it found and where it looked.
- **The mandatory boundary catch shape** (`src/umd/ForeignBoundary.h`): six
  clauses in ROADMAP's order plus `catch (const std::logic_error&) { throw; }`
  before the `std::exception` arm, which ROADMAP's list omits and D74 requires —
  without it this template would have retired D74 silently. One unit test per
  clause.
- **`ImportError`**, one new prelude class. Within protoScala an import failure is
  a compile error; the class is what a caller in *another* runtime receives.
- **The prelude is compiled at build time.** `protoscala-precompile` emits static
  tables that a hand-written reconstruction walks, with no lexer, parser,
  desugarer or compiler in the start-up path. It is a **build product** with a
  CMake `DEPENDS` on `lib/prelude.scala`, not a cache, so it cannot go stale; a
  format version and an FNV-1a-64 of the source guard a hand-copied file and fall
  back to the source rather than failing. `PROTOSCALA_PRELUDE_NO_IMAGE=1` takes
  the source path, so both live in one binary.
- Tutorial chapter 15, the worked example, and two "protoScala in 10 minutes"
  sections in the README — one per audience, every snippet a fixture.

### Fixed

- **A native from a provider plug-in that threw a non-`std::exception` terminated
  the process.** `ExecutionEngine::callNative` gained the last-resort clause;
  `std::logic_error` still passes through untouched, so D74 survives.
- `GlobalTable::bind` and `aliasType` now seed the key counters. A name installed
  from the prelude image or from an import never goes through `declare`, so a REPL
  redefinition of a prelude name would have been handed the prelude's own key and
  **silently overwritten it**.
- A module could not import another module: `desugarModule` moved the import
  inside the synthetic object, where a template body *skips* `Import` nodes. A
  template-level import is now compiled rather than skipped, which was correct
  while `import` was parsed-and-ignored and is a silent trap now.
- `import M.given` swallowed the following line: `given` was missing from the
  layout pass's `canEndStatement`.
- Forcing a module's singleton segfaulted when its top level called a native: it
  is forced from the compiler, not from `run()`, so nothing had installed the
  thread's active call context.

### Packaging

The installer phase (merged 2026-09-23) delivered the CPack block, the DEB
generator, `install(TARGETS)`, the `INSTALL_RPATH $ORIGIN/../lib` that removes the
need for `LD_LIBRARY_PATH`, and `docs/INSTALLATION.md`. **The changelog never
recorded any of it; this entry is where that gap is closed**, not backdated into
0.3.0, because it landed after 0.3.0 was cut and a changelog that rewrites history
is worse than one with a gap.

0.6.0 adds what the installer phase left: the `.tar.gz` half was configured and
**never built**; the only artefact was at version 0.3.0; and `build_pkg/` was
untracked and unignored. Both artefacts now exist at 0.6.0, and both were
*extracted and run* under `env -u LD_LIBRARY_PATH`, with `ldd` confirming
libprotoCore resolved from inside the package rather than from a system copy.
The package also creates `lib/protoscala/providers`, so the path `--version`
prints exists on an installed system. `tests/cli/package.sh` re-runs that smoke
test on demand and skips, visibly, when there is no archive to test.

RPM, macOS DragNDrop and Windows NSIS/ZIP remain configured but never built,
which the installer phase recorded as its own deliberate gap.

### Known limitations

- **No runtime in the family registers a `py`, `js` or `clj` provider.**
  protoScala routes all four family prefixes and reports
  `ImportError: no provider registered for '<alias>'`; protoPython registers
  `native`, `python_stdlib`, `compiled` and `hpy`, and protoJS and protoClojure
  register none. `tests/conformance/23-named-arguments/foreign-python-*.scala`
  stay `XFAIL` with their recorded output and a directive that now names the real
  blocker. ROADMAP's **Track Y** is the cross-repository work. The keyword
  convention itself *is* now exercised across a real provider boundary.
- **A provider serves only callers that share its `ProtoSpace`.** Two runtimes
  were made co-resident in one process and both providers stayed reachable — R5's
  first real evidence since Phase 0 — but a cross-runtime import misses, because
  `ModuleProvider::tryLoad` receives the caller's context. This is the UMD
  contract, not a defect in either runtime.
- **A wildcard import of a foreign module is refused** (D92), and **imports are
  hoisted to their unit rather than scoped lexically** (D96), which is the same
  question as scoping extension methods (D82) and is decided with it.
- Nothing outstanding on DESIGN §1's cold-start budget: it is **met** at 0.6.0
  (script 23.73 ms, REPL 23.89 ms, against a target of 25 ms), which 0.5.0 missed
  by about 1 ms. **[Confirmed 2026-09-26: this claim stands. On a quiet host (0.55 busy CPUs) the
  script median is 21.63 ms and `cold-start.sh` exits 0 in 24 of 24 cells, so the
  23.73 ms both reproduces and is beaten. A `MISSED` reading published in between was
  a load artefact and is withdrawn; see the Unreleased *Changed* entry.]** Three rounds interleaved in one window, 21 verified runs per
  case; `PROTOSCALA_PRELUDE_NO_IMAGE=1` still measures 25.65 / 26.01 ms, which is
  what proves the image and not the release moved the number. What no protoScala
  change can remove is `linkSymbols` and *running* the compiled prelude
  (342 µs + 177 µs); that would need a protoCore space image, which does not
  exist. See `benchmarks/RESULTS.md`.

## [0.5.0] - 2026-09-24

Phase 4: exceptions, `super[T]`, enums, named and default arguments, extension
methods and templates nested in an `object`. Built against protoCore `983bbf98`
(2.1.0) — the same commit 0.4.0 was built against; **Phase 4 needed nothing new
from protoCore** (P3). Phase 5 shipped out of order as 0.3.0 and Phase 3 as
0.4.0, so Phase 4 is 0.5.0.

### Added

- **`try` / `catch` / `finally` / `throw`, with pattern-matched handlers.** A
  `catch` body is `compileMatch`'s own cascade with `RETHROW` instead of
  `MATCH_ERROR`, so a handler may use constructor patterns, guards and type
  patterns, and an exception no clause matches continues outward rather than
  being replaced. `try` is an expression. Both syntaxes.
- **The `Throwable` hierarchy** in `lib/prelude.scala`: twenty ordinary
  protoScala classes, the JVM's names without the `java.lang.` prefix (D73), so
  `case e: ArithmeticException` is the same per-class marker test as
  `case p: Point`.
- **Native error translation.** Every failure the runtime raises becomes a real
  exception value of the class its name means, materialised lazily so the uncaught
  path allocates nothing. A compiler or VM defect (`std::logic_error`) is
  deliberately **not** catchable (D74).
- **Exceptions across an actor turn and a suspended `await`.** A handler exception
  fails that message with the exception value itself and leaves the actor alive;
  a resumed frame runs with its handler table active; no `finally` runs on a
  suspension (D75); and a failed `await` raises **at its call site**, so
  `try { f.await } catch { … }` works across a cooperative suspension.
- **`super[T].m`**, which probes `T` itself first and then continues after it
  (D76).
- **`enum` and sealed hierarchies**, lowered entirely in the frontend to a sealed
  abstract class, one `case object` or `case class` per case, and a companion with
  `values` / `valueOf` / `fromOrdinal` — whose messages are scalac's own, byte for
  byte. `ordinal` is a `val` and `toString` is Product's, so `enum` needs no
  native method and no new opcode (D77, D79).
- **Named and default arguments** for Scala-defined methods, constructors,
  case-class `apply`/`copy`, function values and local functions, bound in the
  **callee** through protoCore's `keywordParameters` keyed by the address of the
  interned parameter-name symbol — the same convention a foreign callee will
  receive them by, documented in `docs/INTEROP.md` §7 (D81, D88, D89).
- **Extension methods**, installed as attributes of the receiver type's prototype
  so dispatch is the ordinary prototype walk (D6), global for the session (D82,
  D83) — and with them **custom string interpolators**, over a new prelude
  `StringContext`.
- **Templates nested in an `object`**, lifted to the top level with a qualified
  name and resolvable unqualified inside the object (D80 keeps `class`-nested,
  local and anonymous classes out).
- **Multiple constructor parameter lists**, concatenated into one flat list (D84).
- New opcodes: `CALL_KW` (80), `THROW` (96), `RETHROW` (97).
- Tutorial chapters 11 (Exceptions) and 12 (Enums and sealed hierarchies), plus
  new sections in chapters 2, 5, 6, 10 and 13; a conformance fixture for every
  runnable snippet.

### Changed

- **`Failure` carries a `Throwable`.** `RuntimeError` and `__mkRuntimeError` are
  gone from the prelude, and `Try`'s `recover`/`recoverWith` take a `Throwable`,
  so a failed `Future` and a `Try` both carry a value a `catch` clause can match.
  **D44 retired.**
- **`Priority` is a real `enum`** whose ordinals are the scheduler's own band
  indices; a plain `Int` band is still accepted. **D52 retired.**
- **A custom string interpolator** is an extension method on `StringContext`; an
  undefined one is a run-time `NoSuchMethodError` naming the member it looked for
  rather than a compile error. **D56 retired.**
- **Class prototypes are mutable.** An extension is installed after the class
  exists and every instance already created must see it. Measured cost:
  `object_tree` +2.8 % cycles, `attr_lookup` inside its error bars — both within
  the phase's 3 % gate, and class *creation* is cheaper.
- `Throwable.getClass` answers the class's simple name as a `String` (D86); the
  internal global `__classNameOf` supplies it, and `__installExtension` installs
  an extension on a prototype the compiler cannot name.

### Fixed

- A parameter **default** may read an enclosing local. It previously did so only
  when the body happened to read the same local — a local `def` is hoisted, so a
  default that captured an unboxed slot read `null`. The capture analysis now
  walks the defaults and a discarded pre-pass forces the callee's captures through
  the ordinary resolver, so the same program no longer works or fails for an
  unrelated reason.
- Phase 3's two `XFAIL` fixtures waiting on `enum` are flipped and verified
  against scalac; the prose of `map-enum-case-keys.scala` is corrected — a
  singleton `enum` case is a case object, so it is a **value** key, not an
  identity key. The two are indistinguishable at run time (a case object has one
  instance), which is why only the white-box test can tell.

### Performance

- No workload regression. The wall-clock suite could not answer the question on a
  host whose own CPython reference moved by 8-23 % between runs, so the two
  mechanisms this phase puts in hot paths were measured directly with
  `perf stat -r 3`: the **per-frame retry loop is free** (`fib30` -3.9 % cycles,
  `attr_lookup` -5.2 %, `tak` +3.1 %, all inside the noise) and **mutable class
  prototypes cost `object_tree` +2.8 % cycles / +1.4 % instructions**, inside the
  phase's 3 % gate and recorded rather than hidden.
- The **actor suite re-run on 0.5.0** (nine modes x six worker counts, 5
  interleaved samples, 450/450 verified, nothing killed) shows the two Phase 4
  mechanisms cost the scheduler nothing: `Priority`-as-`enum` leaves the High-band
  ask p50 at 27.2 µs (w=1) to 40.4 µs (w=16), against 26.7-41.9 µs when `Priority`
  was three integers on an object; `saturation-8` / `saturation-32` peak at 3.32x
  and 3.48x, at or above the previous `ProtoMPSCQueue` series, on a busier host.
  Report: `benchmarks/reports/2026-09-24-phase4-actors.md`.

### Known issues

- **Cold start is above the < 25 ms budget** by about 1 ms, and the cause is
  measured, not guessed: 0.4.0 and 0.5.0 interleaved in one window over three
  rounds of 21 verified runs give 23.91 ms against 25.22 ms (script) and 24.46
  against 25.58 ms (REPL), and a probe build with twenty *more* prelude exception
  classes costs a further +1.19 ms -- monotone in all three rounds. At roughly
  60 us per prelude class, the prelude's growth from 156 to 200 lines (twenty
  exception classes, `StringContext` and the `Priority` enum, all compiled at
  every start-up) accounts for the whole regression, which rules out the engine's
  exception machinery as the cause. Not claimed as met; a precompiled prelude is
  a Phase 6 decision.
- **The foreign half of named arguments is unexercised**: UMD is Phase 6, so
  `tests/conformance/23-named-arguments/foreign-python-*.scala` are `XFAIL` with
  their expected output recorded.
- `Mailbox.EightProducersLoseNothingAndDuplicateNothing` still aborts under
  `PROTOCORE_HEAP_LIMIT_CELLS=20000`, pre-existing on `bca0352`.

## [0.4.0] - 2026-09-23

Phase 3: fast paths, collections, the prelude and string interpolation. Built
against protoCore `983bbf98` (2.1.0), which supplies `ProtoMap` and the
hashed-collection helper. Phase 5 shipped out of order as 0.3.0, so Phase 3 is
0.4.0 and Phase 4 will be 0.5.0.

### Added

- **String interpolation executes.** `s"…"` and `raw"…"` compile to the new
  `CONCAT` opcode (39), which converts each piece with `toScalaString` and joins
  them with `ProtoString::appendLast` — an O(log n) rope join that copies
  neither side, so `s"$a$b"` on two strings allocates one node. `f"…"` compiles
  to a call of the native `__fmt` with every specifier a compile-time constant,
  so a malformed one is a *compile* error at the interpolation's position, as
  scalac reports it. A hole is parsed by the same Lexer/Layout/Parser pipeline
  as the file, so it may hold any expression, nested interpolations included.
- **`Vector`, `Range`, `Map` and `Set`.** `Map` and `Set` are built on
  protoCore's `ProtoMap` and are read and written only through
  `proto::hashedPut`/`hashedGet`/`hashedRemove`/`hashedForEach` with one
  `KeySemantics` whose callbacks are protoScala's own `scalaHash` and
  `valuesEqual` — so a `Map` can never disagree with `==`, and Scala's
  cooperative numeric equality makes `1`, `1L` and `1.0` one key. `Range` is
  arithmetic: `length`, `apply`, `head`, `last`, `sum` and `contains` are O(1)
  and it is never materialised except by an explicit conversion.
- **The full `List` surface**, and `Vector` sharing *one* implementation with it
  installed on both prototypes, so the two cannot drift; a result is of the
  receiver's own kind. `sorted`/`sortBy`/`sortWith` are a stable merge sort;
  `foldLeft`/`foldRight` accept both `xs.foldLeft(z)(f)` and `xs.foldLeft(z, f)`.
- **Cross-kind `Seq` equality and hashing**: `List(1,2) == Vector(1,2) ==
  (1 to 2)`, all three hash alike, and a `Map` keyed by one is found by another.
  Decided once, in `valuesEqual` and `scalaHash`, over one allocation-free
  `SeqView`, so `==` and `.equals` cannot split and
  `(0 until 1000000000) == List(1)` is O(1).
- **`Either`/`Left`/`Right`**, an extended `Option` (`fold`, `toRight`,
  `toLeft`, `orNull`, `forall`, `count`, `zip`, `iterator`, `toSeq`) and an
  extended `Try` (`map`, `flatMap`, `foreach`, `recover`, `recoverWith`,
  `orElse`, `toEither`), with `Try { … }` taking its body by name.
- **The `String` surface**: `split`, `replace`, `stripMargin`, `stripPrefix`,
  `stripSuffix`, `lastIndexOf`, `capitalize`, `equalsIgnoreCase`, `compareTo`,
  `toBoolean`, `format` (through the same formatter as the `f` interpolator) and
  the collection-like `toList`, `head`, `last`, `init`, `take`, `drop`,
  `takeWhile`, `dropWhile`, `map`, `filter`, `foreach`, `mkString`.
- **`->` on `Any`**, so `k -> v` is the `Tuple2` `(k, v)` — a case-class
  instance, never a `ProtoTuple`.
- **Benchmark suite v1** completed with `list_ops` and `map_build`, both printing
  the work they did and verified by the runner before any rate is computed;
  recorded in `benchmarks/RESULTS.md` with the machine, both commits and the
  load average.
- **Tutorial chapters 8 and 10**, with a conformance fixture per runnable
  snippet, and the two bridge chapters extended.
- **Deviations D54–D71** (D57, D60 and D64 unused — the divergences they were
  reserved for were removed by the rulings of 2026-09-23 and by by-name
  parameters landing). D71 is new and was not foreseen by the plan.

### Fixed

- The dispatch loop interned `unary_-`, `unary_!` and every binary operator
  symbol on **every execution**. All three families now read their interned
  names from `RuntimeLayout`: 1.630 → 1.466 Gcycles on a 200000-iteration
  operator-overload loop (`perf stat -r 3`), 10.0 % fewer.
- A name read inside an interpolation hole was never boxed as a capture, so
  `var v = 1; val f = () => s"$v"` would have read a stale value.
- The lexer scanned `$name` with the full identifier rule, in which `$` is a
  legal character, so `s"$a$b"` lexed as one hole named `a$b`; scalac reads it
  as two, which is what the parser now does.
- The 22 `-Wmissing-field-initializers` warnings the by-name work left behind,
  so the tree builds warning-free again.

### Changed

- `O(args)` on an object honours `apply`'s by-name parameters, as
  `O.apply(args)` already did. Without it `Try { … }` would have evaluated its
  block on the caller while `Try.apply { … }` would not, and DESIGN §5.1 makes
  the two the same call.
- `Map`/`Set` iteration is ascending-hash (D58) — deterministic for a given key
  set, unrelated to insertion order or to Scala's. Every fixture that prints
  more than one entry sorts first.

### Not done, deliberately

- A SmallInteger fast path on the `EQ`/`NE` opcodes. It was written, measured
  and backed out: `valuesEqual` already fast-paths two SmallIntegers, so it won
  nothing on its own workload (inside the error bars) and cost 8 % of
  `sum_loop`'s cycles through code layout in the hottest function. The
  measurement is recorded at the opcode.
- `Failure`'s payload still carries `RuntimeError`; Phase 4 re-points it and
  closes D44 and D50 with it.

## [0.3.0] - 2026-09-23

Phase 5: actors, priority bands and cooperative futures. Built against
protoCore `bf972d3f` (2.0.0). Phase 5 was implemented before Phases 3 and 4,
so the minor version goes from 0.2.0 straight to 0.3.0.

### Added

- **Actors** (DESIGN §8.1, §8.2): `Actor.spawn(state)(handler)` where a handler
  returns `(newState, reply)` (D45; a bare `newState` is accepted since the
  ruling of 2026-09-23, see [Unreleased]); `a ! msg`, `a ? msg`,
  `a.send(msg, priority)`, `a.ask(msg, priority)`, `a.value`,
  `Actor.isActor`, `Actor.stats`, the printed form `Actor(<state>)`.
- **Three priority bands** per actor (`Priority.High`/`Medium`/`Low`, D52),
  drained in strict priority order, eight messages per turn.
- **A GIL-free worker pool** of protoCore threads (`PROTOSCALA_ACTOR_WORKERS`,
  default `max(2, cores − 2)` capped at 16) over three lock-free ready stacks
  with ABA-tagged heads and a type-stable node pool, with spin-before-park
  inside a `ProtoContext::UnmanagedScope`. The pool starts on the first
  `Actor.spawn`, so a script that uses no actor pays nothing at start-up.
- **The single-method invariant**: one atomic per actor across claim, wake and
  suspension, so an actor never runs twice at once however many threads send
  to it. Checked by 8 threads × 25 000 sends at 1, 2, 8 and 16 workers.
- **Futures** (DESIGN §8.3): `await`, `isCompleted`, `value: Option[Try[T]]`,
  `map`, `flatMap`, `recover`, `onComplete`, `Future(() => e)` (D47; taken by
  name since the ruling of 2026-09-23, see [Unreleased]),
  `Future.successful`, `Future.failed`. Continuations run on the thread that
  completes the future (D48).
- **Cooperative `await` inside an actor**: the handler's call chain is
  snapshotted frame by frame during unwinding and rebuilt on resume, so the
  worker is released while the actor waits and the `await` workloads complete
  with a single worker. An `await` whose chain cannot be snapshotted is
  refused instead of corrupting the frame (D43).
- `Try`/`Success`/`Failure` and `RuntimeError(className, message)` in the
  prelude, moved up from Phase 3 (D44).
- `Thread.start(() => …)`, `t.join()`, `System.nanoTime()`,
  `System.currentTimeMillis()`, `System.getenv(name)` (D49).
- The seven actor benchmark modes of DESIGN §8.5
  (`benchmarks/actor-bench.sh`), each self-reporting the work it did and
  verified by the runner before any rate is computed, with a protoClojure
  comparison measured on the same machine and day.
- Tutorial chapter 13, "Actors and futures", with every snippet as a fixture.

### Changed

- `protoscala --version` now names the actor mailbox backend, so a benchmark
  report cannot misattribute its numbers:
  `protoScala 0.3.0 (actor mailboxes: CAS list)`.
- `~Session` joins every actor worker before the `ProtoSpace` is destroyed, on
  every exit path.
- `ExecutionEngine::execute` is split into a prologue and `runLoop`, and every
  re-entrant opcode records the in-flight call's base slot. No new opcodes: the
  128..159 range stays reserved (plan Task 0 A0-11).

### Known limitations

- The actor mailbox is the **CAS'd `ProtoList` fallback**, not protoCore's
  `ProtoMPSCQueue`: protoCore 2.0.0 does not carry `newMPSCQueue` yet. The
  `Mailbox` seam switches to it in one file once Phase P2 merges.
- No supervision trees, no `ExecutionContext`, no actor timeouts and no
  `Await.result(f, duration)`: an `await` waits forever, and the shutdown
  reports any actor still parked on a future that never completed.
- An actor lives as long as the session (D46), and `Future.apply` creates one
  actor per call.
- D43–D52 are recorded in `docs/STATUS.md` as provisional, pending the
  maintainer's review.

## [0.2.0] - 2026-09-23

Phase 2: object model, apply, for, match. Built against protoCore `e43fa2e4`.

### Added

- Classes: `val`/`var`/plain constructor parameters, fields, methods, auxiliary
  constructors, `extends`/`with`, abstract members, `override`, `final`,
  `sealed`; `private` enforced as a lookup restriction (D5).
- Traits with Scala's linearization, installed as protoCore parent chains
  (DESIGN §4.3), trait parameters, `super` calls including stackable traits
  (DESIGN §4.4).
- Objects (lazy singletons), companions, case classes and case objects with
  `apply`, `unapply`, `equals`, `hashCode` (equal to the JVM's), `toString`,
  `copy` (positional and named), `canEqual`, `productArity`, `productElement`,
  `productPrefix`, `_1`..`_N`.
- Tuples `Tuple2`..`Tuple22` as case classes (never protoCore tuples,
  DESIGN §4.6).
- The universal `apply` rule, `update`, setters, method values.
- Pattern matching: literals, wildcards, variables, typed patterns,
  constructor and tuple patterns, `::`, `List(a, rest*)`, alternatives,
  binders, stable identifiers, custom extractors, guards, `MatchError`;
  pattern `val`s; `{ case ... }` literals; `isInstanceOf`/`asInstanceOf`.
- For-comprehensions (generators, guards, value definitions, patterns; `yield`
  and `do`) over `List`, `Option` and any class with
  `map`/`flatMap`/`withFilter`/`foreach`; lazy `withFilter`.
- Placeholder syntax (`_ + 1`).
- The prelude (`lib/prelude.scala`): `Option`, `Some`, `None`.
- `List(...)`, `Nil`, `::`, `map`, `flatMap`, `filter`, `withFilter`,
  `foreach`, `length`, `tail`, `drop`, `mkString`.
- REPL: class, trait, object and case-class definitions, redefinition by
  shadowing.
- Benchmarks: `attr_lookup` (twin of protoPython/protoST) and `object_tree`
  (a deep immutable object graph); GC-pressure checks for object graphs.
- Tutorial chapters 6, 7 and 9; chapters 2, 3, 5 and 14 extended.
- Conformance fixtures for the object model, case classes, `apply`, lists,
  pattern matching and for-comprehensions (`tests/conformance/07-*` to
  `12-*`), each in a braces and an indentation variant.
- Benchmark suite: `benchmarks/comparable/*.scala`, Scala 3 twins (same
  algorithm and N) of the protoPython/protoST core workloads (`int_sum_loop`,
  `fib`, `str_concat`, `range_iterate`) and of protoClojure's (`tak`, `fib30`,
  `sum_loop`, `factorial_100`), each self-checking through an `// EXPECT:`
  line and valid for `scalac` unchanged. `benchmarks/bench.sh` /
  `run_benchmarks.py` run them interleaved against Scala on the JVM, CPython,
  protopy, protost and protoclj, verify every run's result, include
  `cold-start.sh`, and write dated reports to `benchmarks/reports/`. First
  results in the README "Performance" section. Every comparable file is also
  a CTest case (`benchmarks/<file>`).

### Fixed

Fixes from the final Phase 1 review, which had not been released yet:


- REPL: an indented construct typed line by line (`while i < 3 do`, then
  its body lines) is read to its end instead of running after its first
  body line; the input ends on a blank line or when a line returns to the
  first column without continuing the construct (`else`, `end`, ...).
- REPL redefinitions shadow as in the Scala REPL: earlier code keeps the
  binding it saw (per-definition global keys), a change of kind never breaks
  it, and an input that fails defines nothing (D25).
- Value discarding: a method declared `: Unit` (also `return e` in it, the
  innermost body of a curried one), `val v: Unit = e` and `(e: Unit)` yield
  `()`; `if` without `else` yields `()` when the condition is true (D26 for
  expected types from function types).
- Forward references follow Scala's block rule ("forward reference to value
  y extends over the definition of value x"); lazy vals are hoisted, so legal
  forward references to them work.
- `return` inside a method with several parameter lists.
- `@main` rejects non-String repeated parameters and curried methods; typed
  parameters are rejected as D27.
- Deeply nested source raises `StackOverflowError` instead of crashing; the
  lambda look-ahead is linear.
- `Char` supports `*`, `/`, `%`, `max`, `min`, bitwise, shift and unary
  operators like `Int`; string literals keep an embedded NUL.
- REPL: failed and Unit-valued inputs no longer use up a `resN`; String
  results are echoed in quotes; a UTF-8 byte-order mark is accepted.
- `benchmarks/cold-start.sh` formats numbers in the C locale.

### Changed

- The `ProtoSparseListObject` platform type is now called `ProtoMap`
  (`docs/platform/PROTOMAP-SPEC.md`).
- D1 also covers integer literals (no range limit).
- The cold-start target is now < 25 ms (was < 20 ms): the embedded Scala
  prelude adds about 1.2 ms to every start and the standard library will keep
  growing (maintainer decision, DESIGN §1).
- Companion objects are linked at compile time instead of through a runtime
  `__companion__` attribute, so class prototypes stay immutable (DESIGN §4.2).
- Class and trait membership is tested with a per-class marker attribute
  rather than protoCore's `isInstanceOf`, whose traversal caps give false
  negatives on flattened chains (DESIGN §5.3, `docs/platform/ISINSTANCEOF-FIX.md`).

### Deviations (provisional, pending maintainer review)

- D28–D42 ([docs/STATUS.md](docs/STATUS.md)); D5 and D10 extended.

## [0.1.0] - 2026-09-22

### Added

- **Lexer** for Scala 3 lexical syntax: alphanumeric, operator, mixed and
  backquoted identifiers; hard and soft keywords; decimal, hex and binary
  integers with `_` and `L`; floating-point literals; characters and strings
  with escapes; triple-quoted strings; interpolated strings as structured
  tokens; nested comments.
- **Significant indentation** (Scala 3 offside rule) and braces, mixable;
  `end` markers.
- **Parser and compiler** for `val`, `var`, `lazy val`, `def` (multiple
  parameter lists, varargs, `@main`), `if`/`then`/`else`, `while`/`do`,
  blocks, lambdas, closures with per-activation captures, `return` in
  methods, imports (parsed).
- **Bytecode VM** on protoCore with SmallInteger fast paths and
  arbitrary-precision promotion (D1), Java-style double printing,
  `StackOverflowError` instead of crashes.
- **Standard surface:** `println`, `print`, methods of Int, Double, Boolean,
  Char, String, List (varargs) and functions.
- **REPL** with readline history, multi-line continuation, `:help`, `:quit`,
  `:load`.
- **Tooling:** `--disassemble`; conformance, unit and CLI test suites;
  tutorial chapters 1–5 and 14.
- Design specification (`docs/DESIGN.md`), language reference
  (`docs/LANGUAGE.md`), roadmap, status tracker and interop design.
- Platform specifications for protoCore: `ProtoMap`
  (`docs/platform/PROTOMAP-SPEC.md`) and `ProtoMPSCQueue`
  (`docs/platform/PMQ-SPEC.md`).
- Actor model on protoClojure's design with the `actor-bench.sh` suite
  (DESIGN §8).
- Implementation plans for Phase P1 and Phase 1 (`docs/plans/`).
- Phase 0 skeleton: CMake build against protoCore, `protoscala --version` /
  `--help`, GoogleTest unit harness, conformance runner with `// EXPECT:`
  directives, CLI tests.
