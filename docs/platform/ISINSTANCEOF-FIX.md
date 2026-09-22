# Platform fix: `isInstanceOf` / `hasParent` (protoCore)

> **Status:** 2026-09-22. Maintainer decision (Phase 2 Q2, refined): **no new
> API — correct the existing `isInstanceOf` and `hasParent` implementations**
> ("corregir en lo posible"), keeping their contracts. Implemented on its own
> protoCore branch, reviewed and merged by the maintainer. §2 below was the
> first proposal (a new `isDescendantOf`) and is kept only as the behavioural
> target of the fix.

## 1. Problem

Every language on protoCore needs a fast, exact answer to "does this object
inherit from that prototype?" — Scala `isInstanceOf` / type patterns, Smalltalk
`isKindOf:`, Python `isinstance`. protoCore offers:

- `isInstanceOf(ctx, prototype)` (`core/ProtoObject.cpp:437-505`) — a
  depth-first walk with a sibling stack that starts at `getPrototype()`,
  returns `PROTO_TRUE`/`PROTO_NONE`, and returns `PROTO_FALSE` after an
  arbitrary 50-step cap: wrong answers for deep hierarchies.
- `hasParent(ctx, target)` — allocates a list through `getParents` on every
  call and returns true for `target == this`.

Yet an object's parent chain is already **flat and de-duplicated**: `newChild`
prepends the receiver to the receiver's own chain and `addParent` inserts the
new parent's missing ancestors (`core/ProtoObject.cpp:398-423, 1191-1226`), and
attribute lookup already relies on that (it walks only the receiver's own
chain). Membership is therefore a linear scan of one singly linked chain.

## 2. API

```cpp
// True iff `ancestor` appears in this object's parent chain (the flattened,
// de-duplicated chain used by attribute lookup). The object itself is not its
// own descendant. Allocation-free, lock-free, no step limit beyond the chain
// length; false for non-object values (embedded values, collections) unless
// their prototype chain contains `ancestor`.
bool isDescendantOf(ProtoContext* context, const ProtoObject* ancestor) const;
```

- Non-object receivers (SmallInteger, strings, lists, …) are answered through
  their prototype: `x.isDescendantOf(p)` is `x.getPrototype() == p ||
  x.getPrototype()->isDescendantOf(p)`.
- A mutable object answers for its current version's chain.
- `nullptr` ancestor → `false`.
- `isInstanceOf` and `hasParent` are not changed (other runtimes rely on
  them); their limits are documented next to the new API.

## 3. Tests (GoogleTest)

- Direct parent, grand-parent, mixin added with `addParent`, object vs itself
  (false), unrelated prototype (false), `nullptr` (false).
- A 1,000-level `newChild` chain: the root is found (no step limit) and the
  call allocates nothing (compare the context's allocated-cells count before
  and after).
- Embedded values and strings answered through their prototypes.
- Mutable object after `setParents`.
- No regression: the full protoCore suite.

## 3b. Chosen approach

`isInstanceOf` keeps its contract (starts from `getPrototype()`, returns
`PROTO_TRUE` / `PROTO_NONE`) but walks the flattened chain without the 50-step
cap, the DFS stack or any allocation; `hasParent` keeps its contract (true also
for `target == this`) without building a list. Where a construction path leaves
the chain unflattened, results must stay identical to today's, except that deep
hierarchies beyond the old cap now answer correctly.

## 4. Rollout

Implementation fix, no API or layout change. protoScala Phase 2 uses a
per-class marker attribute until this merges, then switches its type tests to
`isInstanceOf` (one call site). protoST (`isKindOf:`) and protoPython may
adopt it independently.
