# protoScala Interoperability (UMD)

> **Status:** implemented in 0.6.0; `import st.<module>` works as of Track Y. The
> mechanism below is built, installed and tested: protoScala loads `.scala`
> modules, registers itself as a UMD provider, routes the four family prefixes,
> and loads provider plug-ins by `dlopen`. **One family prefix now has a provider
> behind it — `st`** — and a protoScala program imports a protoST module, reads its
> members and shares its values with no copy at the boundary (§6). `py`, `js` and
> `clj` still have no provider, so `import py.numpy` compiles, routes, and reports
> `no provider registered for 'py'`. §3 says exactly what routes where, §6 says
> what cross-runtime interop does and does not cover, and ROADMAP's **Track Y** is
> the remaining cross-repository work.

## 1. Mechanism

protoCore's Unified Module Discovery consists of:

- `proto::ModuleProvider` — `tryLoad(logicalPath, ctx)`, `getGUID()`,
  `getAlias()`;
- `proto::ProviderRegistry::instance()` — `registerProvider`, `findByAlias`,
  `findByGUID`, `getProviderForSpec("provider:<alias|GUID>")`;
- the space's resolution chain (`ProtoSpace::setResolutionChain`) — an ordered
  list of `"provider:X"` entries and filesystem paths; the first
  non-`PROTO_NONE` result wins and is cached;
- `ProtoSpace::getImportModule(ctx, logicalPath, attrName)`.

The resolver passes the **whole** logical path to every provider; it does not
route by prefix.

## 2. protoScala provider

- `ScalaModuleProvider`, alias `scala`, registered once per process.
- Resolves `a.b.C` to `a/b/C.scala` relative to the importing file, then to
  the directories in `PROTOSCALA_PATH`.
- A module object exposes the file's top-level definitions as attributes.
- The provider finds its runtime through a registry keyed by `ProtoSpace`
  (protoST pattern), so imports work from any thread.

protoScala registers a **second** provider since Phase 7:

- `CompiledModuleProvider`, alias `compiled`, GUID `protoScala-compiled-v1`.
- Resolves `a.b.C` to `<base>/a/b/C.so` under `PROTOSCALA_MODULE_PATH`
  (`:`-separated) and then `<prefix>/<libdir>/protoscala/modules`.
  `protoscala --version` prints the list.
- Installed **after** `provider:scala` in the resolution chain, so a `.scala` beside a
  `.so` still wins and installing a compiled module cannot change an import that already
  resolved.
- It loads **any** conforming `.so`, generated or hand-written: only
  `proto_module_init` is required, and the version accessor is optional. A `.so` that
  exports `proto_module_main` is refused, because a script is not a module (D8).
- It carries its own state — base paths, handle map, and the host runtime whose call
  context `proto_module_init` needs — rather than looking anything up by `ctx->space`.
  That is §6's rule applied from the first line, and the host it carries is why a
  process runs one protoScala Session while compiled modules are in use.
- Under P3 its GUID makes a compiled `util.Strings` and a source `util.Strings` **two**
  modules, with two identities and two module objects.
- What an import of it binds is a **foreign** module: late binding, members resolved by
  name at run time. The `ExportsRec` tables that would keep early type binding are not
  built, so a pattern match against a compiled module's class does not compile. This is
  the one respect in which a compiled module is less than a source one.

## 3. Prefix routing (protoScala convention)

| Prefix | Provider spec | Example |
|---|---|---|
| `py.` | `provider:py` | `import py.numpy as np` |
| `js.` | `provider:js` | `import js.d3 as d3` |
| `st.` | `provider:st` | `import st.PumpTwin as Twin` |
| `clj.` | `provider:clj` | `import clj.core as clj` |
| none | resolution chain (`provider:scala` first) | `import util.Strings` |

**An `import` does not always reach a module at all.** Plain Scala's member
import — `import Color.*`, `import Obj.{a, b}` — takes names out of something
already in scope and never enters this machinery: a family prefix wins over
everything, and otherwise an import whose longest dotted prefix names an
in-scope object, companion or `enum` is resolved in the compiler and no provider
is consulted. LANGUAGE.md §3.2 is the whole rule (D105). Everything below is
about the import that *does* load a module.

The prefix is stripped before `tryLoad`. A missing provider raises
`ImportError: no provider registered for 'py'`.

Only a leading segment equal to one of the four is stripped, and only when the
path has at least two segments: `import py.a.b` asks the `py` provider for `a.b`,
`import local.py.Helper` is an ordinary module path, and `import py` looks for a
module called `py` on the resolution chain. The list is **closed** on purpose —
if any registered alias could be a prefix, `import util.Strings` would become
hijackable by a plug-in aliased `util`, and a program's meaning must not depend on
which plug-ins are installed. A fifth runtime needs one line in
`src/umd/Prefixes.h`.

Current provider availability in the family, re-verified 2026-09-24 (Track Y):

| Runtime | Aliases it registers | Reachable from a protoScala importer today |
|---|---|---|
| protoPython | `native`, `python_stdlib`, `compiled`, `hpy` | no — and none of them is `py` (§6.1) |
| protoST | `st` | **yes**, in a process that also constructs an `STRuntime`, from the thread that constructed it (§6) |
| protoJS | none | no |
| protoClojure | none | no |
| protoScala | `scala`, `compiled` | yes |

The shipped `protoscala` binary links libprotoCore and libreadline and nothing
else of the family, so "in a process that also constructs an `STRuntime`" means an
embedder that links protoST, or a provider plug-in (§3.1) that brings one. The
tests that exercise it are `umd/protost-interop`
(`tests/unit/protost_interop.cpp`), built whenever protoST is found beside this
tree, and protoST's own `tests/unit/test_cross_runtime_provider.cpp`.

So `import py.numpy as np` reports, verbatim:

```
ImportError: no provider registered for 'py'. Install the runtime that provides
it, or point PROTOSCALA_PROVIDERS at its plug-in
```

protoScala's half is done and the message is what makes the coordination visible
to a user rather than silent. The remaining half is **Track Y** in ROADMAP: it
needs protoPython to register an agreed alias, to make its provider serve a caller
in another `ProtoSpace` the way protoST's now does, and to ship a plug-in exporting
the ABI of §3.1 — and it needs a maintainer ruling on R5. §6.1 states what was
measured about that.

### 3.1 Provider plug-ins

A plug-in is a shared object that registers one or more protoCore
`ModuleProvider`s. **protoScala ships none**: the mechanism exists so a sibling
runtime *can* be present, and whether two runtimes may be co-resident in one
process stays R5's question for the maintainer.

The ABI a plug-in must export:

```cpp
extern "C" const char* protoScalaProviderABI(void);
    // returns "protoScala-provider-1" exactly; anything else is refused
extern "C" int protoScalaRegisterProviders(proto::ProtoSpace* space);
    // registers with proto::ProviderRegistry::instance(); 0 on success
```

Discovery order: each colon-separated entry of `PROTOSCALA_PROVIDERS` (a `.so`
file, or a directory whose `*.so` files are loaded in sorted order), then
`<prefix>/lib/protoscala/providers/`, which the package creates.
`dlopen` uses `RTLD_NOW | RTLD_GLOBAL`, so a plug-in's own dependencies resolve;
handles are never `dlclose`d, because a registered provider outlives the call.
A plug-in that fails to load, exports the wrong ABI or returns non-zero is
reported on stderr and **skipped** — a broken plug-in must never stop the runtime
from starting. `protoscala --version` lists what it found and where it looked.

The registration call is wrapped in the boundary shape of §6, because a plug-in
runs foreign code and a C++ exception escaping `dlopen`'d code into this frame
would cross an ABI this binary did not compile.

### 3.x Compiled modules (Phase 7)

`protoscalac` produces a `.so` that defines `proto_module_init`, which is the same
artefact a hand-written C++ UMD module and `protopyc` produce. The **producing
language is an implementation detail of the module**.

Status, precisely: the artefact is produced and `protoscala --run-module` loads and
runs it. The provider that would make it an `import` target — alias `compiled`, GUID
`protoScala-compiled-v1` — is **specified and not built**, so a compiled module cannot
yet be imported. See `docs/PROTOSCALAC_SPECIFICATION.md` §2.

## 4. Calling convention

A foreign value is an ordinary `ProtoObject`. `np.sqrt(2.0)` compiles to
`np.call(ctx, nullptr, sym("sqrt"), np, [2.0], kw)`; named arguments go in the
keyword `ProtoSparseList`. Returned values are used directly — no copies, no
conversions — except where a language-level wrapper is needed (a foreign
list seen as a Scala `Seq` is wrapped, not copied).

## 5. Type mapping

| protoCore value | Scala view |
|---|---|
| `SmallInteger` / `LargeInteger` | `Int` / `Long` / `BigInt` |
| double | `Double` |
| `ProtoString` | `String` |
| `PROTO_TRUE` / `PROTO_FALSE` | `Boolean` |
| `PROTO_NONE` | `null` |
| `ProtoList` | `List` |
| other objects | dynamic objects: members resolved by name at run time |

## 6. Rules, and the measured limit on co-residency

- Interned symbols are **process-global** since protoCore 2.2.0 (P3), so a
  function-local static cache of a `createSymbol` result is now sound. This rule
  used to read "never cache interned symbols in function-local statics; symbols
  are per space", and it is **reversed**. protoScala keeps every key it needs in
  `RuntimeLayout` anyway — one place to read, nothing re-interned — which is a
  clarity convention now rather than a correctness requirement.
- A provider resolves its runtime from **`ctx->space`**, never from a
  thread-local: a thread-local answers "module not found" on every actor worker,
  which is the bug protoST's `STModuleProvider.h` records having had.
- **The boundary catch shape is mandatory at every UMD and foreign-call site.**
  One template, `src/umd/ForeignBoundary.h`, six clauses in this order, and the
  order is the contract: `FutureYield` (not a `std::exception`, and first anyway,
  or a `catch (...)` would eat a cooperative suspension); `ScalaThrow` (a Scala
  exception already in flight, which the `std::exception` arm would rewrite into a
  `RuntimeException`); `ScalaError` (already a translation); **`std::logic_error`
  re-thrown** (D74: a VM defect must never be catchable, and `std::logic_error`
  *is* a `std::exception`); `std::exception`, carrying `what()` across; and
  `catch (...)`, which says "native exception" rather than inventing a message.
  There is one unit test per clause.
- **A provider must not resolve its runtime from `ctx->space`.** This was the
  measured ceiling on co-residency, and Track Y removed it. `tryLoad(path, ctx)`
  receives the *caller's* context, and each runtime owns its own `ProtoSpace`, so
  a provider that looks its runtime up by the caller's space finds nothing when
  the caller is another runtime — and reports a module that is there as absent.
  Measured on 2026-09-24 against protoST `3fd0438`: both providers stayed
  reachable in one process, but `import st.counter_lib` from protoScala reported
  `provider 'st' has no module 'counter_lib'`. protoST `e82682b` fixes it in the
  provider, with **no protoCore change**: a `ModuleProvider` is an object with its
  own state, so it takes its runtime from that state and uses `ctx` only to
  allocate the result in the caller's context. `provider:scala` should follow the
  same shape the day a foreign runtime imports a `.scala` module; today it
  resolves through the space-keyed `moduleHostForSpace` and therefore still
  answers only protoScala's own callers.

- **A cross-space attribute key is the SAME pointer since protoCore 2.2.0 (P3).**
  This entry used to read "a cross-space attribute key is a different pointer",
  and it is **reversed**. An attribute key is the address of an interned symbol,
  and protoCore used to intern per `ProtoSpace` (`ctx->space->symbolTable`), so
  the module NAMESPACE a provider returned had to be rebuilt with keys interned
  in the CALLER's space. The trap was that protoCore embeds a short string in the
  pointer word, so a 5-byte member name matched across spaces by accident and a
  7-byte one missed silently, with `getAttribute` returning `PROTO_NONE` — which
  is also a legitimate value. `Counter` (7 bytes) was the test case for exactly
  that reason. P3 made interning process-global: one canonical pointer per
  spelling per process.

  **The namespace rebuild survives, for a different reason.** `buildCallerFacade`
  also re-parents the namespace object to `callerCtx->space->objectPrototype`,
  and **prototypes remain per space**. A foreign object handed straight across
  still carries parent links into the providing runtime's prototype chain.
  Deleting the facade would trade a silent attribute miss for a silent prototype
  mismatch (P3 D9). Global interning fixed names, not prototypes.

- **A module's identity is provider + path + version** (protoCore 2.2.0, P3 D11),
  rendered as one canonical string with `\x1F` between the components. The
  provider component is the provider's GUID, never its alias. A module that
  declares no version has the empty version, which is reserved permanently for
  exactly that and is not a wildcard. `Session::loadForeign` publishes through
  `ProtoSpace::registerModule` under that identity, so the module joins the
  process-global module list and is rooted in the **importing** space — before
  P3 its only anchor was inside the providing runtime, and destroying that
  runtime dropped it.

- **No copy at the boundary, verified.** `umd/protost-interop` reads the same
  protoST class out of the namespace protoScala received and out of protoST's own
  globals, each through its own context with its own interned symbol, and prints
  both addresses:

  ```
  NO-COPY PROOF
    protoScala space   = 0x7ffec21cb4c0
    protoST space      = 0x57d25533c440
    Counter via Scala  = 0x743e4cf7e9c0  getHash(scalaCtx) = 127810928110016
    Counter via ST     = 0x743e4cf7e9c0  getHash(stCtx)    = 127810928110016
  ```

  Two spaces, one cell, the same `getHash` from either side. Cloning the value at
  the boundary turns that test red.

- **What cross-runtime interop does NOT cover today**, stated so nobody infers
  more than was built:
  - **Values, not calls.** A protoST class or object crosses as a value and its
    attributes read back. **Calling a protoST method from protoScala does not
    work**: a protoST method is an object carrying `__bc_ptr__` that protoST's own
    `ExecutionEngine` interprets on SEND, not a `proto::ProtoMethod` protoCore can
    dispatch. A foreign callable needs to be a protoCore method, as the plug-in
    fixtures in `tests/conformance/25-interop/` are.
  - **One thread.** A protoST module's top level runs on protoST's root context,
    which only the thread that constructed the runtime may allocate on (protoST
    D26), and a foreign caller has no context of its own in that space. An import
    from any other thread — an actor worker, for instance — is refused with a
    message rather than raced.
  - **One protoST runtime per process.** The provider is registered once per
    process, so two `STRuntime`s make "which one" ambiguous, and the ambiguous
    case is refused rather than resolved arbitrarily.
  - **A snapshot of the namespace.** protoST's module object is mutable; the
    namespace the importer receives is taken once, at import. A name the module
    binds later is not visible to an importer that already imported it.
- **A module is a process-level entity** (maintainer ruling, 2026-09-24): loaded
  once per process, listed in a global and therefore perennial list, anchoring its
  own contents through its variables. So `SharedModuleCache` being keyed by
  logical path with **no `ProtoSpace` component** is the correct identity, not the
  hazard Phase 6 took it for, and there is no cross-space GC edge to reason about
  — a loaded module is not owned by a space, and the anchor is the mechanism that
  keeps its values alive.

  A **prefixed** import still calls the named provider's `tryLoad` directly rather
  than going through `getImportModule`, and the reasons are now different ones:
  `getImportModule` has no way to address a *named* provider (it walks the calling
  space's chain), and the cache key drops the prefix, so `st.counter_lib` and a
  `counter_lib.scala` would be one entry. R5 in [STATUS.md](STATUS.md) states both,
  with what the implementation actually anchors and where it approximates the
  platform rule; the mechanism is protoCore's, so it is reported there rather than
  changed here.

## 7. Named arguments across the boundary

protoScala compiles `f(x = 1)` into protoCore's `keywordParameters`, the fifth
parameter of every `ProtoMethod`, for Scala-defined methods, native methods and
foreign callables alike — there is no special case for the foreign path, because
there is no boundary to translate at. Keyword arguments are part of protoCore's
kernel calling convention:

```cpp
typedef const ProtoObject*(*ProtoMethod)(
    ProtoContext* context, const ProtoObject* self, const ParentLink* parentLink,
    const ProtoList* positionalParameters, const ProtoSparseList* keywordParameters);
```

**The key is the address of the interned `ProtoString` symbol** for the
parameter's name, obtained with `ProtoString::createSymbol`. Interning guarantees
exactly one address per name in a `ProtoSpace`, so the address *is* the name's
identity; and because UMD is in-process, the same address is valid in every
runtime, which is the unambiguity the convention buys.

`fromUTF8String`, `fromUTF8` and `fromStdString` do **not** intern: they return a
different pointer for the same text, and a key built from one of them matches
nothing — silently, exactly as if the argument had not been passed. The sharp
edge is that protoCore embeds a **short** string in the pointer word itself, so a
non-interning key works by accident for a short parameter name and fails only for
one too long to embed. `tests/unit/test_exceptions.cpp`
(`KeywordConvention.ANonInternedKeyMatchesNothing`) asserts both halves, and
`tests/conformance/23-named-arguments/named-argument-with-a-long-name-binds.scala`
exercises a long name end to end.

An integer-keyed `ProtoSparseList` is the right structure and not a compromise:
interned strings are **perennial** — never collected, never moved — so there is
nothing for the collector to trace. `ProtoMap` exists for arbitrary *collectable*
object keys, which is a different problem; it is the same principle that makes
`ProtoTuple` interned and perennial, and the same reason transient data must
never be mapped to it.

Binding happens in the **callee**, not the caller: `BytecodeModule` carries the
parameter names (interned by `linkSymbols`) and one default block per parameter,
and `execute`'s prologue fills positional parameters, then the keyword arguments
by name, then the remaining defaults. A caller that cannot resolve its callee
statically therefore still works. The platform is late-binding even where Scala
is not; late *detection* is accepted, silent failure is not, so an unknown name, a
duplicate and a missing argument each raise an `IllegalArgumentException` naming
the method and the parameter.

Each other runtime translates its own surface on its own side of the boundary:
Python's `**kwargs` map straight across, JavaScript has no keyword arguments and
its provider decides what an options object means, Clojure's trailing map and
Smalltalk's keyword selectors likewise. None of that is protoScala's concern.

**What is implemented, and now proved across a real boundary.** UMD itself ships
in 0.6.0, so this convention is no longer exercised only in-process. A named
argument has been carried through a provider loaded by `dlopen`, into a callee
that reads it out of `keywordParameters` keyed by the interned symbol's address,
with no adapter on either side:

- `tests/conformance/25-interop/stand-in-provider-keyword.scala` — `p.echo(1, b = 2)`
  through `import js.probe as p`, printing `echo a=1 b=2`;
- `…/stand-in-provider-long-keyword.scala` — the same with
  `aVeryLongKeywordName`, a name too long to embed in a pointer word, which is
  where a key built with anything but `createSymbol` fails **silently**;
- `…/a-provider-failure-is-catchable.scala` — a C++ `std::runtime_error` thrown
  inside the provider, caught at the Scala call site as a `RuntimeException` with
  its message intact.

The provider those fixtures use is a **test double**
(`tests/unit/probe_provider.cpp`). It answers `probe` and never a real library's
data, and it is named for what it is: a stand-in dressed as numpy would be a green
test asserting numpy behaviour against a stub.

**What is still not reachable:** a `py` provider. `foreign-python-keyword.scala`
and `foreign-python-open-encoding.scala` remain `XFAIL`, with their expected
output recorded and their directives naming the real blockers (§6.1). The runner
inverts an `XFAIL` verdict, so the day a `py` provider appears both fixtures go
red and say so. That work is ROADMAP's **Track Y**.

### 6.1 Why `py` did not follow `st`, measured

Track Y made `st` work by changing protoST's provider alone. The same change
cannot be made to protoPython's providers, and the reasons were read out of
protoPython's source on 2026-09-24 rather than assumed:

1. **No `py` alias.** protoPython registers `native`, `python_stdlib`, `compiled`
   and `hpy` (`src/library/PythonEnvironment.cpp`). Adding an alias is the easy
   part.
2. **The environment comes from a thread-local, not from the provider or the
   caller.** `PythonEnvironment::fromContext(ctx)` ignores `ctx` and returns
   `s_threadEnv`, a `thread_local` pointer. A provider that has no environment for
   the calling thread cannot serve the call, and one that does would serve it with
   whatever environment that thread happens to hold.
3. **protoPython's `ProtoSpace` is a process singleton by design.**
   `PythonEnvironment::getProcessSpace()` returns a function-local
   `static proto::ProtoSpace`, commented "L-Shape: one per process". A co-resident
   protoScala `Session` owns its own space, so the premise that there is one space
   per process is false in that process. Every module protoPython builds, and every
   symbol it interns, belongs to that singleton space.
4. **The first fixture cannot pass at all.** `foreign-python-keyword.scala`
   expects `float64` from `np.array(…, dtype = "float64")`, and protoPython ships
   no `numpy` — `lib/python3.14/` has 213 entries and none of them is numpy. No
   amount of provider plumbing produces that output, and a stand-in dressed as
   numpy would be a green test asserting numpy behaviour against a stub, which is
   the failure mode this family already rejected once.

So (1) and (4) are facts about protoPython's contents and (2)–(3) are its
ownership model, which is what DESIGN R5 asks the maintainer about. Making a `py`
provider serve a foreign caller is a change to `PythonEnvironment`, not a
provider-local change, and it is not protoScala's to make.


## 8. What a transpiled function adds (Phase 7)

Track Y proved two things, and the second is what Phase 7 answers. Values cross a
runtime boundary **without copying** — one cell, two `ProtoSpace`s, the same address
and the same `getHash` printed from both sides — and a foreign runtime **cannot call**
a protoST method, because a protoST method is `__bc_ptr__` plus protoST's own engine
rather than a `proto::ProtoMethod`. protoScala had exactly the same shape: a compiled
function is an object carrying a bytecode-module address, and only
`ExecutionEngine::execute` can run it.

**A transpiled module's functions are `proto::ProtoMethod`s**, reached through
`ProtoObject::asMethod`, so any runtime that holds the object can call it with no
knowledge of protoScala.

### 8.1 How a foreign runtime reaches them

`proto_module_init` returns a **module object** that carries one `proto::ProtoMethod`
cell per exported top-level function, under the function's own Scala name. A caller
needs three protoCore calls and nothing else:

```cpp
const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, "add");
const proto::ProtoObject*  fn = module->getAttribute(ctx, key);
const proto::ProtoObject*   r = fn->asMethod(ctx)(ctx, fn->asMethodSelf(ctx), nullptr, args, nullptr);
```

What is exported is every top-level `def` **that captures nothing**. A capturing block
reads its captures from the `self` argument, and a bare cell is called with no `self`,
so exporting one would publish a function that reads a null environment; a top-level
`def` captures nothing by construction, so the restriction removes nothing a reader
would expect. Names the module does not export read as `PROTO_NONE`, like any absent
attribute.

The cells are a **second view** of the same block, not a replacement. The globals the
module's own top level binds still hold ordinary protoScala function objects, whose
`__code__` carries the arity, method-ness and capture slots that the nineteen places
inside the runtime read (`Try.apply`, a by-name `FORCE_THUNK`, eta-expansion, …).
Publishing only the bare cells would lose that metadata; publishing only the function
objects would leave a foreign runtime nothing it can call.

The host side is one call, `Session::withModule`, which loads the module and hands the
object to a callback. The callback runs with **no protoScala call context active** — the
guards are closed before it is entered — because a foreign runtime cannot install one
without a protoScala header, so a demonstration that left the guard open would prove
nothing. `gen::enterMethod` notices the absence and installs the engine and layout of
the host that linked the module, for the duration of that call.

That fallback carries its own limit, and it is the reason `linkModule` refuses a second
host: an exported cell is a bare function pointer with nowhere to carry its host, so
**one `Session` per process** while exported cells are in use. Two would be a choice
between two preludes with no way to make it, which is a wrong answer rather than an
error, so it is refused at link time with a message that says so.

### 8.2 Demonstrated

`tests/interop/foreign-call.sh` (CTest case `interop/foreign-call`) is the test, and it
is a differential, not a self-assertion:

1. `tests/interop/foreign_caller.cpp` includes `protoCore.h` and one shim header whose
   only `#include` is `protoCore.h`. The harness **greps** the file and fails if the
   string `protoScala` appears in it below the banner comment, so its independence is
   checked rather than described. The host half is a separate translation unit.
2. The same four functions are run **through the interpreter** from the same
   `Exports.scala`, and their output is compared with the values the caller asserts.
   Neither path is trusted to define the answer.
3. The foreign caller then calls all four through `asMethod`: two `Int` returns (one of
   them a `while` loop over a local), a `String` return read back as bytes, and one that
   raises — the exception crosses the boundary as a C++ exception the caller catches,
   rather than ending the process.

Four mutations (`tests/mutations/apply.py X1`–`X4`) each turn that case red: dropping
the export scan, dropping the host fallback in `enterMethod`, never attaching the cells
to the module object, and changing `add` so the pinned values disagree.

**What is not demonstrated.** The caller stands in for another runtime; it is not
protoPython, protoJS or protoST. Wiring one of those up needs a change in *that*
repository — each would call `Session::withModule` (or its own equivalent of the two
guards) and then exactly the three protoCore calls above — and no such change has been
made or measured. The claim proven here is the one the phase set out to prove: the call
needs protoCore and nothing else. Which runtimes have taken it up is a separate,
currently empty, list.

Two further limits, stated so nobody infers more:

- **Within one `ProtoSpace`.** A `proto::ProtoMethod` is a raw code pointer; the
  arguments and the result are cells, and cells belong to the space that allocated
  them. A caller in another space can hold the method object (Track Y's no-copy
  property) and can call it, but the call allocates in the *caller's* context and the
  body reads protoScala's `RuntimeLayout`, which is per-space. A cross-**space** call
  therefore needs the module's host runtime to be reachable from that space, which is
  R5's open question and is **not** resolved.
- **Without protoScala's front end, not without protoScala's runtime.** `ldd
  module.so` names `libprotoScala.so.1`, and that is the honest statement of the cost:
  the module brings a runtime, and therefore one `ProtoSpace` term in protoCore's
  process sizing rule.

Both limits are unchanged by §8.2: what that test proves is the *call*, not
co-residency across spaces and not freedom from the runtime the module links.
