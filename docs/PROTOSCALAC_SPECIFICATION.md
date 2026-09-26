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

`protoscala --run-module <path.so> [args...]` loads a module and runs it:
`proto_module_init`, then `proto_module_main` when present. Without
`proto_module_main` it returns 0 and prints nothing; a module has no output of its
own. A `.so` that defines neither reports
`not a protoScala module: proto_module_init not found`.

**Not yet shipped:** `CompiledModuleProvider` (alias `compiled`, GUID
`protoScala-compiled-v1`) and `PROTOSCALA_MODULE_PATH`, so `import` of a compiled
module is not available. `--run-module` is the whole loading surface today. See §8.

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

**The retry loop is not implemented in this cut**, so `try`/`catch`/`finally` is
refused (§8). When it is written, the `switch` that reaches the labels must be
**inside** the `try`: C++ forbids jumping *into* a try block and permits jumping
within one, and the frame must be able to catch a **second** exception — one raised
by its own handler body, by the `RETHROW` a non-matching cascade emits, or by a
`finally`. Entering the handler from inside the catch abandons the loop and the
frame's table is never consulted again: measured, that turns **11** fixtures red.

## 6. The published ABI

One shared library and **one** installed header:

- `libprotoScala.so`, `SOVERSION 1` (`PROTOSCALA_ABI_SOVERSION`).
- `include/protoScala/GeneratedModule.h` — *the whole published surface. A generated
  module includes this and `protoCore.h`, nothing else.*
- `lib/cmake/protoScala/` — `find_package(protoScala CONFIG)`.

Nothing is added to the header without a note here, and the soversion moves when the
header changes **incompatibly**, and only then. `gen::generatedModuleAbi()` is the
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
| **D118** | classes, traits, objects, case classes, enums | `MAKE_CLASS`, `NEW`, `NEW_SPREAD`, `INVOKE_INIT`, `STORE_FIELD`, `STORE_FIELD_IF_NEW`, `SET_FIELD`: the `ClassSpec` must be rebuilt from the static tables, which this cut does not do |
| **D120** | `try` / `catch` / `finally` | the generated frame has no retry loop (§5) |
| **D121** | named arguments and default values | both are bound by the **callee's** prologue, which a transpiled frame does not run; a transpiled callee would silently see an unbound parameter |
| **D122** | `super`, `super[T].m` | the super-site search needs the defining template's key |
| **D123** | `import` | an import is resolved at transpile time by loading (D90) and leaves no trace in the emitted code — the imported names become ordinary global keys — so a generated module would push globals nothing had filled. Detected on the **source**, an over-approximation: a line beginning with `import` inside a triple-quoted string is refused too |
| **D115** | the REPL | `protoscalac` compiles files. `UnitMode::Repl` is not offered and there is no `res0` echo |
| — | a `--pure` emission mode | §7 |
| — | static type inference | the transpiler performs **none**: the bytecode it consumes has none |
| — | a `py::`-style C++ abstraction layer | values are `const proto::ProtoObject*` and nothing wraps them |
| — | C++ namespaces mirroring the module hierarchy | every generated symbol is `static` in one translation unit |
| — | `CompiledModuleProvider`, `PROTOSCALA_MODULE_PATH` | §2; `--run-module` is the loading surface today |

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

Measured over all **921** registered fixtures:

| | count |
|---|---:|
| pass | **921** |
| fail | **0** |
| of which transpiled, compiled and ran | **360** |
| of which correctly rejected at compile time (an `EXPECT-ERROR` fixture) | **61** |
| excluded, each a refusal with a reason code | **500** |

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

**The corpus differential** (`.agent_scratch/phase7-transpiler/corpus/`) runs the
Scala 3 `tests/run` corpus down both paths. The rule is *not* "does protoScala agree
with Scala"; it is **every corpus test the interpreter passes must also pass
transpiled**. Over the 601 in-scope tests: the interpreter passes **191 (31.8 %)**,
**0 divergences** in either direction — and the transpiler **refuses 186** of those
191 and runs **5**, of which **one** is checkfile-verified. The reason is one code:
a `tests/run` test is `object X { def main … }`, and D118 refuses a class. **The
fixture differential's 360 is what our fixtures are made of, not a third of Scala**,
and that is a finding the fixture harness could not have produced.

## 10. The mutation matrix

**Every differential case must be shown to fail under a named mutation of the
generator.** A code-generator suite that has never been red is a suite whose coverage
is unknown. `tests/mutations/` holds the patches; the measured red sets are in
`.agent_scratch/phase7-transpiler/mutations-emitter.md`, together with the mutations
the plan listed that **cannot** be applied to this cut because the code they mutate
does not exist yet (`MAKE_CLASS`'s parent order, the handler-entry stack depth,
`materialise` held in a C++ local, `emitExports`'s `TypeRec` table). Listing them as
inapplicable is the honest form; listing them as passed would not be.
