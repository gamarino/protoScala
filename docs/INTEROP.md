# protoScala Interoperability (UMD)

> **Status:** planned — Phase 6. Nothing here is implemented yet.

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
