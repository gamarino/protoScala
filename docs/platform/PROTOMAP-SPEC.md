# Platform spec: `ProtoMap` (protoCore)

> **Status:** implemented on protoCore branch `feature/pslo-p1` (not merged). This is protoCore work
> carried out as part of the protoScala project (roadmap Phase P1) and reused
> by protoClojure (track C) and protoST (track S).

## 1. Problem

Every hashed collection in the proto* family needs a map from **language
objects** to values:

- Scala `Map`/`Set` (protoScala §6.1),
- Clojure maps and sets (protoClojure builds its own layout of two sparse
  lists + buckets + sequence numbers on a mutable wrapper),
- Smalltalk `Dictionary`/`Set` (protoST builds a hash → bucket
  `ProtoSparseList`; its D32 records that `Set` and `Dictionary` disagree on
  element equality).

protoCore offers:

- `ProtoSparseList` — a persistent AVL map keyed by `unsigned long`. The GC
  traces values only (`ProtoSparseListImplementation::processReferences`,
  `core/ProtoSparseList.cpp:234`; the Small form at `:392`). An object's
  address cannot be used as a key safely: if the key is the only reference,
  the object is collected and the key dangles.
- `ProtoSet` / `ProtoMultiset` — keyed by `getHash()` with no collision
  handling: two elements with the same hash replace each other.
- `ProtoTuple` — interned and perennial, so it cannot hold transient entries.

Each runtime has therefore invented its own layer, with different equality
semantics and different GC exposure.

## 2. Design (maintainer decision)

Add a **new object type**, `ProtoMap`, **identical to
`ProtoSparseList` in every respect except that the key is a
`const ProtoObject*` traced by the GC**.

- `ProtoSparseList` is not modified: its model, API, ABI and performance stay
  as they are.
- Keys are ordered by the numeric value of the `ProtoObject*` word (tag
  included), exactly as `ProtoSparseList` orders its `unsigned long` keys.
  Equality of keys is word identity.
- Because protoCore's collector does not move objects, an object's word is a
  stable key and the key object is recovered directly from the node.
- A key word may be an embedded (non-pointer) value such as a `SmallInteger`;
  `processReferences` must trace a key only when it carries a cell pointer
  (`ProtoObject::asCellPointer(key) != nullptr`), exactly as values are traced
  today.
- `nullptr` is not a valid key (the Small form uses a zero key to mark empty
  slots).

### 2.1 Public API (mirrors `ProtoSparseList`)

```cpp
class ProtoMap {
public:
    bool has(ProtoContext*, const ProtoObject* key) const;
    const ProtoObject* getAt(ProtoContext*, const ProtoObject* key) const;      // nullptr when absent
    const ProtoMap* setAt(ProtoContext*, const ProtoObject* key, const ProtoObject* value) const;
    const ProtoMap* removeAt(ProtoContext*, const ProtoObject* key) const;
    bool isEqual(ProtoContext*, const ProtoMap* other) const;
    unsigned long getSize(ProtoContext*) const;

    const ProtoObject* asObject(ProtoContext*) const;
    const ProtoMapIterator* getIterator(ProtoContext*) const;
    unsigned long getHash(ProtoContext*) const;

    void processElements(ProtoContext*, void* self,
        void (*method)(ProtoContext*, void*, const ProtoObject* key, const ProtoObject* value)) const;
    void processValues(ProtoContext*, void* self,
        void (*method)(ProtoContext*, void*, const ProtoObject* value)) const;
};

// ProtoContext
const ProtoMap* newMap();

// ProtoObject
bool isMap(ProtoContext*) const;
const ProtoMap* asMap(ProtoContext*) const;
```

`getAt` returns `nullptr` for an absent key so that a stored `PROTO_NONE`
remains distinguishable (the PROTO_NONE-ambiguity lesson).

### 2.2 Implementation outline

- Reuse the `ProtoSparseList` AVL algorithms (rotation, insertion, removal,
  Small inline form with up to three entries) parameterised on key type, so
  there is one algorithmic implementation and two node classes. The node
  classes differ only in the key field's type and in `processReferences`,
  which traces the key when it is a cell pointer.
- Every mutator allocates new nodes inside a `CriticalSection`, as
  `ProtoSparseList` does today.
- `getHash` is order-independent over (key word, value hash) pairs, like
  `ProtoSparseList::getHash`.

## 3. Tagged-pointer budget

protoCore encodes the type of a `ProtoObject*` word in a **6-bit pointer tag**
(64 values) and, for embedded values, in a **4-bit embedded type** (16
values). Usage in protoCore 1.3.0 (`headers/proto_internal.h`):

| Space | Used | Free |
|---|---|---|
| Pointer tags | 0–26, plus 27 = `ProtoMap` (28 values) | 28–63 (36 values) |
| Embedded types | 0, 2, 3, 4, 5 (5 values) | 1, 6–15 (11 values) |

Every language added to the platform will want new types, so tags are a
scarce, platform-wide resource. (`ProtoMPSCQueue`, PMQ-SPEC, takes one more
tag under the same rules.) Rules for this work:

1. `ProtoMap` consumes **at most one new pointer tag**, for its
   public handle.
2. Its internal node classes (AVL node, Small form) need **no tag of their
   own**. The GC discriminates cells through the `Cell` virtual interface
   (`processReferences`, `getType`), not through tags; internal nodes are
   never exposed as `ProtoObject*` words. They get new `CellType` enum values,
   which are not a scarce resource.
3. The iterator **does not take a second tag**. **Decided (2026-09-22):
   option (a).** The two options considered:
   - **(a, recommended)** expose iteration only through `processElements` /
     `processValues` and a `ProtoMapIterator` C++ handle that is
     never boxed as a `ProtoObject*` word (runtimes wrap it in their own
     iterator objects when a language needs one);
   - **(b)** share `POINTER_TAG_SPARSE_LIST_ITERATOR` and discriminate the two
     iterator kinds by `CellType`.
4. Language-level encodings (for example protoScala's hashed keys) must reuse
   existing encodings — a `SmallInteger` word carrying a hash — and never claim
   a tag or embedded type of their own.
5. The tag table in `proto_internal.h` gains a comment block recording the
   budget and the rule that a new tag needs maintainer approval.

## 4. Shared hashed-collection helper (proposed)

The key-classification scheme every runtime needs is the same; only the
language's equality and hash differ (Scala: cooperative numeric equality,
`1 == 1.0`; Clojure: `(= 1 1.0)` is false; Smalltalk: `1 = 1.0` is true). To
avoid three copies, protoCore may provide a small helper on top of
`ProtoMap`:

```cpp
struct KeySemantics {
    // true when the language's equality for this key is identity (eq)
    bool (*isIdentityKey)(ProtoContext*, const ProtoObject* key);
    // the language's hash, used only for value-equality keys
    unsigned long (*hash)(ProtoContext*, const ProtoObject* key);
    // the language's equality, used only inside a collision bucket
    bool (*equals)(ProtoContext*, const ProtoObject* a, const ProtoObject* b);
};

// Persistent hashed map / set operations over a ProtoMap:
//  - identity key      -> slot key = the key object itself, slot value = v
//  - value-equality key -> slot key = SmallInteger(hash & 54-bit mask),
//                          slot value = entry ProtoList[k, v], or a ProtoList of
//                          entries on a real hash collision
const ProtoMap* hashedPut(ProtoContext*, const ProtoMap*,
                                       const KeySemantics&, const ProtoObject* k, const ProtoObject* v);
const ProtoObject* hashedGet(ProtoContext*, const ProtoMap*,
                             const KeySemantics&, const ProtoObject* k);   // nullptr when absent
const ProtoMap* hashedRemove(ProtoContext*, const ProtoMap*,
                                          const KeySemantics&, const ProtoObject* k);
void hashedForEach(ProtoContext*, const ProtoMap*, void* self,
                   void (*fn)(ProtoContext*, void*, const ProtoObject* k, const ProtoObject* v));
```

Properties:

- Identity keys and hashed keys can never collide: a hashed slot key is a
  `SmallInteger` word, an identity slot key is a cell pointer or a non-integer
  immediate. The language's `isIdentityKey` must return `false` for integers.
- Entries and buckets are `ProtoList`s (the Small form holds up to five
  elements in one cell), never `ProtoTuple`s, so they die with the collection.
- No global table, no lock, no interning: the structure is as GC-friendly as
  any other persistent protoCore structure.

**Decided (2026-09-22): the helper lives in protoCore**, because the three
runtimes need exactly the same mechanism.

## 5. Tests (protoCore, GoogleTest)

- API parity with `ProtoSparseList`: has/get/set/remove/size/isEqual/hash/
  iteration, in both the Small and AVL forms and across the Small→AVL
  promotion boundary.
- **GC safety:** insert keys that are referenced *only* by the collection,
  force collections under a low `ProtoSpace` memory limit, then recover every
  key and verify its contents.
- Embedded keys (`SmallInteger`, booleans, chars) are stored and never traced.
- Persistence: old versions remain valid and unchanged after updates.
- Concurrency: several threads build independent versions from one shared
  base while the GC runs.
- Helper (if adopted): identity/hashed coexistence, forced collisions with a
  constant hash, removal from a collision bucket, iteration yields every
  (k, v) exactly once.

## 6. Rollout

1. Implement in protoCore with the tests above; bump protoCore's minor
   version (ABI addition).
2. **Full rebuild of every embedder** (protoPython, protoJS, protoST,
   protoClojure, protoScala) — a stale binary against a new protoCore crashes
   (ABI lesson).
3. protoScala Phase 3 uses it for `Map`/`Set`.
4. Track C: protoClojure maps/sets migrate to it; its custom layout is
   removed. The ordering semantics of Clojure maps must not gain guarantees
   the language does not have.
5. Track S: protoST `Dictionary`/`Set` migrate to it; which equality decides
   membership (its D32) is a protoST language decision that the migration
   makes implementable, not a decision this spec takes.

## 7. Decisions (P1)

Recorded 2026-09-22. D1 and D2 were taken by the maintainer (accepted
recommendations). The rest were taken by the implementing agent under the
maintainer's authorisation for the overnight run, and were **approved by the
maintainer on 2026-09-23**.

| Id | Decision | Taken by |
|---|---|---|
| D1 | Iterator option (a): `processElements`/`processValues` plus an unboxed C++ iterator handle; no second tag | maintainer |
| D2 | The hashed-collection helper lives in protoCore (Task 9 runs) | maintainer |
| D2a | Bucket encoding (ε): the slot value is one flat list `[k0, v0, k1, v1, …]`; `[k, v]` without collisions | agent; approved by the maintainer on 2026-09-23 |
| D2b | Hashed slot key: the 54 hash bits written directly into an embedded `SmallInteger` word (never a cell) | agent; approved by the maintainer on 2026-09-23 |
| D2c | Put with an existing equal key keeps the stored key and replaces the value (Scala `HashMap.updated` behaviour) | agent; approved by the maintainer on 2026-09-23 |
| D3 | `nullptr` key is ignored silently: `has` → false, `getAt` → nullptr, `setAt`/`removeAt` return the receiver | agent; approved by the maintainer on 2026-09-23 |
| D4 | `isEqual`: same size, same key words, values compared by word identity | agent; approved by the maintainer on 2026-09-23 |
| D5 | Own prototype `ProtoSpace::mapPrototype` (layout change covered by the mandatory rebuild) | agent; approved by the maintainer on 2026-09-23 |
| D6 | Small vs AVL distinguished by `CellType` under the single tag 27 | agent; approved by the maintainer on 2026-09-23 |
| D7 | Keep `SOVERSION 1`; version 1.2.0 → 1.3.0 | agent; approved by the maintainer on 2026-09-23 |
| — | Overnight run: work in the worktree `../protoCore-pslo` on branch `feature/pslo-p1`, build in its own build directory, not merged; Task 12 (embedder rebuild) deferred until the maintainer reviews the branch | maintainer (scope) |
