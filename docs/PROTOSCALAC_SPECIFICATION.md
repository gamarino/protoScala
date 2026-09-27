# `protoscalac` — the protoScala-to-C++ transpiler

Status: **first cut**, Phase 7. This document is the specification of record for
`protoscalac`; where it and `docs/plans/2026-09-25-phase-7-transpiler.md` disagree,
this document is what shipped. It mirrors
`protoPython/docs/PROTOPYC_SPECIFICATION.md` in location and in section order, so a
reader who knows one finds the other.

Two sentences no other document in this repository states, and both are load-bearing:

> **The transpiler consumes bytecode, so its coverage is decided per opcode and the
> list in §8 is exhaustive rather than indicative.**

> **There is one implementation of every opcode, in `src/runtime/OpcodeOps.h`,
> called by both the interpreter and generated code. A second implementation inside
> the emitter is a defect regardless of whether it is correct.**

**What it is for.** A transpiled module's functions are `proto::ProtoMethod`s, and a
`proto::ProtoMethod` is callable by any runtime in the family; protoScala bytecode is
not. That is the capability this adds. It is **not** a performance feature — the
generated C++ calls the runtime dynamically, so it removes the front end and the
dispatch loop's `switch` and removes neither dynamic dispatch nor the cost of a
send — and it is **not** justified by start-up, where the prelude image already took
the large share.

---

## 1. Command line

```
protoscalac <file.scala> [options]
```

| Option | Effect |
|---|---|
| `--emit-cpp` | generate C++ source only (the default) |
| `--emit-make` | also write a `Makefile` |
| `--build-so` | write the `Makefile` and run `make` |
| `--as-module` | module mode: `desugarModule`, no `proto_module_main` |
| `--as-script` | script mode: `desugar`, emit `proto_module_main` |
| `--module-name <dotted>` | the logical path the module declares; defaults to the file stem |
| `--module-version <v>` | the version component of the identity; defaults to the empty string |
| `--report-purity` | print what the module needs and why, then exit 0 without emitting |
| `-o <dir>` | write the output there (default: the working directory) |
| `--version`, `--help` | as usual |

With neither `--as-module` nor `--as-script`, the mode comes from the unit: **script**
when it declares an `@main`, **module** otherwise. `--as-module` on a unit with an
`@main` reports `desugarModule`'s own D91 message, unchanged.

`protoscalac` owns a `Session`, and therefore a `ProtoSpace`. That is deliberate and
is the one way it differs from `protoscala-precompile`: an `import` is resolved by
*loading* (D90), a Scala programmer imports **types**, and `case Point(x, y) =>` has
to compile. (Imports are refused in this cut — §8 — but the Session is what the
next cut needs, and removing it would be a second module-loading semantics.)

**The generated `Makefile`:**

```make
CXXFLAGS = -O2 -fPIC -std=c++20
INCLUDES = -I<protoScala include> -I<protoCore includes>
LDFLAGS  = -L<dir> -Wl,-rpath,<dir> ...
LIBS     = -lprotoScala -lprotoCore
TARGET   = module.so
```

- **`-O2`, not `-O3`.** A generated module is one long function per block, and
  `-O3`'s inlining makes compile time superlinear in block size.
- **`-Wl,-rpath` for every library directory**, so the module loads with no
  `LD_LIBRARY_PATH`.
- **The target is `module.so`** — rename it to `<module>.so` before putting it on a
  module search path.

**Toolchain resolution.** Both the build tree's directories and the installation's
are baked in, and the executable decides between them by comparing its own directory
(`/proc/self/exe`, or `argv0` canonicalised) with the build tree's, so a relocated
prefix keeps working. Three overrides replace the corresponding value:
`PROTOSCALAC_CXX`, `PROTOSCALAC_INCLUDE_DIRS`, `PROTOSCALAC_LIBRARY_DIRS`
(`:`-separated).

**A directory containing whitespace is refused**, because `make` splits words on it:
`protoscalac: directory contains whitespace, which make cannot handle: "<dir>"`.

**Errors and exit status.** Parse and compile errors print
`file:line:column: message`; refusals print `file:line: error: message`. Exit 0 on
success; **1** when no arguments are given, the source does not exist, an option is
unknown, parsing, compilation or emission fails, a directory contains whitespace,
the unit is refused, or `make` fails.

## 2. Loading a generated module

A generated module defines:

```c
extern "C" void*       proto_module_init();          // required
extern "C" const char* proto_module_version_v1();    // optional; "" when none
extern "C" const char* proto_module_language_v1();   // diagnostic only
extern "C" int         proto_module_main(int, char**);  // script mode only
```

`proto_module_init` takes no arguments, because that is the contract a **hand-written**
C++ UMD module obeys and Phase 7 must load such a module unchanged. protoScala has no
`getCurrentContext()`, so the host hands the context over with a scoped thread-local
(`gen::ModuleEntryGuard`, internal) and `gen::currentContext` throws
`std::logic_error` when no guard is open — a module initialised outside a protoScala
call context is a host defect, and D74 keeps a defect uncatchable.

`proto_module_init` returns the **module object**, which carries one
`proto::ProtoMethod` cell per exported top-level function — every top-level `def` that
captures nothing — under the function's own Scala name. That is what lets another
runtime call into a transpiled module with protoCore alone; `docs/INTEROP.md` §8 has the
three-call recipe, the limits and the test. `Session::withModule` is the host side, and
it closes its guards before handing the object over, so a caller that needed a
protoScala guard would fail rather than pass for the wrong reason.

`protoscala --run-module <path.so> [args...]` loads a module and runs it:
`proto_module_init`, then `proto_module_main` when present. Without
`proto_module_main` it returns 0 and prints nothing; a module has no output of its
own. A `.so` that defines neither reports
`not a protoScala module: proto_module_init not found`.

**Importing a compiled module.** `CompiledModuleProvider` (alias `compiled`, GUID
`protoScala-compiled-v1`) resolves `a.b.C` to `<base>/a/b/C.so` under each base path in
order: `PROTOSCALA_MODULE_PATH` (`:`-separated) first, then
`<prefix>/<libdir>/protoscala/modules`. `protoscala --version` prints the list, because
that list is the whole surface and a reader who cannot see it cannot tell a missing
module from a mis-set path.

It is installed **after** `provider:scala` in the resolution chain, so a `.scala` beside
a `.so` still wins: installing a compiled module cannot change the meaning of an import
that already resolved. A `.so` that exports `proto_module_main` is refused — a script is
not a module (D8) — with the wording D91 uses on the source path, so one fixture covers
both. The identity is the provider's own GUID, so under P3 a compiled `util.Strings` and
a source `util.Strings` are **two** modules, not two names for one.

**What an imported compiled module binds is a FOREIGN module: late binding.** Its
members resolve by name at run time, exactly as any other foreign module's do. The
`ExportsRec` tables that would carry `ClassInfo` across and keep **early type binding**
are **not built**, so `import util.Shapes` of a `.so` does not make `case Point(x, y) =>`
compile. That is the one place a compiled module is less than a source module, it is the
reason `ModuleLoader.h` binds imports early at all, and it is recorded in §8 rather than
left to be discovered.

## 3. Module identity and version

P3 fixed the key as

```
providerGUID  '\x1F'  logicalPath  '\x1F'  version
```

with the provider **GUID** rather than its alias, and **the empty string as the
permanent, first-class version of a module that declares none** — not a wildcard, not
a synonym for any declared version. The forward-compatibility rule is that an
unversioned module's key is `G\x1FP\x1F` today and byte-identical after manifests
exist, so introducing versions cannot re-alias a module that exists now. `"0.0.0"`,
`"unversioned"` and `"latest"` are all unsafe reservations for the same reason: each
is a value a real module could one day declare.

- The **logical path** is the path the provider was asked for, not the file name.
- The **version** comes from `--module-version` and from nowhere else. protoScala has
  no module manifest and **this phase did not invent one**. A `// @version` pragma
  was rejected (it would be a manifest, invented here and ungoverned) and so was
  defaulting to protoScala's own `PROJECT_VERSION` (every module built today would
  key on `0.7.0`, and rebuilding under `0.8.0` would silently become a different
  module).
- `proto_module_version_v1` is **optional**: a hand-written module that defines only
  `proto_module_init` gets the empty version.
- **D116, recorded so nobody reads "one artefact" as "one instance":** the same `.so`
  loaded by protoScala's `compiled` provider and by another runtime's compiled
  provider has two different GUIDs and is therefore **two modules** in one process,
  with two module objects and two top-level runs.

## 4. Debugging

The emitter writes `#line <n> "<absolute .scala path>"` whenever the source line
changes, so g++, GDB and LLDB report **Scala** source lines. The absolute path is
used so a debugger finds the file from any working directory.

`protoscalac --emit-cpp` is the inspection path. `--disassemble` is **not** available
for a compiled module: there is no bytecode at run time to disassemble.

The entry points (`proto_module_init` and friends) are emitted **before** the block
bodies, so a diagnostic about one of them points at the generated file. A `#line`
cannot be un-set — a directive of 0 is not valid — and that ordering is the reason
none is needed.

## 5. Generated code

Values are `const proto::ProtoObject*`. Everything else follows from two rules.

**The frame (P1, P2).** Each block opens exactly one `gen::Frame`, which owns one
`proto::ProtoContext` and resizes its automatic locals to

```
arity + localCount + maxStack + 1
[0, arity)                        parameters (slot 0 is `this` for a method)
[arity, arity + localCount)       locals
[stackBase, stackBase + maxStack) THE OPERAND STACK
[stackBase + maxStack]            the reserved in-flight-exception slot
```

**No generated C++ local ever holds a `const proto::ProtoObject*`.** The emitter
tracks the operand-stack depth at emit time, so every stack access is a constant
index — `S[B + 7]`, never `*sp++` — and there is **no expression-temporary
mechanism at all**. A binary operation is two slot reads and one slot write:

```cpp
S[B + 7] = gen::add(C, S[B + 7], S[B + 8]);
```

which is P1-safe by construction: both operands are traced slot reads evaluated
before the call, and the result lands in a traced slot. `tests/cli/transpiler-cli.sh`
greps the output for a C++ local holding a value, and finds none.

**Capture slots** are bound from the closure object, after the positional arguments
and the variadic tail, exactly as `ExecutionEngine::execute` binds them. For a block
that is not a method, `self` carries the captures.

**Symbols come from `createSymbol`, never `fromUTF8String`** (P4 rule 4):
`getOwnAttributeDirect` silently misses a non-symbol key, and ≤ 6 ASCII bytes match
by accident, so a short name would hide the bug. `gen::linkModule` interns every
symbol once per space and refuses to link one module into two spaces.

**`JUMP_BACK` emits `gen::safepoint(C)`** and it is not optional:
`ProtoContext::safepoint()` is the only place a context's young chain reaches the
collector, an unsubmitted chain is live by construction, and a generated loop without
it reclaims nothing while looking healthy (P4 rule 1). protoST reclaimed exactly
**0** cells for its entire history while passing 833 tests.

**`Frame` opens with `checkNativeStack()`**, as `execute` does. Without it a
transpiled recursive function ran off the native stack and **segfaulted** instead of
raising `StackOverflowError`.

**A transpiled block IS a `BytecodeModule`** — one with no code words, carrying the
real arity, variadic, method, paramless and capture-slot metadata, whose
`nativeEntry()` is the block's `proto::ProtoMethod`, and which
`ExecutionEngine::execute` calls instead of running bytecode. This is the single most
consequential design decision in the phase, and it was made because the alternative
was measured to be wrong: a function object the runtime cannot recognise is a **wrong
answer with no error** at four sites — `Try.apply` refuses it, a `Map`'s
arity-deciding `map` reads its arity through `compiledModuleOf` and defaults to 1, a
by-name parameter is never forced, and eta-expansion reads `arity()`. Putting the
metadata where the nineteen `compiledModuleOf` readers already look makes all
nineteen correct unchanged.

**The two boundary sites, and no `catch` in the generated file.** A transpiled
function is a `proto::ProtoMethod`, so from the VM's point of view it is a foreign
callable and must present the same boundary the VM's other natives do. The six
clauses live in `src/umd/ForeignBoundary.h` and are reached from exactly two places,
both inside `libprotoScala.so`:

| site | function | why |
|---|---|---|
| 1 | `gen::runModuleBody` | `proto_module_init` is called across a `dlopen` boundary |
| 2 | `gen::enterMethod` | a transpiled block is a foreign callable from the VM |

The order is load-bearing, clause by clause:

```cpp
catch (FutureYield&)            { throw; }   // a cooperative suspension, not an error
catch (ScalaThrow&)             { throw; }   // a Scala exception already in flight
catch (ScalaError&)             { throw; }   // a native throw site's own translation
catch (const std::logic_error&) { throw; }   // D74: a VM defect stays uncatchable
catch (const std::exception& e) { throw ScalaError("RuntimeException", e.what()); }
catch (...)                     { throw ScalaError("RuntimeException", "native exception"); }
```

`catch (const std::logic_error&) { throw; }` sits **before** the `std::exception` arm
because `std::logic_error` derives from `std::exception`; **without it, D74 retires
silently.** The emitter writes none of this: a generator bug that dropped a clause
would otherwise retire D74 in a file no human wrote.

### 5.1 The frame's retry loop

A block with a protected region wraps its **whole body** — not each `try` — in one
retry loop, reproducing `ExecutionEngine::runFrame`:

```cpp
    std::size_t pc = 0;
    std::size_t resumePc = kEntry;
    for (;;) {
      try {
        switch (resumePc) {
            case kEntry: goto L_entry;
            case 20: goto L20;            // one case per handlerPc
            default: throw std::logic_error("blk1: unreachable resume pc");
        }
      L_entry:
        pc = 0; /* ... the body, with `pc = n;` before every instruction that can raise */
      } catch (...) {
        const gen::HandlerRec* h = gen::handleCaught(C, blk1_rec, pc, S, F.pendingSlot());
        resumePc = h->handlerPc;
        continue;
      }
    }
```

Five properties, each deliberate.

**One loop per BLOCK, not per `try`.** `tests/cli/transpiler-cli.sh` counts the
`catch (...)` clauses of a generated file with a `try` and requires exactly one, and
requires **zero** in a file without one — so a block that cannot enter a handler pays
nothing, and the emitter cannot drift into per-`try` loops.

**The `switch` is inside the `try`.** C++ forbids jumping *into* a try block and permits
jumping within one, so the dispatch and every label it reaches share the try — which is
what re-protects the handler body under the frame's own table, exactly as the
interpreter's `continue` re-protects it. The CLI check asserts the two lines' order,
because a switch placed outside would not compile but would also not be noticed.

**The handler body runs OUTSIDE the C++ catch.** `handleCaught` returns the entry and the
generated code assigns `resumePc` and `continue`s. Two things depend on it: the body runs
with no live C++ handler, so it suspends like any other code; and the frame can catch a
**second** exception — raised by the handler body itself, by a non-matching cascade's
`RETHROW`, or by a `finally`. `tests/conformance/20-exceptions/nested-try.scala` is that
case, and it runs transpiled.

**`pc` is assigned before every instruction that can raise, and nowhere else.** The
handler search reads it, so a stale `pc` would find the wrong region — a wrong answer.
`canThrow` lists the opcodes that *cannot*, so anything added later is treated as
throwing until proven otherwise; `JUMP_IF_FALSE`/`JUMP_IF_TRUE` (they call `truthy`) and
`JUMP_BACK` (a GC safepoint) are on the throwing side despite looking pure.

**The classification lives once, in `gen::handleCaught`.** It re-raises the in-flight
exception with a bare `throw;` and reproduces what `runLoop` and `runFrame` do together:
`FutureYield` and `std::logic_error` rethrown; `ScalaThrow`'s value re-rooted into
`returnValue` and the pending slot before anything allocates (A0-2 / E1); `ScalaError`
and the `std::` errors the interpreter names — `invalid_argument` →
`IllegalArgumentException`, `out_of_range` → `IndexOutOfBoundsException`,
`overflow_error` → `ArithmeticException`, `bad_alloc` → `OutOfMemoryError`, any other
`runtime_error` → `RuntimeException` — materialised. **The D74 arm comes AFTER
`invalid_argument` and `out_of_range`**, which derive from `std::logic_error` and *are*
translated by the interpreter; putting it first would make them escape as defects, which
is mutation `R1`. And the handler search happens **before** `materialise`, so the
uncaught path allocates nothing — as an interpreted frame's does not.

The entry for this deviation warned that entering the handler from inside the catch
abandons the loop and the frame's table is never consulted again, measured at **11**
fixtures red. That warning is why the shape above is the interpreter's and not a
simplification of it.

## 6. The published ABI

One shared library and **one** installed header:

- `libprotoScala.so`, `SOVERSION 1` (`PROTOSCALA_ABI_SOVERSION`).
- `include/protoScala/GeneratedModule.h` — *the whole published surface. A generated
  module includes this and `protoCore.h`, nothing else.*
- `lib/cmake/protoScala/` — `find_package(protoScala CONFIG)`.

Nothing is added to the header without a note here, and the soversion moves when the
header changes **incompatibly**, and only then.

**The header HAS changed incompatibly since it was written, and the soversion has NOT
moved. That is deliberate and is recorded rather than left implicit.** On 2026-09-27
`runModuleBody` gained the export table (two parameters), `rethrow` gained the block it
names in its message, and `ExportRec` and `handleCaught` were added. Each of those breaks
a module compiled against the earlier header — which is exactly what the rule is for.
SOVERSION stays at 1 because **no version of this library has ever shipped**: protoScala
is at 0.6.0, the phase is not released, and there is no module anywhere that was built
against the earlier signatures. Moving to 2 would assert that a 1 exists in the wild to be
protected from. The rule applies from the first release: after that, any change to this
file's signatures moves the soversion, and a module built against the old one fails at
`dlopen` with a version message rather than an undefined symbol. `gen::generatedModuleAbi()` is the
**library's** answer, so a module can compare it with the `kGeneratedModuleABI` it
was compiled against; comparing the constant with itself would prove nothing.

`ldd module.so` names `libprotoScala.so.1` and `libprotoCore.so.3`. That is §7's
statement made by a tool rather than asserted by a document.

## 7. What a transpiled module needs, and why

A protoCore-**pure** emission mode is **not offered**, and `--report-purity` is
built instead. A unit is protoCore-pure iff

1. its emitted opcode set is a subset of `{NOP, EXTEND, PUSH_CONST, PUSH_UNIT,
   PUSH_NULL, PUSH_TRUE, PUSH_FALSE, POP, DUP, PUSH_LOCAL, STORE_LOCAL, JUMP,
   JUMP_IF_FALSE, JUMP_IF_TRUE, JUMP_BACK, RETURN}`,
2. it references no `PUSH_GLOBAL` / `STORE_GLOBAL` key,
3. it allocates no `ClassSpec`, and
4. its constant pool holds no `Symbol`, `SendSite`, `SuperSite`, `KwSendSite` or
   `ClassSpec` entry.

Together those mean: **no arithmetic, no send, no class, no prelude.** The set is
empty for anything a Scala programmer would write, and not because of a missing
feature: D1 and D2 make `Int` = `Long` = `BigInt`, so `a + b` means protoScala's
promotion semantics and a pure module would have to reimplement arithmetic — the
exact second implementation this architecture exists to prevent, with `2^54` as the
wrong answer nobody sees. The one genuinely pure case, a wrapper around an external
C library, is **hand-written** C++ and needs no transpiler.

`--report-purity` prints the reasons and a verdict:

```
util/Strings.scala needs protoScala:
  PUSH_GLOBAL println at line 3    a prelude or module global
  ADD at line 7                    protoScala numeric semantics (Int = Long = BigInt, D1)
verdict: not protoCore-pure. Loading this module adds a protoScala runtime and
therefore one ProtoSpace term to the process sizing rule (protoCore MemoryModel.md).
```

**The sizing consequence**, from `protoCore/docs/MemoryModel.md` §1:

> The working set of a process is the size of its perennials, plus the sum of every
> `ProtoSpace`'s *peak* working set, plus all memory not managed by protoCore.

A module that drags a runtime adds a space, and therefore a term in that sum. That is
what the `ldd` line means, and it is why `--pure` was not faked.

## 8. Not implemented

This section is **mandatory**, and the reason is recorded in it:
`PROTOPYC_SPECIFICATION.md` §5 exists precisely to retract an earlier specification
that described features `protopyc` did not have. Everything below is refused at
transpile time with a named message and a source position, and the emitter writes
**no output at all** when it refuses — a partially written `.cpp` that a later `make`
compiles into something is worse than none.

| code | refused | why |
|---|---|---|
| **D113** | `await` in transpiled code | cooperative suspension snapshots a *bytecode* frame (`__mod__`, `__ip__`, `__fbase__`, `__fslots__`), and `nativeReentryDepth()` already refuses to suspend above depth 1 (D43). A transpiled frame has no `ip` and its C++ frame cannot be rebuilt. Detected by **send-site name**, so a user method named `await` is refused too — the safe direction |
| **D121** | named arguments and default values | both are bound by the **callee's** prologue, which a transpiled frame does not run; a transpiled callee would silently see an unbound parameter |
| **D123** | `import` | an import is resolved at transpile time by loading (D90) and leaves no trace in the emitted code — the imported names become ordinary global keys — so a generated module would push globals nothing had filled. Detected on the **source**, an over-approximation: a line beginning with `import` inside a triple-quoted string is refused too |
| **D115** | the REPL | `protoscalac` compiles files. `UnitMode::Repl` is not offered and there is no `res0` echo |
| — | a `--pure` emission mode | §7 |
| — | static type inference | the transpiler performs **none**: the bytecode it consumes has none |
| — | a `py::`-style C++ abstraction layer | values are `const proto::ProtoObject*` and nothing wraps them |
| — | C++ namespaces mirroring the module hierarchy | every generated symbol is `static` in one translation unit |
| — | `emitExports` / `ExportsRec`: **early type binding** for an imported compiled module | §2. The provider ships and `import` of a `.so` works, but it binds LATE, like any foreign module. A `ClassInfo` is not carried across, so a pattern match or a `new` against a compiled module's class does not compile |

**Closed on 2026-09-27, and named here because a specification that quietly drops a
refusal is the failure `PROTOPYC_SPECIFICATION.md` §5 exists to retract.** **D120** —
`try` / `catch` / `finally` — is **supported**: the frame has its retry loop, one per
**block** rather than one per `try`, with the resume `switch` inside the `try` and the
classification in `gen::handleCaught` so the generated file writes exactly one
`catch (...)` and a block with no protected region writes none. **D118** —
classes, traits, objects, case classes and enums — and **D122** — `super` and
`super[T].m` — are **supported**. `gen::constFrom` rebuilds a `ClassSpec` from the
static tables and the interpreter's own `makeClass`, `instantiate` and `superSend` do
the work, so §5's "one implementation, two consumers" still holds. `object Main extends
App` is supported through `gen::runApp`, because for such a unit the program *is* the
object's initialisation (D104) and calling an `@main` that does not exist printed
nothing.

**D114** is not a refusal but a difference: a transpiled program's
`StackOverflowError` fires at a different recursion depth than the interpreter's,
because the native frame differs. The class and the message are unchanged; only the
depth is.

## 9. The differential harness

Two harnesses, and the second exists because the first has a blind spot.

**The fixture differential** (`tests/conformance/run-transpiled.sh`, one CTest case
per registered fixture, `-DPROTOSCALA_TRANSPILED_TESTS=ON` by default) runs each
fixture through transpile → `make` → `--run-module` and judges it against the
fixture's own first-line directive. The directive parser is copied verbatim from
`run.sh`, so the two harnesses cannot disagree about what a fixture asks for.

Measured over all **922** registered fixtures (2026-09-27, after D120):

| | count |
|---|---:|
| pass | **922** |
| fail | **0** |
| of which transpiled, compiled and ran | **704** |
| of which correctly rejected at compile time (an `EXPECT-ERROR` fixture) | **61** |
| excluded, each a refusal with a reason code | **157** |

By code: D123 **87**, D113 **39**, D121 **31**. The progression was 360 run / 500
excluded in the first cut, 626 / 235 after D118, 704 / 157 after D120.

**Each code names the FIRST refusal found**, which is why the per-code numbers do not
move monotonically: closing D120 raised **D113 from 31 to 39**, because eight of the 86
were `await` inside a `try` and D120 had been answering for them. The harness found those
eight by failing — they had come off the list with the rest of D120, and the bidirectional
guard named each one as "refused and not on the exclusion list".

Three anti-rot guards, each present because its absence is a way for the harness to
pass while proving nothing:

1. **A C++ compile failure is always a FAIL**, never a pass. Without it an
   `EXPECT-ERROR` fixture whose expected substring appeared in a g++ diagnostic would
   go green on a broken generator.
2. **A transpile refusal is a FAIL unless the fixture is on the exclusion list.**
3. **A listed fixture that transpiles, compiles and runs correctly is a FAIL**, naming
   itself and asking to be removed from the list — the same discipline the `XFAIL`
   directive already uses.

A refusal is told apart from a **compile error** by its message, and a compile error
is judged against the directive. That is what keeps the 158 `EXPECT-ERROR` fixtures
inside the coverage instead of excluding them: a compile-error fixture is the one
place the two paths are guaranteed to share an implementation.

The **benchmark workloads** are covered too, by the same rule: twelve
`benchmarks-transpiled/<workload>` cases run each `benchmarks/comparable/*.scala`
through the pipeline and verify the result its `// EXPECT:` line states. All twelve
transpile; `attr_lookup` and `object_tree` were refused under D118 and the bidirectional
guard duly failed them the day classes landed, which is the guard doing its job. **No timing is asserted in any gate**: the host is not reliably quiet and a
timing assertion in a gate is a false failure waiting to happen. The comparison lives in
`benchmarks/run_transpiled_benchmarks.py`, whose finding is that the transpiled path is
**slower** — geomean 1.244×, up to 2.57× on a call-bound workload — because a
`proto::ProtoMethod` takes a `ProtoList`, so `execute`'s native-entry branch allocates
one per call.

**The corpus differential** (`.agent_scratch/phase7-transpiler/corpus/`) runs the
Scala 3 `tests/run` corpus down both paths. The rule is *not* "does protoScala agree
with Scala"; it is **every corpus test the interpreter passes must also pass
transpiled**. Over the 601 in-scope tests the interpreter passes **191 (31.8 %)**, with
**0 divergences** in either direction at every stage. What changed is the coverage:

| | first cut | after D118 | after D120 |
|---|---:|---:|---:|
| transpiled pass (raw body) | 5 | 158 | **176** (92.1 %) |
| of those, checkfile-verified | 1 | 91 | **95** |
| refused | 186 | 33 | **15** — D121 9, D123 6 |

Two readings this measurement is kept for. **A fixture count is not language coverage**:
D118 moved the fixtures 1.7× and the corpus 31×, a blind spot the fixture harness could
not have reported on itself; D120 then moved both by about 1.1×, and that agreement is
itself informative — `try` is as common in our fixtures as in real Scala, and classes were
not. And the harness's `full`-shim configuration reports **0**, not because of the
transpiler but because the shim itself declares `def assert(cond: Boolean, msg: Any =
"assertion failed")` — a default value, D121 — so every test refuses at the scaffolding's
first line. That is the **third** time in this phase a shim artefact has looked like a
transpiler limit, and it is the strongest argument for implementing D121 next: nine raw
tests, and the whole `full` row.

## 10. The mutation matrix

**Every differential case must be shown to fail under a named mutation of the
generator.** A code-generator suite that has never been red is a suite whose coverage
is unknown.

Each mutation records **which ctest expression it must turn red**, in
`tests/mutations/apply.py`'s `COVERS` map, and the runner asks for it rather than
choosing by hand. That is not tidiness: choosing by hand reported two retry-loop
mutations as GREEN when the filter (`Guarded.`) matched **no case at all**, because
`test_generated_support` is one ctest case named `unit/generated_support` rather than a
set discovered from gtest. A matrix that ran zero cases is indistinguishable from a matrix
that passed, so the filter's selection count is checked with `ctest -N` before any green
result is believed. `tests/mutations/` holds the patches; the measured red sets are in
`.agent_scratch/phase7-transpiler/mutations-emitter.md`, together with the mutations
the plan listed that **cannot** be applied to this cut because the code they mutate
does not exist yet (the handler-entry stack depth, `materialise` held in a C++ local,
`emitExports`'s `TypeRec` table). Listing them as inapplicable is the honest form;
listing them as passed would not be.

The retry loop has four (`R1`–`R4`): the D74 arm moved ahead of the two
`std::logic_error` subclasses the interpreter translates, the re-rooting write dropped,
the `pc` tracking suppressed, and `handlerFor` ignoring its range. The first and the last
are the two that would produce a **wrong answer** rather than an error, which is why they
exist.

`R3`'s expression is `cli/transpiler-cli`, which asserts the property directly. The
transpiled `20-exceptions` cases go red under it as well, but by **hanging**: with `pc`
stuck at 0 every exception enters the same handler, and a handler that raises then loops.
Each such case costs the harness's 90-second timeout, so proving it that way takes half an
hour and says nothing the direct assertion does not.

`CompiledModuleProvider` has four of its own (`P1`–`P4`), and one of them earned its
place by staying **green**: reversing the resolution-chain order left
`cli/compiled-provider` passing, because `Session::load` consults the source loader itself
and reaches the chain only on a miss — so the order, which is what decides for every other
runtime arriving through `getImportModule`, was untested. It is now asserted on the
chain's contents in `Provider.CompiledComesAfterSourceInTheChain`, which `P2` reds. A
mutation that stays green locates a missing test; it is not a dud.

The cross-runtime call has its own four (`tests/mutations/apply.py X1`–`X4`), and each
turns `interop/foreign-call` red: the export scan finding nothing, `gen::enterMethod`
losing its host fallback, the cells never being attached to the module object, and `add`
changed so the harness's pinned values disagree. The last one is the check on the
*expectations* rather than the code: it fails on both paths, because the interpreted run
and the transpiled run are compared against the same three lines.
