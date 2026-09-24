# protoScala Interoperability (UMD)

> **Status:** implemented in 0.6.0, with one gap that is not protoScala's to
> close. The mechanism below is built, installed and tested: protoScala loads
> `.scala` modules, registers itself as a UMD provider, routes the four family
> prefixes, and loads provider plug-ins by `dlopen`. **What no runtime in the
> family registers yet is a `py`, `js` or `clj` provider**, so `import py.numpy`
> compiles, routes, and reports `no provider registered for 'py'`. §3 says
> exactly what routes where, and ROADMAP's **Track Y** is the cross-repository
> work. A second, measured limit is in §6.

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

## 3. Prefix routing (protoScala convention)

| Prefix | Provider spec | Example |
|---|---|---|
| `py.` | `provider:py` | `import py.numpy as np` |
| `js.` | `provider:js` | `import js.d3 as d3` |
| `st.` | `provider:st` | `import st.PumpTwin as Twin` |
| `clj.` | `provider:clj` | `import clj.core as clj` |
| none | resolution chain (`provider:scala` first) | `import util.Strings` |

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

Current provider availability in the family, re-verified 2026-09-24:

| Runtime | Aliases it registers | Reachable from `protoscala` today |
|---|---|---|
| protoPython | `native`, `python_stdlib`, `compiled`, `hpy` | no — and none of them is `py` |
| protoST | `st` | only in a process that also constructs an `STRuntime`, and then only for callers in protoST's own object space (§6) |
| protoJS | none | no |
| protoClojure | none | no |
| protoScala | `scala` | yes |

So `import py.numpy as np` reports, verbatim:

```
ImportError: no provider registered for 'py'. Install the runtime that provides
it, or point PROTOSCALA_PROVIDERS at its plug-in
```

protoScala's half is done and the message is what makes the coordination visible
to a user rather than silent. The other half is **Track Y** in ROADMAP: it needs
protoPython to register an agreed alias and ship a plug-in exporting the ABI of
§3.1, and it needs a maintainer ruling on R5, because protoPython's providers come
with a whole `PythonEnvironment`.

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

- Never cache interned symbols in function-local statics; symbols are per space.
  Every key protoScala needs lives in `RuntimeLayout` and is read, never
  re-interned.
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
- **A provider serves only callers that share its `ProtoSpace`**, and this is the
  measured ceiling on co-residency today. `ModuleProvider::tryLoad(path, ctx)`
  receives the *caller's* context, and every provider in the family resolves its
  runtime from `ctx->space`, so in a process holding two runtimes each provider
  answers only its own. Measured on 2026-09-24 against protoST `3fd0438`: an
  `STRuntime` and a protoScala `Session` were built in one process and **both**
  `provider:st` and `provider:scala` stayed reachable, but
  `import st.counter_lib` from protoScala reported `provider 'st' has no module
  'counter_lib'`, while the same provider loaded that module from a context in
  protoST's own space. See R5 in [STATUS.md](STATUS.md).
- **protoCore's `SharedModuleCache` is keyed by logical path with no `ProtoSpace`
  component** and is never invalidated, so in a two-runtime process an unprefixed
  import could be answered from a module the other runtime loaded under the same
  name. A **prefixed** import therefore calls the named provider's `tryLoad`
  directly and does not go through `getImportModule`, and `provider:scala` is
  prepended to the chain so protoScala's own provider is asked first for an
  unprefixed path. The residual hazard is the unprefixed path (DESIGN R5).

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
output recorded and their directive now naming the real blocker — *no runtime in
the family registers the UMD alias `py`* — rather than the routing, which shipped.
The runner inverts an `XFAIL` verdict, so the day a `py` provider appears both
fixtures go red and say so. That work is ROADMAP's **Track Y**.
