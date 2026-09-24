# protoScala Interoperability (UMD)

> **Status:** planned — Phase 6. Nothing here is implemented yet, **except the
> named-argument convention of §7, whose protoScala half shipped in Phase 4.**

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

Current provider availability in the family (2026-09): protoPython registers
`native`, `python_stdlib`, `compiled`, `hpy`; protoST registers `st`; protoJS
and protoClojure register none yet. Aliases `py`, `js`, `clj` therefore need
work in those runtimes; this is coordinated, not assumed.

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

## 6. Rules

- Never cache interned symbols in function-local statics; symbols are per
  space.
- The UMD module cache is process-global (DESIGN R5): one runtime per process.

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

**What is implemented:** the protoScala side, exercised end to end by
`tests/conformance/23-named-arguments/foreign-*.scala` against `__kwprobe`, a
runtime-provided stand-in whose native `call` reads `keywordParameters` exactly as
a UMD-provided foreign method will and is reached through exactly the same
`SEND_KW` path. **What is not:** UMD itself (Phase 6), so no real `import py.…`
call can be made yet. `foreign-python-keyword.scala` and
`foreign-python-open-encoding.scala` are `XFAIL` with their expected output
recorded, for Phase 6 to convert rather than invent.
