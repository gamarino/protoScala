# Phase P1 — ProtoSparseListObject (protoCore) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add to protoCore a new persistent map type, `ProtoSparseListObject`, identical to `ProtoSparseList` except that its key is a GC-traced `const ProtoObject*`, without changing `ProtoSparseList`'s model, API, ABI or performance. Then rebuild every embedder from clean.

**Architecture:** The AVL and Small-form algorithms in `core/ProtoSparseList.cpp` move into one internal header of function templates (`core/SparseListAlgorithms.h`), parameterised on the node class, with the key compared by its machine word. `ProtoSparseList` is re-expressed on those templates with identical behaviour; a performance gate checks that it is not slower. The new type adds three cell classes: an AVL node, a Small form and an iterator. They are told apart by `CellType`, not by pointer tags, and the public handle takes a single new pointer tag (27). The only difference in `processReferences` is that the key is reported to the collector when it carries a cell pointer. The shared hashed-collection helper (PSLO-SPEC §4) and the iterator representation (§3.3) are gated on the maintainer's answers in Task 0.

**Tech Stack:** C++20, CMake ≥ 3.16, GoogleTest 1.14 (FetchContent, `file(GLOB)` test registration), Linux `perf stat`.

**Spec:** protoScala/docs/platform/PSLO-SPEC.md (also protoScala/docs/DESIGN.md §6.1 and §11 R8)

## Global Constraints

- `ProtoSparseList` is not modified: its model, API, ABI and performance stay as they are (PSLO-SPEC §2). Its public class (`headers/protoCore.h:789-805`), its impl class layouts (`headers/proto_internal.h:1490-1616`) and its behaviour must be bit-for-bit unchanged. Only function *bodies* in `core/ProtoSparseList.cpp` may be re-expressed through the shared templates.
- Keys are ordered by the numeric value of the `ProtoObject*` word (tag included). Key equality is word identity (PSLO-SPEC §2).
- `processReferences` traces a key only when `ProtoObject::asCellPointer(key) != nullptr` (PSLO-SPEC §2).
- `nullptr` is not a valid key; the Small form uses a zero key word to mark empty slots (PSLO-SPEC §2).
- `getAt` returns `nullptr` for an absent key, so a stored `PROTO_NONE` stays distinguishable (PSLO-SPEC §2.1).
- The new type uses **at most one new pointer tag** (`POINTER_TAG_SPARSE_LIST_OBJECT = 27`). Internal node classes get `CellType` values and no tags. The iterator does not take a second tag (PSLO-SPEC §3 rules 1-3).
- The tag table in `proto_internal.h` gains a comment block recording the budget and the rule that a new tag needs maintainer approval (PSLO-SPEC §3 rule 5).
- Every mutator allocates new nodes inside a `ProtoContext::CriticalSection`, as `ProtoSparseList` does (PSLO-SPEC §2.2). Callbacks supplied by callers (language `hash`/`equals`, `processElements` visitors) never run inside a critical section.
- GC principle (tasks/lessons.md, 2026-09-15): no new work under stop-the-world beyond stack roots, the mutables-tree root and the roots of global structures. A GC pacing or root-scope change is a maintainer decision. The only STW addition this plan allows is one global prototype root (Task 5, gated on D5).
- Ask before any design decision. Task 0 lists every open decision; the executor gets answers before Task 1 and never picks an option on its own.
- Embedders use protoCore structures. After this ABI change **every embedder is rebuilt from clean**: protoPython, protoJS, protoST, protoClojure, protoScala. **protoJS builds use `-j1` (never `-j≥2`). test262 runs sequentially (`TEST262_CONCURRENCY=1`), and the user must be asked before any full sweep.**
- Workspace safety (tasks/lessons.md, 2026-09-15): create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. Scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/`. Use `perf stat` only, never `perf record`. Use no `rm -rf` (use `cmake --build … --clean-first` for clean rebuilds) and no `cmake --install`.
- Git: work on branch `feature/pslo-p1` in `protoCore`. Commit with the repository's configured identity (name "Gustavo Marino"; never override `user.email`). End every commit message with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Version: protoCore `1.2.0` → `1.3.0` (PSLO-SPEC §6.1: minor bump, ABI addition).
- All code comments and docs are in professional English.

---

## Task 0: Maintainer decisions (blocking — no code before these are answered)

The executor presents these questions to the maintainer (Gustavo Marino) **as written**, records each answer verbatim in a new section `## 7. Decisions (P1)` at the end of `protoScala/docs/platform/PSLO-SPEC.md`, and commits that file in the protoScala repository. Do not choose an option. Where a task below depends on an answer, it says so and gives the code for each option.

- [ ] **D1 — Iterator representation (PSLO-SPEC §3.3, open).**
  - **(a) — spec recommendation.** Iteration goes through `processElements` / `processValues` and a `ProtoSparseListObjectIterator` C++ handle that is never boxed as a `ProtoObject*`. Consequences found in the code:
    - The handle is the raw (untagged) cell address. The public iterator class offers no `asObject` and no `ProtoObject` accessor recognises it, so no tag-0 word that is not a `ProtoObjectCell` can reach the attribute chain. That is the invariant documented at `headers/proto_internal.h:208-218` (#92's class of bug).
    - Like any cell held in C++ locals, the handle lives as long as the allocating context's young chain. A language iterator object that must outlive that context cannot store the handle as an attribute. It keeps `(collection, position)` instead.
  - **(b)** Share `POINTER_TAG_SPARSE_LIST_ITERATOR` (9) and tell the two iterator kinds apart by `CellType`. Consequences:
    - `ProtoObject::asSparseListIterator` (`core/ProtoObject.cpp:1584`) is currently a pure tag test, so it would hand a PSLO iterator to `ProtoSparseListIterator`. It must gain a virtual `getType()` check. That is a small cost on a `ProtoSparseList`-facing accessor, so the "no ProtoSparseList change" constraint must be waived for this line.
    - `getPrototype` returns `sparseListIteratorPrototype` for both kinds (`core/ProtoObject.cpp:345`).
- [ ] **D2 — Where the shared hashed-collection helper lives (PSLO-SPEC §4, open).**
  - **(i) — spec recommendation.** In protoCore (`KeySemantics`, `hashedPut/Get/Remove/ForEach`). Task 9 runs.
  - **(ii)** Each runtime keeps its own copy. Task 9 is skipped.
  - If (i), three sub-questions come up because the spec text is ambiguous:
    - [ ] **D2a — Bucket encoding.** The spec says the slot value is "entry `ProtoList[k, v]`, or a `ProtoList` of entries on a real hash collision". When `k` and `v` are themselves `ProtoList`s, an entry `[k, v]` cannot be told apart from a two-entry bucket.
      - **(ε) — recommended by this plan.** The slot value is always one flat list `[k0, v0, k1, v1, …]`. With no collision it is exactly `[k, v]` (one cell, as in the spec), and a collision appends a pair. It is unambiguous and costs no extra cells.
      - **(α)** The slot value is always a list of entries: +1 cell per value-equality entry.
      - Task 9's code implements (ε). If the answer is (α), the executor stops at Task 9 and asks for a plan revision.
    - [ ] **D2b — Hashed slot-key encoding.** The spec says `SmallInteger(hash & 54-bit mask)`. The `SmallInteger` field is a signed 54-bit value (`headers/proto_internal.h:132-136`), so `ProtoContext::fromInteger` on a value ≥ 2^53 returns a **LargeInteger cell**. That breaks the "slot key is never a cell pointer" property.
      - **Recommended:** write the 54 hash bits directly into an embedded `SMALLINT` word, with no allocation. The word is always a valid `SmallInteger`.
      - **Alternative:** mask to 53 bits and use `fromInteger`.
    - [ ] **D2c — Put with an existing equal key.** Keep the stored key and replace only the value (recommended), or replace both.
- [ ] **D3 — `nullptr` key contract.** The spec only says `nullptr` is not a valid key.
  - **Recommended:** silent. `has` → `false`, `getAt` → `nullptr`, `setAt`/`removeAt` return the receiver unchanged.
  - **Alternative:** throw `std::invalid_argument`.
- [ ] **D4 — `isEqual` value comparison.** `ProtoSparseList::isEqual` is declared (`headers/protoCore.h:796`) but has **no definition** anywhere in `core/`, so there is no reference behaviour.
  - **Recommended:** same size, same key words, and values compared by word identity. protoCore cannot know a language's value equality.
  - **Alternative:** values compared with `ProtoObject::compare(...) == 0`.
- [ ] **D5 — Prototype.**
  - **Recommended (mirrors `ProtoSparseList`):** its own `ProtoSpace::sparseListObjectPrototype`. This adds a `ProtoSpace` field (a layout change, already covered by the mandatory full rebuild) and one global root under STW (O(1), a global-structure root, allowed by the GC principle).
  - **Alternative:** reuse `sparseListPrototype`. There is no layout change, but embedders cannot tell the two types apart through the prototype.
- [ ] **D6 — Telling Small from AVL under one tag.** PSLO-SPEC §3 rule 2 implies this is done by `CellType` (one virtual `getType()` call per PSLO operation; `ProtoSparseList` is unaffected). Confirm.
  - **Alternative:** drop the Small form for PSLO. This deviates from "identical".
- [ ] **D7 — SOVERSION.** The spec asks for a minor bump (1.2.0 → 1.3.0). With D5 = own prototype, the `ProtoSpace` layout changes, which is an ABI break for any stale binary.
  - **Recommended:** keep `SOVERSION 1` as the spec says, and rely on the mandatory clean rebuild.
  - **Alternative:** `SOVERSION 2`.
- [ ] **Recorded, not asked (facts the maintainer should see):**
  - PSLO-SPEC §2.2 refers to "`ProtoSparseList::getHash`". `ProtoSparseList::getHash`, `isEqual` and `processValues` are declared (`headers/protoCore.h:796,801,804`) but never defined. This plan defines them for PSLO only; defining them for `ProtoSparseList` would change `ProtoSparseList` and is out of scope.
  - There is no `ProtoObject::isSparseList`. The spec adds `isSparseListObject` anyway.
  - `ProtoSparseList` itself uses **two** tags (8 and 26: `POINTER_TAG_SPARSE_LIST_SMALL`, `headers/proto_internal.h:248`). PSLO uses one.
  - `ProtoSparseList::processElements` (`core/ProtoSparseList.cpp:616-622`) calls the visitor while it holds a `CriticalSection`. The PSLO version walks the tree recursively without allocating and calls the visitor outside any critical section. There is no semantic difference, but the visitor may run language code.
  - The AVL node's `size` field is 24 bits in both types, so the maximum size is 16,777,215 entries, the same as `ProtoSparseList`.

---

## File Structure

protoCore (`/home/gamarino/Documentos/proyectos/protoCore`):

| File | Change | Responsibility |
|---|---|---|
| `core/SparseListAlgorithms.h` | **Create** | Internal, never installed. Function templates for the AVL (rotate, rebalance, get, set, remove, findMin, in-order walk, iterator stack) and the Small form (count, get, pairAt, promote, setAt), parameterised on the node/Small class. They live in an anonymous namespace so every call stays TU-local: no PLT and no new exported symbols. |
| `core/ProtoSparseList.cpp` | Modify bodies only (`:62-216`, `:223-232`, `:308-379`, `:416-531`) | Re-express `ProtoSparseList` on the templates; behaviour identical. |
| `core/ProtoSparseListObject.cpp` | **Create** | PSLO cells (node, Small, iterator) and public trampolines. |
| `core/ProtoHashedCollection.cpp` | **Create (only if D2 = i)** | `hashedPut/Get/Remove/ForEach`. |
| `headers/proto_internal.h` | Modify | Forward declarations (`:52-54`), union members (`:84-106`), tag 27 plus the budget comment (`:222-248`), `ExpectedTag` (`:321-325`), `CellType` values (`:542-574`), `KeyType` aliases on the existing sparse-list impl classes (`:1490`, `:1554`), new cell classes (after `:1616`), `BigCell` (`:1800-1827`), `static_assert`s (`:1833-1835`). |
| `headers/protoCore.h` | Modify | Forward declarations (`:44-46`), `ProtoObject::isSparseListObject` (`:337-342`) and `asSparseListObject` (`:381-382`), the `ProtoSparseListObjectIterator` and `ProtoSparseListObject` classes (after `:805`), `ProtoContext::newSparseListObject` (after `:1178`), `ProtoSpace::sparseListObjectPrototype` (after `:1511`, D5), `KeySemantics` plus helper functions (D2 = i). |
| `core/ProtoContext.cpp` | Modify (after `:679`) | `newSparseListObject()`. |
| `core/ProtoObject.cpp` | Modify | `getPrototype` case (`:343-345`), `isSparseListObject`/`asSparseListObject` (near `:1563-1584`), `asSparseListIterator` `getType` check (D1 = b only). |
| `core/ProtoSpace.cpp` | Modify | `cellTypeName` (`:126-161`), prototype creation (`:1188`), GC global root (`:418-419`). |
| `CMakeLists.txt` | Modify | Add sources (`:32-57`); version `1.2.0` → `1.3.0` (`:8`). |
| `performance/sparse_list_benchmark.cpp` | Modify | Deterministic seed and self-verifying checksum (benchmarks-must-self-report lesson). |
| `test/SparseListCharacterizationTests.cpp` | **Create** | Model-based characterization of `ProtoSparseList` (locks behaviour before the refactor). |
| `test/SparseListObjectCellTests.cpp` | **Create** | Tag, CellType, `processReferences` of each cell (embedded keys not traced). |
| `test/test_sparselistobject.cpp` | **Create** | API parity, Small/AVL/promotion, persistence, `isEqual`/`getHash`, object integration. |
| `test/SparseListObjectIteratorTests.cpp` | **Create** | Iterator (D1 variant). |
| `test/SparseListObjectGCTests.cpp` | **Create** | GC safety under a low heap limit; concurrent versions while the GC runs. |
| `test/HashedCollectionTests.cpp` | **Create (only if D2 = i)** | Helper tests. |
| `CHANGELOG.md` | Modify | `[Unreleased]` → 1.3.0 entry. |

**Every** place in protoCore that switches on a sparse-list tag or `CellType`, and what the new type needs there:

| Site | What it does | PSLO action |
|---|---|---|
| `headers/proto_internal.h:230-231,248` | `POINTER_TAG_SPARSE_LIST`, `_ITERATOR`, `_SMALL` definitions | add tag 27 plus the budget comment (Task 3) |
| `headers/proto_internal.h:321-325,390-391` | `ExpectedTag` for the sparse-list impls and the iterator | add specialisations (Task 3; iterator Task 6) |
| `headers/proto_internal.h:548,558,573` | `CellType::SparseList`, `SparseListIterator`, `SparseListSmall` | append 3 values (Task 3) |
| `headers/proto_internal.h:1501,1561,1601` | `getType()` overrides | new classes override (Task 3, 6) |
| `headers/proto_internal.h:1808-1810,1833-1835` | `BigCell` union and size `static_assert`s | add new classes (Task 3, 6) |
| `headers/proto_internal.h:1864-1877` | `sparseListGetRaw` tag dispatch | none: `ProtoSparseList`-only internal helper; PSLO `getAt` already returns `nullptr` when absent |
| `core/ProtoObject.cpp:343-345` | `getPrototype` switch | add `case POINTER_TAG_SPARSE_LIST_OBJECT` (Task 5; D1 = b: tag 9 already maps) |
| `core/ProtoObject.cpp:1563-1576` | `asSparseList` tag test | none (must keep returning `nullptr` for tag 27; tested) |
| `core/ProtoObject.cpp:1584` | `asSparseListIterator` tag test | D1 = b only: add `getType()` check (Task 6b) |
| `core/ProtoObject.cpp:1504-1527` | `getHash` (virtual `Cell::getHash`, address; no sparse-list override) | none: PSLO cells inherit `Cell::getHash` exactly like `ProtoSparseList` cells |
| `core/ProtoObject.cpp:1733-1747` | `compare` (identity fallback for non-numbers and non-strings) | none |
| `core/ProtoSpace.cpp:133,143,158` | `cellTypeName` debug/diagnostic switch | add 3 names (Task 5) |
| `core/ProtoSpace.cpp:381-405` | STW thread-list scan: `POINTER_TAG_SPARSE_LIST_SMALL` vs AVL | none: `space->threads` is a `ProtoSparseList` |
| `core/ProtoSpace.cpp:418-419` | GC global roots `sparseListPrototype`, `sparseListIteratorPrototype` | add `sparseListObjectPrototype` (Task 5, D5) |
| `core/ProtoSpace.cpp:1188,1222` | prototype creation | create `sparseListObjectPrototype` (Task 5, D5) |
| `core/ProtoSparseList.cpp:46,247,384,413` | `implAsObject` tagging and the Small-form test | untouched logic (Task 2 re-expresses bodies only) |
| `test/test_smallsparselist.cpp:9-19` | form checks by tag | none |
| GC mark (`core/ProtoSpace.cpp:168-196` `pushReportedReference`, work-list loop ~`:500-530`) | generic: calls `Cell::processReferences` | none: tracing happens entirely in the new `processReferences` overrides |

No embedder source switches on `POINTER_TAG_SPARSE_LIST*` (a grep of protoPython, protoJS, protoST, protoClojure and protoScala found none).

---

### Task 1: Baseline — branch, deterministic benchmark, before-numbers

**Files:**
- Modify: `performance/sparse_list_benchmark.cpp` (whole file)
- Output (not committed): `/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/`

**Interfaces:**
- Consumes: nothing.
- Produces: `sparse_list_benchmark` that prints `VERIFIED` and exits 0 only when every phase's result matches a `std::map` model. It also produces baseline files `perf-sparse-before.txt`, `perf-object-before.txt`, and one `embedders-before-*.txt` per embedder.

- [ ] **Step 1: Create the branch and the scratch directory**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore status --short
git -C /home/gamarino/Documentos/proyectos/protoCore switch -c feature/pslo-p1
mkdir -p /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo
```
Expected: clean status (if it is not clean, stop and ask), and the new branch is created.

- [ ] **Step 2: Replace the benchmark with a deterministic, self-verifying version**

`performance/sparse_list_benchmark.cpp`:

```cpp
/*
 * sparse_list_benchmark.cpp
 *
 * Deterministic ProtoSparseList benchmark.  Every phase is checked against
 * a std::map model; the program prints VERIFIED and exits 0 only when all
 * phases match, so a silent failure can never be mistaken for a fast run.
 */

#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <vector>
#include "../headers/protoCore.h"

using namespace proto;

namespace {
    struct IterationSum {
        long long sum;
        unsigned long count;
    };

    void accumulate(ProtoContext* c, void* self, unsigned long key, const ProtoObject* value) {
        auto* s = static_cast<IterationSum*>(self);
        s->sum += static_cast<long long>(key) + value->asLong(c);
        s->count++;
    }

    double seconds(std::chrono::high_resolution_clock::time_point a,
                   std::chrono::high_resolution_clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    }
}

int main() {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;

    const int numIterations = 100000;
    const unsigned long keyRange = 10000;
    std::mt19937 gen(12345);
    std::uniform_int_distribution<unsigned long> distrib(0, keyRange);

    std::vector<unsigned long> keys(numIterations);
    std::vector<const ProtoObject*> values(numIterations);
    std::map<unsigned long, long long> model;
    for (int i = 0; i < numIterations; ++i) {
        keys[i] = distrib(gen);
        values[i] = c->fromInteger(i);
        model[keys[i]] = i;
    }

    std::cout << "--- Sparse List Benchmark ---" << std::endl;
    std::cout << "Iterations: " << numIterations << ", key range: " << keyRange << std::endl;

    auto t0 = std::chrono::high_resolution_clock::now();
    const ProtoSparseList* list = c->newSparseList();
    for (int i = 0; i < numIterations; ++i) list = list->setAt(c, keys[i], values[i]);
    auto t1 = std::chrono::high_resolution_clock::now();

    long long checksum = 0;
    for (int i = 0; i < numIterations; ++i) {
        const ProtoObject* v = list->getAt(c, keys[i]);
        if (v != PROTO_NONE) checksum += v->asLong(c);
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    long long expectedChecksum = 0;
    for (int i = 0; i < numIterations; ++i) expectedChecksum += model[keys[i]];

    IterationSum iter{0, 0};
    list->processElements(c, &iter, accumulate);
    auto t3 = std::chrono::high_resolution_clock::now();
    long long expectedIter = 0;
    for (const auto& [k, v] : model) expectedIter += static_cast<long long>(k) + v;
    const unsigned long pairsBeforeRemoval = model.size();

    for (int i = 0; i < numIterations; i += 2) {
        list = list->removeAt(c, keys[i]);
        model.erase(keys[i]);
    }
    auto t4 = std::chrono::high_resolution_clock::now();

    const bool okAccess = checksum == expectedChecksum;
    const bool okIter = iter.sum == expectedIter && iter.count == pairsBeforeRemoval;
    const bool okRemove = list->getSize(c) == model.size();

    std::cout << "Insertion time: " << seconds(t0, t1) << " s" << std::endl;
    std::cout << "Access time: " << seconds(t1, t2) << " s" << std::endl;
    std::cout << "Iteration time: " << seconds(t2, t3) << " s" << std::endl;
    std::cout << "Removal time: " << seconds(t3, t4) << " s" << std::endl;
    std::cout << "Access checksum: " << checksum << " (expected " << expectedChecksum << ")" << std::endl;
    std::cout << "Iteration sum: " << iter.sum << " over " << iter.count << " pairs (expected " << expectedIter << ")" << std::endl;
    std::cout << "Size after removal: " << list->getSize(c) << " (expected " << model.size() << ")" << std::endl;
    const bool ok = okAccess && okIter && okRemove;
    std::cout << (ok ? "VERIFIED" : "MISMATCH") << std::endl;
    return ok ? 0 : 1;
}
```

- [ ] **Step 3: Build and check that the benchmark verifies**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/sparse_list_benchmark
```
Expected: the last line is `VERIFIED` and the exit code is 0.

- [ ] **Step 4: Run the full protoCore suite (baseline)**

```bash
ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_release --output-on-failure 2>&1 | tail -30 | tee /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/protocore-tests-before.txt
```
Expected: record the pass/fail list. Tests that already fail here are pre-existing failures and are not attributed to this work.

- [ ] **Step 5: Record the performance baseline on the unmodified library**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo; B=/home/gamarino/Documentos/proyectos/protoCore/build_release
perf stat -r 3 -e cycles,instructions $B/sparse_list_benchmark > $S/sparse-before.out 2> $S/perf-sparse-before.txt
perf stat -r 3 -e cycles,instructions $B/object_access_benchmark > $S/object-before.out 2> $S/perf-object-before.txt
grep -c VERIFIED $S/sparse-before.out; grep -c "Checksum verified" $S/object-before.out
```
Expected: `3` and `3` (every run verified). The two `perf-*-before.txt` files contain the mean cycles with `+-` variance.

- [ ] **Step 6: Record embedder test baselines (before any protoCore change)**

Ask the user before running this step (it is long). protoJS runs sequentially.

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
ctest --test-dir $P/protoPython/build_release --output-on-failure 2>&1 | tail -15 > $S/embedders-before-protoPython.txt
ctest --test-dir $P/protoJS/build_release -j1 --output-on-failure 2>&1 | tail -15 > $S/embedders-before-protoJS.txt
ctest --test-dir $P/protoST/build_release --output-on-failure 2>&1 | tail -15 > $S/embedders-before-protoST.txt
ctest --test-dir $P/protoClojure/build_release -j1 --output-on-failure 2>&1 | tail -15 > $S/embedders-before-protoClojure.txt
ctest --test-dir $P/protoScala/build_release --output-on-failure 2>&1 | tail -15 > $S/embedders-before-protoScala.txt
```
Expected: one summary per embedder. These are the "before" numbers for Task 12.

- [ ] **Step 7: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add performance/sparse_list_benchmark.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "perf: make sparse_list_benchmark deterministic and self-verifying

Fixed seed, removal and iteration phases, and a std::map model check;
prints VERIFIED and exits non-zero on any mismatch.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Extract the sparse-list algorithms into templates (ProtoSparseList unchanged in behaviour and speed)

**Files:**
- Create: `core/SparseListAlgorithms.h`
- Modify: `core/ProtoSparseList.cpp:62-216,219-232,280-404,409-541` (bodies only)
- Modify: `headers/proto_internal.h:1490-1501` and `:1554-1561` (add a `KeyType` alias only)
- Test: `test/SparseListCharacterizationTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `proto::sparse_avl`, file-local instantiation):
  - `uintptr_t keyWord(unsigned long)`, `uintptr_t keyWord(const ProtoObject*)`
  - `template<class Node> unsigned long nodeSize(const Node*)`, `int nodeHeight(const Node*)`, `const Node* makeEmpty<Node>(ProtoContext*)`
  - `template<class Node> const ProtoObject* getAt(const Node* root, typename Node::KeyType key)`
  - `template<class Node> const Node* setAt(ProtoContext*, const Node* self, typename Node::KeyType key, const ProtoObject* value)`
  - `template<class Node> const Node* removeAt(ProtoContext*, const Node* self, typename Node::KeyType key)`
  - `template<class Node> const Node* findMin(const Node*)`
  - `template<class Node, class Fn> void inorder(const Node*, Fn& fn)` (`fn(key, value)`; allocates nothing)
  - `template<class Node, class Iter> const Iter* iteratorWithQueue(ProtoContext*, const Node*, const Iter* queue)`
  - `template<class Small> unsigned long smallCount(const Small*)`, `bool smallHas(const Small*, typename Small::KeyType)`, `const ProtoObject* smallGetAt(const Small*, typename Small::KeyType)`, `bool smallPairAt(const Small*, unsigned i, typename Small::KeyType* outKey, const ProtoObject** outValue)`
  - `template<class Small, class Node> const Node* smallPromote(ProtoContext*, const Small*)`
  - `template<class Small, class Node> const ProtoObject* smallSetAt(ProtoContext*, const Small*, typename Small::KeyType key, const ProtoObject* value)` (returns the tagged handle of the result, via `implAsObject`)
  - Node requirements: `using KeyType`; fields `key`, `value`, `previous`, `next`, `size`, `height`, `isEmpty`; ctor `(ProtoContext*, KeyType, const ProtoObject*, const Node*, const Node*, bool empty)`. Small requirements: `using KeyType`; `MAX_INLINE`; `keys[]`, `values[]`; ctor `(ProtoContext*, unsigned n, const KeyType* keys, const ProtoObject* const* values)`.

- [ ] **Step 1: Write the characterization test (it must pass on the current code)**

`test/SparseListCharacterizationTests.cpp`:

```cpp
// SparseListCharacterizationTests.cpp — locks ProtoSparseList's observable
// behaviour (contents, form, order, iterator output, AVL balance) before its
// algorithms are shared with ProtoSparseListObject.  Must pass unchanged
// before and after the refactor.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <map>
#include <random>
#include <utility>
#include <vector>

using namespace proto;

namespace {
    using PairVec = std::vector<std::pair<unsigned long, long long>>;

    void collect(ProtoContext* c, void* self, unsigned long key, const ProtoObject* value) {
        static_cast<PairVec*>(self)->emplace_back(key, value->asLong(c));
    }

    bool isSmallForm(const ProtoSparseList* sl) {
        ProtoObjectPointer pa{};
        pa.oid = reinterpret_cast<const ProtoObject*>(sl);
        return pa.op.pointer_tag == POINTER_TAG_SPARSE_LIST_SMALL;
    }

    PairVec viaIterator(ProtoContext* c, const ProtoSparseList* sl) {
        PairVec out;
        const ProtoSparseListIterator* it = sl->getIterator(c);
        while (it && it->hasNext(c)) {
            out.emplace_back(it->nextKey(c), it->nextValue(c)->asLong(c));
            it = const_cast<ProtoSparseListIterator*>(it)->advance(c);
        }
        return out;
    }
}

TEST(SparseListCharacterization, RandomOperationsMatchAnOrderedModel) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    std::mt19937 gen(20260922);
    std::uniform_int_distribution<unsigned long> keyDist(0, 40);
    std::uniform_int_distribution<int> opDist(0, 9);

    std::map<unsigned long, long long> model;
    const ProtoSparseList* sl = c->newSparseList();
    bool promoted = false;   // Small never demotes once it became AVL

    for (int step = 0; step < 4000; ++step) {
        const unsigned long k = keyDist(gen);
        if (opDist(gen) < 7) {
            sl = sl->setAt(c, k, c->fromInteger(step));
            model[k] = step;
            if (model.size() > ProtoSparseListSmallImplementation::MAX_INLINE || k == 0) promoted = true;
        } else {
            sl = sl->removeAt(c, k);
            model.erase(k);
        }
        ASSERT_EQ(isSmallForm(sl), !promoted) << "step " << step;
        ASSERT_EQ(sl->getSize(c), model.size()) << "step " << step;

        if (step % 50 == 0) {
            for (unsigned long q = 0; q <= 40; ++q) {
                auto it = model.find(q);
                ASSERT_EQ(sl->has(c, q), it != model.end()) << "key " << q;
                const ProtoObject* v = sl->getAt(c, q);
                if (it == model.end()) ASSERT_EQ(v, PROTO_NONE);
                else ASSERT_EQ(v->asLong(c), it->second);
            }
            PairVec expected(model.begin(), model.end());
            PairVec visited;
            sl->processElements(c, &visited, collect);
            ASSERT_EQ(visited, expected);
            ASSERT_EQ(viaIterator(c, sl), expected);
        }
    }
}

TEST(SparseListCharacterization, SequentialInsertionStaysBalanced) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseList* sl = c->newSparseList();
    for (unsigned long k = 1; k <= 10000; ++k) sl = sl->setAt(c, k, c->fromInteger(k));
    ASSERT_FALSE(isSmallForm(sl));
    const auto* root = toImpl<const ProtoSparseListImplementation>(sl);
    EXPECT_EQ(root->size, 10000u);
    EXPECT_LE(root->height, 20u);   // AVL bound: 1.44 * log2(10002) ≈ 19.1
    for (unsigned long k = 1; k <= 10000; k += 2) sl = sl->removeAt(c, k);
    EXPECT_EQ(sl->getSize(c), 5000u);
    EXPECT_LE(toImpl<const ProtoSparseListImplementation>(sl)->height, 20u);
}

TEST(SparseListCharacterization, SetAtNullptrRemovesAndSameValueKeepsAvlHandle) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseList* sl = c->newSparseList();
    for (unsigned long k = 1; k <= 8; ++k) sl = sl->setAt(c, k, c->fromInteger(k));
    const ProtoObject* five = sl->getAt(c, 5);
    EXPECT_EQ(sl->setAt(c, 5, five), sl);           // AVL: same value → same tree
    const ProtoSparseList* removed = sl->setAt(c, 5, nullptr);
    EXPECT_FALSE(removed->has(c, 5));
    EXPECT_EQ(removed->getSize(c), 7u);
    EXPECT_TRUE(sl->has(c, 5));                      // old version intact
}
```

- [ ] **Step 2: Run it on the unmodified code**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListCharacterization.*'
```
Expected: 3 tests PASS. This is a characterization of existing behaviour. If any fails, stop: the model is wrong and must be fixed before refactoring.

- [ ] **Step 3: Commit the characterization test**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add test/SparseListCharacterizationTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "test: characterize ProtoSparseList behaviour before sharing its algorithms

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 4: Add `KeyType` aliases (no layout change)**

In `headers/proto_internal.h`, in `class ProtoSparseListImplementation` (`:1490`), add as the first line after `public:`:

```cpp
        using KeyType = unsigned long;
```
and in `class ProtoSparseListSmallImplementation` (`:1554`), after `public:`:

```cpp
        using KeyType = unsigned long;
```

- [ ] **Step 5: Create `core/SparseListAlgorithms.h`**

```cpp
/*
 * SparseListAlgorithms.h — internal to protoCore (never installed).
 *
 * The persistent AVL and Small-form algorithms shared by ProtoSparseList
 * (key: unsigned long) and ProtoSparseListObject (key: const ProtoObject*).
 * Keys are ordered and compared by their machine word, so both types have
 * one algorithmic implementation.
 *
 * Everything lives in an anonymous namespace: each translation unit gets its
 * own internal-linkage instantiation, so calls stay direct (no PLT) and no
 * symbol is added to libprotoCore's exported interface — exactly the linkage
 * the former file-local helpers in ProtoSparseList.cpp had.
 *
 * Callers wrap every mutator in a ProtoContext::CriticalSection.
 */

#ifndef PROTOCORE_SPARSE_LIST_ALGORITHMS_H
#define PROTOCORE_SPARSE_LIST_ALGORITHMS_H

#include "../headers/proto_internal.h"
#include <algorithm>
#include <cstdint>

namespace proto::sparse_avl {
namespace {

    inline uintptr_t keyWord(unsigned long k) { return k; }
    inline uintptr_t keyWord(const ProtoObject* k) { return reinterpret_cast<uintptr_t>(k); }

    template<class Node>
    inline unsigned long nodeSize(const Node* node) {
        if (!node || (reinterpret_cast<uintptr_t>(node) & 0x3F) != 0) return 0;
        return node->size;
    }

    template<class Node>
    inline int nodeHeight(const Node* node) {
        if (!node || (reinterpret_cast<uintptr_t>(node) & 0x3F) != 0) return 0;
        return node->height;
    }

    template<class Node>
    inline int balanceOf(const Node* node) {
        if (!node || node->isEmpty) return 0;
        return nodeHeight(node->previous) - nodeHeight(node->next);
    }

    template<class Node>
    inline const Node* makeEmpty(ProtoContext* context) {
        return new(context) Node(context, typename Node::KeyType{}, nullptr, nullptr, nullptr, true);
    }

    template<class Node>
    const Node* rightRotate(ProtoContext* context, const Node* y) {
        const Node* x = y->previous;
        const Node* t2 = x->next;
        auto* newY = new(context) Node(context, y->key, y->value, t2, y->next, false);
        return new(context) Node(context, x->key, x->value, x->previous, newY, false);
    }

    template<class Node>
    const Node* leftRotate(ProtoContext* context, const Node* x) {
        const Node* y = x->next;
        const Node* t2 = y->previous;
        auto* newX = new(context) Node(context, x->key, x->value, x->previous, t2, false);
        return new(context) Node(context, y->key, y->value, newX, y->next, false);
    }

    template<class Node>
    const Node* rebalance(ProtoContext* context, const Node* node) {
        const int balance = balanceOf(node);
        if (balance > 1) {                                   // left heavy
            if (balanceOf(node->previous) < 0) {             // left-right
                const Node* newPrev = leftRotate(context, node->previous);
                return rightRotate(context, new(context) Node(context, node->key, node->value, newPrev, node->next, false));
            }
            return rightRotate(context, node);               // left-left
        }
        if (balance < -1) {                                  // right heavy
            if (balanceOf(node->next) > 0) {                 // right-left
                const Node* newNext = rightRotate(context, node->next);
                return leftRotate(context, new(context) Node(context, node->key, node->value, node->previous, newNext, false));
            }
            return leftRotate(context, node);                // right-right
        }
        return node;
    }

    template<class Node>
    const ProtoObject* getAt(const Node* root, typename Node::KeyType key) {
        const uintptr_t w = keyWord(key);
        const Node* node = root;
        while (node) {
            if (node->isEmpty) break;
            const uintptr_t nk = keyWord(node->key);
            if (w < nk) node = node->previous;
            else if (w > nk) node = node->next;
            else return node->value;
        }
        return nullptr;
    }

    template<class Node>
    const Node* removeAt(ProtoContext* context, const Node* self, typename Node::KeyType key);

    template<class Node>
    const Node* setAt(ProtoContext* context, const Node* self, typename Node::KeyType key, const ProtoObject* newValue) {
        if (newValue == nullptr) return removeAt(context, self, key);
        if (self->isEmpty) return new(context) Node(context, key, newValue, nullptr, nullptr, false);

        const uintptr_t w = keyWord(key);
        const uintptr_t sk = keyWord(self->key);
        const Node* newNode;
        if (w < sk) {
            const Node* newPrev = self->previous
                ? setAt(context, self->previous, key, newValue)
                : new(context) Node(context, key, newValue, nullptr, nullptr, false);
            newNode = new(context) Node(context, self->key, self->value, newPrev, self->next, false);
        } else if (w > sk) {
            const Node* newNext = self->next
                ? setAt(context, self->next, key, newValue)
                : new(context) Node(context, key, newValue, nullptr, nullptr, false);
            newNode = new(context) Node(context, self->key, self->value, self->previous, newNext, false);
        } else {
            if (self->value == newValue) return self;
            newNode = new(context) Node(context, self->key, newValue, self->previous, self->next, false);
        }
        return rebalance(context, newNode);
    }

    template<class Node>
    const Node* findMin(const Node* node) {
        while (node && node->previous && !node->previous->isEmpty) node = node->previous;
        return node;
    }

    template<class Node>
    const Node* removeAt(ProtoContext* context, const Node* self, typename Node::KeyType key) {
        if (self->isEmpty) return self;

        const uintptr_t w = keyWord(key);
        const uintptr_t sk = keyWord(self->key);
        const Node* newNode;
        if (w < sk) {
            if (!self->previous) return self;
            const Node* newPrev = removeAt(context, self->previous, key);
            if (newPrev == self->previous) return self;
            newNode = new(context) Node(context, self->key, self->value, newPrev, self->next, false);
        } else if (w > sk) {
            if (!self->next) return self;
            const Node* newNext = removeAt(context, self->next, key);
            if (newNext == self->next) return self;
            newNode = new(context) Node(context, self->key, self->value, self->previous, newNext, false);
        } else {
            if (!self->previous || self->previous->isEmpty) {
                if (!self->next || self->next->isEmpty) return makeEmpty<Node>(context);
                return self->next;
            }
            if (!self->next || self->next->isEmpty) return self->previous;
            const Node* successor = findMin(self->next);
            const Node* newNext = removeAt(context, self->next, successor->key);
            newNode = new(context) Node(context, successor->key, successor->value, self->previous, newNext, false);
        }
        return rebalance(context, newNode);
    }

    // In-order walk; allocates nothing, so it needs no critical section.
    template<class Node, class Fn>
    void inorder(const Node* node, Fn& fn) {
        while (node && !node->isEmpty) {
            inorder(node->previous, fn);
            fn(node->key, node->value);
            node = node->next;
        }
    }

    template<class Node, class Iter>
    const Iter* iteratorWithQueue(ProtoContext* context, const Node* self, const Iter* queue) {
        if (self->isEmpty) return queue;
        const Node* node = self;
        const Iter* stack = queue;
        while (node && !node->isEmpty) {
            stack = new(context) Iter(context, ITERATOR_NEXT_THIS, node, stack);
            node = node->previous;
        }
        return stack;
    }

    //--- Small inline form.  keyWord(keys[i]) == 0 marks an empty slot. ---

    template<class Small>
    unsigned long smallCount(const Small* s) {
        unsigned long c = 0;
        for (unsigned i = 0; i < Small::MAX_INLINE; ++i)
            if (keyWord(s->keys[i]) != 0) ++c;
        return c;
    }

    template<class Small>
    bool smallHas(const Small* s, typename Small::KeyType key) {
        const uintptr_t w = keyWord(key);
        if (w == 0) return false;
        for (unsigned i = 0; i < Small::MAX_INLINE; ++i)
            if (keyWord(s->keys[i]) == w) return true;
        return false;
    }

    template<class Small>
    const ProtoObject* smallGetAt(const Small* s, typename Small::KeyType key) {
        const uintptr_t w = keyWord(key);
        if (w == 0) return nullptr;
        for (unsigned i = 0; i < Small::MAX_INLINE; ++i)
            if (keyWord(s->keys[i]) == w) return s->values[i];
        return nullptr;
    }

    template<class Small>
    void sortPairs(typename Small::KeyType* ks, const ProtoObject** vs, unsigned n) {
        for (unsigned i = 1; i < n; ++i) {
            const auto ki = ks[i];
            const ProtoObject* vi = vs[i];
            unsigned j = i;
            while (j > 0 && keyWord(ks[j - 1]) > keyWord(ki)) {
                ks[j] = ks[j - 1];
                vs[j] = vs[j - 1];
                --j;
            }
            ks[j] = ki;
            vs[j] = vi;
        }
    }

    template<class Small>
    bool smallPairAt(const Small* s, unsigned i, typename Small::KeyType* outKey, const ProtoObject** outValue) {
        if (i >= Small::MAX_INLINE) return false;
        typename Small::KeyType ks[Small::MAX_INLINE];
        const ProtoObject* vs[Small::MAX_INLINE];
        unsigned n = 0;
        for (unsigned j = 0; j < Small::MAX_INLINE; ++j) {
            if (keyWord(s->keys[j]) != 0) {
                ks[n] = s->keys[j];
                vs[n] = s->values[j];
                ++n;
            }
        }
        sortPairs<Small>(ks, vs, n);
        if (i >= n) return false;
        if (outKey) *outKey = ks[i];
        if (outValue) *outValue = vs[i];
        return true;
    }

    template<class Small, class Node>
    const Node* smallPromote(ProtoContext* context, const Small* s) {
        const unsigned long n = smallCount(s);
        const Node* avl = makeEmpty<Node>(context);
        for (unsigned i = 0; i < n; ++i) {
            typename Small::KeyType k;
            const ProtoObject* v;
            if (smallPairAt(s, i, &k, &v)) avl = setAt(context, avl, k, v);
        }
        return avl;
    }

    // setAt on a Small (value == nullptr means removeAt).  Returns the tagged
    // handle of a fresh Small, or of an AVL when the result exceeds
    // MAX_INLINE or the key word is 0 (the empty-slot sentinel).
    template<class Small, class Node>
    const ProtoObject* smallSetAt(ProtoContext* context, const Small* small,
                                  typename Small::KeyType key, const ProtoObject* value) {
        using Key = typename Small::KeyType;
        constexpr unsigned M = Small::MAX_INLINE;
        const uintptr_t w = keyWord(key);

        if (w == 0 && value != nullptr) {
            const Node* avl = smallPromote<Small, Node>(context, small);
            return setAt(context, avl, key, value)->implAsObject(context);
        }

        if (value == nullptr) {
            Key ks[M];
            const ProtoObject* vs[M];
            unsigned n = 0;
            for (unsigned i = 0; i < M; ++i) {
                const uintptr_t kw = keyWord(small->keys[i]);
                if (kw != 0 && kw != w) {
                    ks[n] = small->keys[i];
                    vs[n] = small->values[i];
                    ++n;
                }
            }
            sortPairs<Small>(ks, vs, n);
            return (new(context) Small(context, n, ks, vs))->implAsObject(context);
        }

        Key ks[M + 1];
        const ProtoObject* vs[M + 1];
        unsigned n = 0;
        bool replaced = false;
        for (unsigned i = 0; i < M; ++i) {
            const uintptr_t kw = keyWord(small->keys[i]);
            if (kw == 0) continue;
            if (kw == w) {
                ks[n] = key;
                vs[n] = value;
                replaced = true;
            } else {
                ks[n] = small->keys[i];
                vs[n] = small->values[i];
            }
            ++n;
        }
        if (!replaced) {
            ks[n] = key;
            vs[n] = value;
            ++n;
        }
        sortPairs<Small>(ks, vs, n);
        if (n <= M) return (new(context) Small(context, n, ks, vs))->implAsObject(context);

        const Node* avl = makeEmpty<Node>(context);
        for (unsigned i = 0; i < n; ++i) avl = setAt(context, avl, ks[i], vs[i]);
        return avl->implAsObject(context);
    }

}  // anonymous namespace
}  // namespace proto::sparse_avl

#endif  // PROTOCORE_SPARSE_LIST_ALGORITHMS_H
```

- [ ] **Step 6: Re-express `ProtoSparseList.cpp` bodies on the templates**

In `core/ProtoSparseList.cpp`:
1. Add `#include "SparseListAlgorithms.h"` after line 8.
2. Delete the anonymous namespace at `:62-113` (`get_node_size` … `rebalance`).
3. Replace the listed bodies with these exact versions (signatures unchanged):

```cpp
    ProtoSparseListImplementation::ProtoSparseListImplementation(ProtoContext* context, unsigned long k, const ProtoObject* v, const ProtoSparseListImplementation* p, const ProtoSparseListImplementation* n, bool empty)
        : Cell(context), key(k), value(v), previous(p), next(n),
          // P8 — `hash` is retained for ABI / cell-layout stability only.
          hash(0),
          size(empty ? 0 : (v != nullptr) + sparse_avl::nodeSize(p) + sparse_avl::nodeSize(n)),
          height(empty ? 0 : 1 + std::max(sparse_avl::nodeHeight(p), sparse_avl::nodeHeight(n))),
          isEmpty(empty) {}

    bool ProtoSparseListImplementation::implHas(ProtoContext* context, unsigned long offset) const {
        return implGetAt(context, offset) != nullptr;
    }

    const ProtoObject* ProtoSparseListImplementation::implGetAt(ProtoContext*, unsigned long offset) const {
        return sparse_avl::getAt(this, offset);
    }

    const ProtoSparseListImplementation* ProtoSparseListImplementation::implSetAt(ProtoContext* context, unsigned long offset, const ProtoObject* newValue) const {
        return sparse_avl::setAt(context, this, offset, newValue);
    }

    // Kept as an out-of-line function: it has external linkage today and
    // removing the symbol is not part of this change.
    const ProtoSparseListImplementation* findMin(const ProtoSparseListImplementation* node) {
        return sparse_avl::findMin(node);
    }

    const ProtoSparseListImplementation* ProtoSparseListImplementation::implRemoveAt(ProtoContext* context, unsigned long offset) const {
        return sparse_avl::removeAt(context, this, offset);
    }

    const ProtoSparseListIteratorImplementation* ProtoSparseListImplementation::implGetIteratorWithQueue(ProtoContext* context, const ProtoSparseListIteratorImplementation* queue) const {
        return sparse_avl::iteratorWithQueue(context, this, queue);
    }

    unsigned long ProtoSparseListSmallImplementation::implCount() const {
        return sparse_avl::smallCount(this);
    }

    bool ProtoSparseListSmallImplementation::implHas(ProtoContext*, unsigned long offset) const {
        return sparse_avl::smallHas(this, offset);
    }

    const ProtoObject* ProtoSparseListSmallImplementation::implGetAt(ProtoContext*, unsigned long offset) const {
        return sparse_avl::smallGetAt(this, offset);
    }

    bool ProtoSparseListSmallImplementation::implPairAt(unsigned i, unsigned long* outKey, const ProtoObject** outValue) const {
        return sparse_avl::smallPairAt(this, i, outKey, outValue);
    }

    const ProtoSparseListImplementation*
    ProtoSparseListSmallImplementation::promoteToAVL(ProtoContext* context) const {
        return sparse_avl::smallPromote<ProtoSparseListSmallImplementation, ProtoSparseListImplementation>(context, this);
    }
```

In the trampoline anonymous namespace (`:409-541`), delete `makeSmallSparseList` and replace `setAtSmall`'s body with:

```cpp
        const ProtoSparseList* setAtSmall(
            ProtoContext* context,
            const ProtoSparseListSmallImplementation* small,
            unsigned long offset,
            const ProtoObject* value)
        {
            return reinterpret_cast<const ProtoSparseList*>(
                sparse_avl::smallSetAt<ProtoSparseListSmallImplementation, ProtoSparseListImplementation>(
                    context, small, offset, value));
        }
```

Leave everything else unchanged: the constructors of the Small form, `implAsObject`, `asSparseList`, `processReferences`, the iterator class, and all public trampolines at `:544-633`.

- [ ] **Step 7: Run the characterization and existing sparse-list tests**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListCharacterization.*:*SparseList*:*Sparse*'
ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_release --output-on-failure 2>&1 | tail -15
```
Expected: every sparse-list test PASSES. The full-suite pass/fail list is identical to `protocore-tests-before.txt`.

- [ ] **Step 8: Performance gate (ProtoSparseList must not get slower)**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo; B=/home/gamarino/Documentos/proyectos/protoCore/build_release
perf stat -r 3 -e cycles,instructions $B/sparse_list_benchmark > $S/sparse-after-t2.out 2> $S/perf-sparse-after-t2.txt
perf stat -r 3 -e cycles,instructions $B/object_access_benchmark > $S/object-after-t2.out 2> $S/perf-object-after-t2.txt
grep -c VERIFIED $S/sparse-after-t2.out; grep -c "Checksum verified" $S/object-after-t2.out
grep -E "cycles" $S/perf-sparse-before.txt $S/perf-sparse-after-t2.txt $S/perf-object-before.txt $S/perf-object-after-t2.txt
```
Expected: `3`, `3`, and for each benchmark the after mean `cycles` ≤ before mean × 1.01.
- If a benchmark is 1-3 % slower, build the pre-refactor commit in a separate worktree and compare interleaved runs, 3 times each:

  ```bash
  git -C /home/gamarino/Documentos/proyectos/protoCore worktree add /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/wt-before HEAD~1
  cmake -S /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/wt-before -B /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/wt-before/build_release -DCMAKE_BUILD_TYPE=Release
  cmake --build /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/wt-before/build_release --target sparse_list_benchmark object_access_benchmark -j4
  ```
  Then alternate `perf stat -r 3 -e cycles` on `wt-before/build_release/sparse_list_benchmark` and `protoCore/build_release/sparse_list_benchmark`, three times each (same for `object_access_benchmark`).
- If it is still more than 1 % slower, **STOP and report the numbers to the maintainer**. Do not micro-optimise (lesson: micro-opts can hurt icache layout).
- Remove the worktree afterwards with `git -C /home/gamarino/Documentos/proyectos/protoCore worktree remove /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/wt-before`.

- [ ] **Step 9: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add core/SparseListAlgorithms.h core/ProtoSparseList.cpp headers/proto_internal.h
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "refactor: share ProtoSparseList AVL/Small algorithms through key-word templates

Behaviour, layout and exported symbols unchanged; characterization tests
and the sparse/object benchmarks confirm it (no cycle regression).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Tag 27, CellTypes and the PSLO node / Small cells

**Files:**
- Modify: `headers/proto_internal.h` (`:52-54`, `:84-106`, `:222-248`, `:321-325`, `:542-574`, after `:1616`, `:1800-1835`)
- Create: `core/ProtoSparseListObject.cpp` (cell part)
- Modify: `CMakeLists.txt:48` (add `core/ProtoSparseListObject.cpp`)
- Test: `test/SparseListObjectCellTests.cpp`

**Interfaces:**
- Consumes: `sparse_avl::nodeSize`, `nodeHeight` (Task 2).
- Produces:
  - `#define POINTER_TAG_SPARSE_LIST_OBJECT 27`
  - `CellType::SparseListObject`, `CellType::SparseListObjectSmall`, `CellType::SparseListObjectIterator` (appended)
  - `class ProtoSparseListObjectImplementation final : public Cell` — `using KeyType = const ProtoObject*;` fields `const ProtoObject* const key; const ProtoObject* value; const ProtoSparseListObjectImplementation* previous; const ProtoSparseListObjectImplementation* next; unsigned long size:24; unsigned long height:8; unsigned long isEmpty:1;` ctor `(ProtoContext*, const ProtoObject* k, const ProtoObject* v, const ProtoSparseListObjectImplementation* p, const ProtoSparseListObjectImplementation* n, bool empty)`
  - `class ProtoSparseListObjectSmallImplementation final : public Cell` — `using KeyType = const ProtoObject*; static constexpr unsigned MAX_INLINE = 3; const ProtoObject* keys[3]; const ProtoObject* values[3];` ctors `(ProtoContext*)` and `(ProtoContext*, unsigned n, const ProtoObject* const* keys, const ProtoObject* const* values)`
  - Both: `implAsObject` tags 27; `processReferences` reports the cell key, the cell value and the children.

- [ ] **Step 1: Write the failing test**

`test/SparseListObjectCellTests.cpp`:

```cpp
// SparseListObjectCellTests.cpp — the PSLO cells: one pointer tag for both
// forms, their own CellTypes, and processReferences reporting the key only
// when it is a cell pointer (embedded keys are never reported).

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <algorithm>
#include <vector>

using namespace proto;

namespace {
    void record(ProtoContext*, void* self, const Cell* cell) {
        static_cast<std::vector<const Cell*>*>(self)->push_back(cell);
    }
    bool contains(const std::vector<const Cell*>& v, const Cell* c) {
        return std::find(v.begin(), v.end(), c) != v.end();
    }
    const ProtoObject* cellObject(ProtoContext* c, long n) {
        return c->newList()->appendLast(c, c->fromInteger(n))->asObject(c);
    }
}

TEST(SparseListObjectCells, BothFormsShareOneTagAndHaveTheirOwnCellType) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    EXPECT_EQ(POINTER_TAG_SPARSE_LIST_OBJECT, 27);

    auto* node = new(c) ProtoSparseListObjectImplementation(c, nullptr, nullptr, nullptr, nullptr, true);
    auto* small = new(c) ProtoSparseListObjectSmallImplementation(c);
    ProtoObjectPointer a{}, b{};
    a.oid = node->implAsObject(c);
    b.oid = small->implAsObject(c);
    EXPECT_EQ(a.op.pointer_tag, static_cast<unsigned long>(POINTER_TAG_SPARSE_LIST_OBJECT));
    EXPECT_EQ(b.op.pointer_tag, static_cast<unsigned long>(POINTER_TAG_SPARSE_LIST_OBJECT));
    EXPECT_EQ(node->getType(), CellType::SparseListObject);
    EXPECT_EQ(small->getType(), CellType::SparseListObjectSmall);
    EXPECT_LE(sizeof(ProtoSparseListObjectImplementation), 64u);
    EXPECT_LE(sizeof(ProtoSparseListObjectSmallImplementation), 64u);
}

TEST(SparseListObjectCells, NodeReportsCellKeyCellValueAndChildren) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoObject* k1 = cellObject(c, 1);
    const ProtoObject* v1 = cellObject(c, 2);
    auto* leaf = new(c) ProtoSparseListObjectImplementation(c, k1, v1, nullptr, nullptr, false);

    std::vector<const Cell*> seen;
    leaf->processReferences(c, &seen, record);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_TRUE(contains(seen, ProtoObject::asCellPointer(k1)));
    EXPECT_TRUE(contains(seen, ProtoObject::asCellPointer(v1)));

    const ProtoObject* k2 = cellObject(c, 3);
    auto* parent = new(c) ProtoSparseListObjectImplementation(c, k2, c->fromInteger(5), leaf, nullptr, false);
    seen.clear();
    parent->processReferences(c, &seen, record);
    ASSERT_EQ(seen.size(), 2u);                      // embedded value is not reported
    EXPECT_TRUE(contains(seen, ProtoObject::asCellPointer(k2)));
    EXPECT_TRUE(contains(seen, leaf));
    for (const Cell* s : seen) EXPECT_EQ(reinterpret_cast<uintptr_t>(s) & 0x3F, 0u);
}

TEST(SparseListObjectCells, EmbeddedKeysAreNeverReported) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoObject* embedded[] = {
        c->fromInteger(7), c->fromInteger(-9), PROTO_TRUE, PROTO_FALSE,
        c->fromUnicodeChar(0x41), PROTO_NONE, c->fromUTF8String("ab")
    };
    for (const ProtoObject* k : embedded) {
        ASSERT_EQ(ProtoObject::asCellPointer(k), nullptr);
        auto* node = new(c) ProtoSparseListObjectImplementation(c, k, c->fromInteger(1), nullptr, nullptr, false);
        std::vector<const Cell*> seen;
        node->processReferences(c, &seen, record);
        EXPECT_TRUE(seen.empty());
    }

    const ProtoObject* ks[3] = {embedded[0], embedded[2], embedded[5]};
    const ProtoObject* vsEmbedded[3] = {c->fromInteger(1), c->fromInteger(2), c->fromInteger(3)};
    auto* smallEmbedded = new(c) ProtoSparseListObjectSmallImplementation(c, 3, ks, vsEmbedded);
    std::vector<const Cell*> seen;
    smallEmbedded->processReferences(c, &seen, record);
    EXPECT_TRUE(seen.empty());

    const ProtoObject* cellValue = cellObject(c, 4);
    const ProtoObject* cellKey = cellObject(c, 5);
    const ProtoObject* ks2[2] = {embedded[0], cellKey};
    const ProtoObject* vs2[2] = {cellValue, c->fromInteger(6)};
    auto* smallMixed = new(c) ProtoSparseListObjectSmallImplementation(c, 2, ks2, vs2);
    seen.clear();
    smallMixed->processReferences(c, &seen, record);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_TRUE(contains(seen, ProtoObject::asCellPointer(cellValue)));
    EXPECT_TRUE(contains(seen, ProtoObject::asCellPointer(cellKey)));
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
```
Expected: compile FAILURE in `SparseListObjectCellTests.cpp`, with errors such as `'POINTER_TAG_SPARSE_LIST_OBJECT' was not declared` and `'ProtoSparseListObjectImplementation' does not name a type`.

- [ ] **Step 3: Tag, budget comment, forward declarations, union, ExpectedTag, CellType**

In `headers/proto_internal.h`:

(a) After line 54 (the forward declarations):

```cpp
    class ProtoSparseListObjectImplementation;
    class ProtoSparseListObjectSmallImplementation;
    class ProtoSparseListObjectIteratorImplementation;
```

(b) In `union ProtoObjectPointer`, after line 106:

```cpp
        const ProtoSparseListObject *sparseListObject;
        const ProtoSparseListObjectImplementation *sparseListObjectImplementation;
        const ProtoSparseListObjectSmallImplementation *sparseListObjectSmallImplementation;
```

(c) After line 248 (`POINTER_TAG_SPARSE_LIST_SMALL`):

```cpp
#define POINTER_TAG_SPARSE_LIST_OBJECT  27 // ProtoSparseListObject handle: AVL node OR Small form, told apart by CellType

// ---------------------------------------------------------------------
// Tagged-pointer budget (platform-wide, scarce).
//
//   Pointer tags (6 bits, 64 values):   used 0-27 (28), free 28-63 (36)
//   Embedded types (4 bits, 16 values): used 0, 2, 3, 4, 5 (5),
//                                       free 1, 6-15 (11)
//
// Rules (protoScala/docs/platform/PSLO-SPEC.md §3):
//   1. A new pointer tag or embedded type requires the protoCore
//      maintainer's explicit approval.
//   2. A type takes at most ONE tag, for its public handle.  Internal cells
//      (tree nodes, inline forms, iterators never exposed as words) are told
//      apart by CellType through Cell::getType(), which is not scarce.
//   3. Language-level encodings reuse existing encodings (e.g. a
//      SmallInteger word carrying a hash) and never claim a tag.
//
// Tag 27 carries two cell types (ProtoSparseListObjectImplementation and
// ProtoSparseListObjectSmallImplementation); code that needs the form calls
// getType(), exactly as tag 0 does for its several cell types above.
// ---------------------------------------------------------------------
```

(d) After line 325 (the `ExpectedTag` block for sparse lists):

```cpp
    template<> struct ExpectedTag<const ProtoSparseListObjectImplementation> { static constexpr unsigned long value = POINTER_TAG_SPARSE_LIST_OBJECT; };
    template<> struct ExpectedTag<ProtoSparseListObjectImplementation> { static constexpr unsigned long value = POINTER_TAG_SPARSE_LIST_OBJECT; };

    template<> struct ExpectedTag<const ProtoSparseListObjectSmallImplementation> { static constexpr unsigned long value = POINTER_TAG_SPARSE_LIST_OBJECT; };
    template<> struct ExpectedTag<ProtoSparseListObjectSmallImplementation> { static constexpr unsigned long value = POINTER_TAG_SPARSE_LIST_OBJECT; };
```

(e) In `enum class CellType`, change the last entry at line 573 and append:

```cpp
        SparseListSmall,
        SparseListObject,
        SparseListObjectSmall,
        SparseListObjectIterator
    };
```

`headers/protoCore.h:44-46` also needs the public forward declarations used by the union. Add them after line 46:

```cpp
    class ProtoSparseListObject;
    class ProtoSparseListObjectIterator;
```

- [ ] **Step 4: Declare the cell classes**

In `headers/proto_internal.h`, after the end of `class ProtoSparseListIteratorImplementation` (line 1616):

```cpp
    /*
     * ProtoSparseListObject cells.  Identical to the ProtoSparseList cells
     * except that the key is a `const ProtoObject*` which processReferences
     * reports to the collector when it carries a cell pointer.  Keys are
     * ordered and compared by their word (sparse_avl::keyWord).  nullptr is
     * not a valid key: the Small form marks empty slots with keys[i] ==
     * nullptr.  Both forms carry POINTER_TAG_SPARSE_LIST_OBJECT; the public
     * trampolines tell them apart with getType().
     */
    class ProtoSparseListObjectImplementation final : public Cell {
    public:
        using KeyType = const ProtoObject*;

        const ProtoObject* const key;
        const ProtoObject* value;
        const ProtoSparseListObjectImplementation* previous;
        const ProtoSparseListObjectImplementation* next;
        unsigned long size: 24;
        unsigned long height: 8;
        unsigned long isEmpty: 1;

        CellType getType() const override { return CellType::SparseListObject; }

        ProtoSparseListObjectImplementation(ProtoContext* context, const ProtoObject* k, const ProtoObject* v,
                                            const ProtoSparseListObjectImplementation* p,
                                            const ProtoSparseListObjectImplementation* n, bool empty);

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };

    class ProtoSparseListObjectSmallImplementation final : public Cell {
    public:
        using KeyType = const ProtoObject*;
        static constexpr unsigned MAX_INLINE = 3;

        const ProtoObject* keys[MAX_INLINE];     // nullptr = unused slot
        const ProtoObject* values[MAX_INLINE];

        CellType getType() const override { return CellType::SparseListObjectSmall; }

        explicit ProtoSparseListObjectSmallImplementation(ProtoContext* context);
        // n <= MAX_INLINE, no nullptr key in the n-prefix, key-word ascending.
        ProtoSparseListObjectSmallImplementation(ProtoContext* context, unsigned n,
                                                 const ProtoObject* const* keys,
                                                 const ProtoObject* const* values);

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };
```

In `class BigCell` (after line 1810, `sparseListSmallCell`):

```cpp
            ProtoSparseListObjectImplementation sparseListObjectCell;
            ProtoSparseListObjectSmallImplementation sparseListObjectSmallCell;
```

After line 1835:

```cpp
    static_assert(sizeof(ProtoSparseListObjectImplementation) <= 64, "ProtoSparseListObjectImplementation exceeds 64 bytes!");
    static_assert(sizeof(ProtoSparseListObjectSmallImplementation) <= 64, "ProtoSparseListObjectSmallImplementation exceeds 64 bytes!");
```

- [ ] **Step 5: Implement the cells**

Create `core/ProtoSparseListObject.cpp`:

```cpp
/*
 * ProtoSparseListObject.cpp
 *
 * Persistent AVL map keyed by a GC-traced `const ProtoObject*` word.
 * Identical to ProtoSparseList (core/ProtoSparseList.cpp) in algorithm and
 * forms; it differs only in the key type and in processReferences, which
 * reports the key to the collector when it is a cell pointer.
 * Specification: protoScala/docs/platform/PSLO-SPEC.md.
 */

#include "../headers/proto_internal.h"
#include "SparseListAlgorithms.h"

namespace proto
{
    //=========================================================================
    // ProtoSparseListObjectImplementation (AVL node)
    //=========================================================================
    ProtoSparseListObjectImplementation::ProtoSparseListObjectImplementation(
        ProtoContext* context, const ProtoObject* k, const ProtoObject* v,
        const ProtoSparseListObjectImplementation* p, const ProtoSparseListObjectImplementation* n, bool empty)
        : Cell(context), key(k), value(v), previous(p), next(n),
          size(empty ? 0 : (v != nullptr) + sparse_avl::nodeSize(p) + sparse_avl::nodeSize(n)),
          height(empty ? 0 : 1 + std::max(sparse_avl::nodeHeight(p), sparse_avl::nodeHeight(n))),
          isEmpty(empty) {}

    const ProtoObject* ProtoSparseListObjectImplementation::implAsObject(ProtoContext*) const {
        ProtoObjectPointer p{};
        p.sparseListObjectImplementation = this;
        p.op.pointer_tag = POINTER_TAG_SPARSE_LIST_OBJECT;
        return p.oid;
    }

    void ProtoSparseListObjectImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        // The key is a reference exactly like the value: an object whose only
        // reference is this key must survive.  Embedded keys (SmallInteger,
        // booleans, chars, None, inline strings) carry no cell and are skipped.
        if (const Cell* c = ProtoObject::asCellPointer(key)) method(context, self, c);
        if (const Cell* c = ProtoObject::asCellPointer(value)) method(context, self, c);
        if (previous) method(context, self, previous);
        if (next) method(context, self, next);
    }

    //=========================================================================
    // ProtoSparseListObjectSmallImplementation (inline form, up to 3 pairs)
    //=========================================================================
    ProtoSparseListObjectSmallImplementation::ProtoSparseListObjectSmallImplementation(ProtoContext* context)
        : Cell(context)
    {
        for (unsigned i = 0; i < MAX_INLINE; ++i) {
            keys[i] = nullptr;
            values[i] = nullptr;
        }
    }

    ProtoSparseListObjectSmallImplementation::ProtoSparseListObjectSmallImplementation(
        ProtoContext* context, unsigned n, const ProtoObject* const* ks, const ProtoObject* const* vs)
        : Cell(context)
    {
        for (unsigned i = 0; i < MAX_INLINE; ++i) {
            keys[i] = i < n ? ks[i] : nullptr;
            values[i] = i < n ? vs[i] : nullptr;
        }
    }

    const ProtoObject* ProtoSparseListObjectSmallImplementation::implAsObject(ProtoContext*) const {
        ProtoObjectPointer p{};
        p.sparseListObjectSmallImplementation = this;
        p.op.pointer_tag = POINTER_TAG_SPARSE_LIST_OBJECT;
        return p.oid;
    }

    void ProtoSparseListObjectSmallImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        for (unsigned i = 0; i < MAX_INLINE; ++i) {
            if (keys[i] == nullptr) continue;   // empty slot
            if (const Cell* c = ProtoObject::asCellPointer(keys[i])) method(context, self, c);
            if (const Cell* c = ProtoObject::asCellPointer(values[i])) method(context, self, c);
        }
    }
}
```

In `CMakeLists.txt`, after `core/ProtoSparseList.cpp` (line 48):

```cmake
    core/ProtoSparseListObject.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListObjectCells.*:SparseListCharacterization.*'
```
Expected: 3 + 3 tests PASS.

- [ ] **Step 7: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add headers/proto_internal.h headers/protoCore.h core/ProtoSparseListObject.cpp CMakeLists.txt test/SparseListObjectCellTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "feat: ProtoSparseListObject cells (tag 27, CellTypes, traced keys)

One pointer tag for both forms; the tag table records the platform budget.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Public `ProtoSparseListObject` API and `ProtoContext::newSparseListObject`

**Files:**
- Modify: `headers/protoCore.h` (after `:805`; after `:1178`)
- Modify: `core/ProtoSparseListObject.cpp` (append trampolines)
- Modify: `core/ProtoContext.cpp` (after `:679`)
- Test: `test/test_sparselistobject.cpp`

**Interfaces:**
- Consumes: the Task 2 templates and the Task 3 cells.
- Produces (public, `headers/protoCore.h`):

```cpp
    class ProtoSparseListObject
    {
    public:
        bool has(ProtoContext* context, const ProtoObject* key) const;
        const ProtoObject* getAt(ProtoContext* context, const ProtoObject* key) const;      // nullptr when absent
        const ProtoSparseListObject* setAt(ProtoContext* context, const ProtoObject* key, const ProtoObject* value) const;
        const ProtoSparseListObject* removeAt(ProtoContext* context, const ProtoObject* key) const;
        bool isEqual(ProtoContext* context, const ProtoSparseListObject* other) const;
        unsigned long getSize(ProtoContext* context) const;

        const ProtoObject* asObject(ProtoContext* context) const;
        const ProtoSparseListObjectIterator* getIterator(ProtoContext* context) const;   // Task 6
        unsigned long getHash(ProtoContext* context) const;

        void processElements(ProtoContext* context, void* self,
            void (*method)(ProtoContext*, void*, const ProtoObject* key, const ProtoObject* value)) const;
        void processValues(ProtoContext* context, void* self,
            void (*method)(ProtoContext*, void*, const ProtoObject* value)) const;
    };
    // ProtoContext:
    const ProtoSparseListObject* newSparseListObject();
```

- [ ] **Step 1: Write the failing tests**

`test/test_sparselistobject.cpp`:

```cpp
// test_sparselistobject.cpp — API parity of ProtoSparseListObject with
// ProtoSparseList: Small and AVL forms, the promotion boundary, ordering by
// key word, persistence, isEqual/getHash, processElements/processValues.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <map>
#include <random>
#include <utility>
#include <vector>

using namespace proto;

namespace {
    CellType formOf(const ProtoSparseListObject* m) {
        return reinterpret_cast<const Cell*>(reinterpret_cast<uintptr_t>(m) & ~0x3FUL)->getType();
    }

    using PairVec = std::vector<std::pair<const ProtoObject*, const ProtoObject*>>;
    void collect(ProtoContext*, void* self, const ProtoObject* k, const ProtoObject* v) {
        static_cast<PairVec*>(self)->emplace_back(k, v);
    }
    void collectValue(ProtoContext*, void* self, const ProtoObject* v) {
        static_cast<std::vector<const ProtoObject*>*>(self)->push_back(v);
    }
}

class SparseListObjectTest : public ::testing::Test {
protected:
    ProtoSpace* space;
    ProtoContext* c;
    void SetUp() override { space = new ProtoSpace(); c = space->rootContext; }
    void TearDown() override { delete space; }
    const ProtoObject* I(long v) { return c->fromInteger(v); }
    const ProtoObject* obj() { return c->newObject(false); }
};

TEST_F(SparseListObjectTest, NewIsAnEmptySmallWithTheNewTag) {
    const ProtoSparseListObject* m = c->newSparseListObject();
    ProtoObjectPointer pa{};
    pa.oid = m->asObject(c);
    EXPECT_EQ(pa.op.pointer_tag, static_cast<unsigned long>(POINTER_TAG_SPARSE_LIST_OBJECT));
    EXPECT_EQ(formOf(m), CellType::SparseListObjectSmall);
    EXPECT_EQ(m->getSize(c), 0u);
    EXPECT_FALSE(m->has(c, obj()));
}

TEST_F(SparseListObjectTest, SmallHoldsThreeThenPromotesAndNeverDemotes) {
    const ProtoObject* k[5] = {obj(), obj(), obj(), obj(), obj()};
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (int i = 0; i < 3; ++i) {
        m = m->setAt(c, k[i], I(i));
        EXPECT_EQ(formOf(m), CellType::SparseListObjectSmall) << i;
    }
    m = m->setAt(c, k[3], I(3));
    EXPECT_EQ(formOf(m), CellType::SparseListObject);
    EXPECT_EQ(m->getSize(c), 4u);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(m->getAt(c, k[i]), I(i));
    m = m->removeAt(c, k[0]);
    EXPECT_EQ(formOf(m), CellType::SparseListObject);    // mirrors ProtoSparseList
    EXPECT_EQ(m->getSize(c), 3u);
    EXPECT_FALSE(m->has(c, k[0]));
}

TEST_F(SparseListObjectTest, AbsentIsNullptrAndStoredNoneIsDistinguishable) {
    const ProtoObject* a = obj();
    const ProtoObject* b = obj();
    const ProtoSparseListObject* m = c->newSparseListObject()->setAt(c, a, PROTO_NONE);
    EXPECT_EQ(m->getAt(c, a), PROTO_NONE);
    EXPECT_TRUE(m->has(c, a));
    EXPECT_EQ(m->getAt(c, b), nullptr);
    EXPECT_FALSE(m->has(c, b));
}

TEST_F(SparseListObjectTest, SetAtNullptrValueRemoves) {
    const ProtoObject* a = obj();
    const ProtoSparseListObject* m = c->newSparseListObject()->setAt(c, a, I(1));
    m = m->setAt(c, a, nullptr);
    EXPECT_FALSE(m->has(c, a));
    EXPECT_EQ(m->getSize(c), 0u);
}

// D3 = silent (recommended).  If the maintainer chose "throw", replace the
// body with EXPECT_THROW(... , std::invalid_argument) for setAt/removeAt/
// getAt/has on a nullptr key.
TEST_F(SparseListObjectTest, NullKeyIsRejected) {
    const ProtoSparseListObject* m = c->newSparseListObject()->setAt(c, obj(), I(1));
    EXPECT_FALSE(m->has(c, nullptr));
    EXPECT_EQ(m->getAt(c, nullptr), nullptr);
    EXPECT_EQ(m->setAt(c, nullptr, I(2)), m);
    EXPECT_EQ(m->removeAt(c, nullptr), m);
    EXPECT_EQ(m->getSize(c), 1u);
}

TEST_F(SparseListObjectTest, EmbeddedKeysAreStored) {
    const ProtoObject* keys[] = {I(0), I(7), I(-3), PROTO_TRUE, PROTO_FALSE, PROTO_NONE,
                                 c->fromUnicodeChar(0x263A), c->fromUTF8String("ab")};
    const ProtoSparseListObject* m = c->newSparseListObject();
    long i = 0;
    for (const ProtoObject* k : keys) m = m->setAt(c, k, I(100 + i++));
    EXPECT_EQ(m->getSize(c), 8u);
    i = 0;
    for (const ProtoObject* k : keys) EXPECT_EQ(m->getAt(c, k), I(100 + i++));
}

TEST_F(SparseListObjectTest, ElementsAreVisitedInAscendingKeyWordOrder) {
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (int i = 0; i < 50; ++i) m = m->setAt(c, (i % 2) ? obj() : I(i * 13 - 200), I(i));
    PairVec visited;
    m->processElements(c, &visited, collect);
    ASSERT_EQ(visited.size(), 50u);
    for (size_t i = 1; i < visited.size(); ++i)
        EXPECT_LT(reinterpret_cast<uintptr_t>(visited[i - 1].first), reinterpret_cast<uintptr_t>(visited[i].first));
    std::vector<const ProtoObject*> values;
    m->processValues(c, &values, collectValue);
    ASSERT_EQ(values.size(), visited.size());
    for (size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i], visited[i].second);
}

TEST_F(SparseListObjectTest, RandomOperationsMatchAnOrderedModel) {
    std::vector<const ProtoObject*> pool;
    for (int i = 0; i < 40; ++i) pool.push_back(obj());
    for (int i = 0; i < 8; ++i) pool.push_back(I(i * 1000 - 3000));
    pool.push_back(PROTO_TRUE);
    pool.push_back(PROTO_NONE);

    std::mt19937 gen(4242);
    std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
    std::uniform_int_distribution<int> op(0, 9);
    std::map<uintptr_t, std::pair<const ProtoObject*, const ProtoObject*>> model;
    const ProtoSparseListObject* m = c->newSparseListObject();
    bool promoted = false;

    for (int step = 0; step < 3000; ++step) {
        const ProtoObject* k = pool[pick(gen)];
        if (op(gen) < 7) {
            const ProtoObject* v = I(step);
            m = m->setAt(c, k, v);
            model[reinterpret_cast<uintptr_t>(k)] = {k, v};
            if (model.size() > ProtoSparseListObjectSmallImplementation::MAX_INLINE) promoted = true;
        } else {
            m = m->removeAt(c, k);
            model.erase(reinterpret_cast<uintptr_t>(k));
        }
        ASSERT_EQ(formOf(m), promoted ? CellType::SparseListObject : CellType::SparseListObjectSmall) << step;
        ASSERT_EQ(m->getSize(c), model.size()) << step;
        if (step % 25 == 0) {
            for (const ProtoObject* q : pool) {
                auto it = model.find(reinterpret_cast<uintptr_t>(q));
                ASSERT_EQ(m->has(c, q), it != model.end());
                ASSERT_EQ(m->getAt(c, q), it == model.end() ? nullptr : it->second.second);
            }
            PairVec visited;
            m->processElements(c, &visited, collect);
            PairVec expected;
            for (const auto& [w, kv] : model) expected.push_back(kv);
            ASSERT_EQ(visited, expected);
        }
    }
}

TEST_F(SparseListObjectTest, OldVersionsRemainValidAndUnchanged) {
    const ProtoObject* k[6] = {obj(), obj(), obj(), obj(), obj(), obj()};
    const ProtoSparseListObject* v0 = c->newSparseListObject();
    const ProtoSparseListObject* v1 = v0->setAt(c, k[0], I(0))->setAt(c, k[1], I(1));
    const ProtoSparseListObject* v2 = v1;
    for (int i = 2; i < 6; ++i) v2 = v2->setAt(c, k[i], I(i));   // crosses into AVL
    const ProtoSparseListObject* v3 = v2->setAt(c, k[0], I(99))->removeAt(c, k[5]);

    EXPECT_EQ(v0->getSize(c), 0u);
    EXPECT_EQ(v1->getSize(c), 2u);
    EXPECT_EQ(v1->getAt(c, k[0]), I(0));
    EXPECT_FALSE(v1->has(c, k[2]));
    EXPECT_EQ(v2->getSize(c), 6u);
    EXPECT_EQ(v2->getAt(c, k[0]), I(0));
    EXPECT_EQ(v2->getAt(c, k[5]), I(5));
    EXPECT_EQ(v3->getSize(c), 5u);
    EXPECT_EQ(v3->getAt(c, k[0]), I(99));
    EXPECT_FALSE(v3->has(c, k[5]));
}

// D4 = identity (recommended).  If the maintainer chose compare()==0, add a
// case with two distinct LargeInteger values of equal magnitude.
TEST_F(SparseListObjectTest, IsEqualAndHashIgnoreInsertionOrderAndForm) {
    const ProtoObject* k[5] = {obj(), obj(), obj(), obj(), obj()};
    const ProtoSparseListObject* a = c->newSparseListObject();
    const ProtoSparseListObject* b = c->newSparseListObject();
    for (int i = 0; i < 5; ++i) a = a->setAt(c, k[i], I(i));
    for (int i = 4; i >= 0; --i) b = b->setAt(c, k[i], I(i));
    EXPECT_TRUE(a->isEqual(c, b));
    EXPECT_EQ(a->getHash(c), b->getHash(c));
    EXPECT_FALSE(a->isEqual(c, b->setAt(c, k[2], I(42))));
    EXPECT_FALSE(a->isEqual(c, b->removeAt(c, k[2])));

    const ProtoSparseListObject* avl3 = a->removeAt(c, k[3])->removeAt(c, k[4]);   // AVL, size 3
    const ProtoSparseListObject* small3 = c->newSparseListObject()
        ->setAt(c, k[0], I(0))->setAt(c, k[1], I(1))->setAt(c, k[2], I(2));      // Small, size 3
    ASSERT_EQ(formOf(avl3), CellType::SparseListObject);
    ASSERT_EQ(formOf(small3), CellType::SparseListObjectSmall);
    EXPECT_TRUE(avl3->isEqual(c, small3));
    EXPECT_EQ(avl3->getHash(c), small3->getHash(c));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
```
Expected: compile FAILURE, `'class proto::ProtoContext' has no member named 'newSparseListObject'` and `invalid use of incomplete type 'const class proto::ProtoSparseListObject'`.

- [ ] **Step 3: Declare the public API**

In `headers/protoCore.h`, after `class ProtoSparseList { … };` (line 805):

```cpp
    /**
     * @class ProtoSparseListObject
     * @brief Persistent map from `const ProtoObject*` keys to values.
     *
     * Identical to ProtoSparseList except that the key is an object word the
     * garbage collector traces: an object whose only reference is a key stays
     * alive.  Keys are ordered and compared by their word (identity, tag
     * included); an embedded value (SmallInteger, boolean, char, None) is a
     * valid key and is never traced.  nullptr is not a valid key.  getAt
     * returns nullptr for an absent key, so a stored PROTO_NONE stays
     * distinguishable.  Every modifier returns a new version; old versions
     * remain valid.
     */
    class ProtoSparseListObject
    {
    public:
        bool has(ProtoContext* context, const ProtoObject* key) const;
        const ProtoObject* getAt(ProtoContext* context, const ProtoObject* key) const;
        const ProtoSparseListObject* setAt(ProtoContext* context, const ProtoObject* key, const ProtoObject* value) const;
        const ProtoSparseListObject* removeAt(ProtoContext* context, const ProtoObject* key) const;
        bool isEqual(ProtoContext* context, const ProtoSparseListObject* other) const;
        unsigned long getSize(ProtoContext* context) const;

        const ProtoObject* asObject(ProtoContext* context) const;
        const ProtoSparseListObjectIterator* getIterator(ProtoContext* context) const;
        unsigned long getHash(ProtoContext* context) const;

        /** Visits (key, value) in ascending key-word order.  Allocates nothing and
         *  holds no GC critical section, so `method` may allocate and run
         *  arbitrary code.  The caller keeps the collection reachable. */
        void processElements(ProtoContext* context, void* self,
            void (*method)(ProtoContext*, void*, const ProtoObject* key, const ProtoObject* value)) const;
        void processValues(ProtoContext* context, void* self,
            void (*method)(ProtoContext*, void*, const ProtoObject* value)) const;
    };
```

In `class ProtoContext`, after `const ProtoSparseList* newSparseList();` (line 1178):

```cpp
        /** Empty ProtoSparseListObject (inline Small form; promotes past 3 entries). */
        const ProtoSparseListObject* newSparseListObject();
```

- [ ] **Step 4: Implement the trampolines (append to `core/ProtoSparseListObject.cpp`, inside `namespace proto`)**

```cpp
    //=========================================================================
    // Public trampolines
    //=========================================================================
    namespace {
        using Node = ProtoSparseListObjectImplementation;
        using Small = ProtoSparseListObjectSmallImplementation;

        inline const Cell* cellOf(const ProtoSparseListObject* m) {
            return reinterpret_cast<const Cell*>(reinterpret_cast<uintptr_t>(m) & ~0x3FUL);
        }

        // One tag serves both forms (PSLO-SPEC §3 rule 2): the form is the
        // cell's CellType.
        inline bool isSmall(const ProtoSparseListObject* m) {
            return cellOf(m)->getType() == CellType::SparseListObjectSmall;
        }

        inline const Small* smallOf(const ProtoSparseListObject* m) { return toImpl<const Small>(m); }
        inline const Node* avlOf(const ProtoSparseListObject* m) { return toImpl<const Node>(m); }

        inline const ProtoSparseListObject* handleOf(const ProtoObject* o) {
            return reinterpret_cast<const ProtoSparseListObject*>(o);
        }

        template<class Fn>
        void forEachPair(const ProtoSparseListObject* m, Fn&& fn) {
            if (isSmall(m)) {
                const Small* s = smallOf(m);
                const unsigned long n = sparse_avl::smallCount(s);
                for (unsigned i = 0; i < n; ++i) {
                    const ProtoObject* k;
                    const ProtoObject* v;
                    if (sparse_avl::smallPairAt(s, i, &k, &v)) fn(k, v);
                }
                return;
            }
            sparse_avl::inorder(avlOf(m), fn);
        }

        // 64-bit finalizer (MurmurHash3 fmix64).
        inline unsigned long mixWord(unsigned long x) {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdUL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53UL;
            x ^= x >> 33;
            return x;
        }
    }

    // D3 (recommended): a nullptr key is ignored.
    bool ProtoSparseListObject::has(ProtoContext* context, const ProtoObject* key) const {
        return getAt(context, key) != nullptr;
    }

    const ProtoObject* ProtoSparseListObject::getAt(ProtoContext*, const ProtoObject* key) const {
        if (!key) return nullptr;
        if (isSmall(this)) return sparse_avl::smallGetAt(smallOf(this), key);
        return sparse_avl::getAt(avlOf(this), key);
    }

    const ProtoSparseListObject* ProtoSparseListObject::setAt(ProtoContext* context, const ProtoObject* key, const ProtoObject* value) const {
        if (!key) return this;
        // GC critical section: the new path of cells is reachable only from
        // this C++ frame until the caller publishes the result.
        ProtoContext::CriticalSection cs(context);
        if (isSmall(this))
            return handleOf(sparse_avl::smallSetAt<Small, Node>(context, smallOf(this), key, value));
        return handleOf(sparse_avl::setAt(context, avlOf(this), key, value)->implAsObject(context));
    }

    const ProtoSparseListObject* ProtoSparseListObject::removeAt(ProtoContext* context, const ProtoObject* key) const {
        if (!key) return this;
        ProtoContext::CriticalSection cs(context);
        if (isSmall(this))
            return handleOf(sparse_avl::smallSetAt<Small, Node>(context, smallOf(this), key, nullptr));
        return handleOf(sparse_avl::removeAt(context, avlOf(this), key)->implAsObject(context));
    }

    unsigned long ProtoSparseListObject::getSize(ProtoContext*) const {
        if (isSmall(this)) return sparse_avl::smallCount(smallOf(this));
        return avlOf(this)->size;
    }

    const ProtoObject* ProtoSparseListObject::asObject(ProtoContext*) const {
        return reinterpret_cast<const ProtoObject*>(this);   // the handle is already tagged
    }

    // D4 (recommended): values compared by word identity.
    bool ProtoSparseListObject::isEqual(ProtoContext* context, const ProtoSparseListObject* other) const {
        if (this == other) return true;
        if (!other || getSize(context) != other->getSize(context)) return false;
        bool equal = true;
        forEachPair(this, [&](const ProtoObject* k, const ProtoObject* v) {
            if (equal && other->getAt(context, k) != v) equal = false;
        });
        return equal;
    }

    // Order-independent over (key word, value hash) pairs.
    unsigned long ProtoSparseListObject::getHash(ProtoContext* context) const {
        unsigned long h = mixWord(getSize(context));
        forEachPair(this, [&](const ProtoObject* k, const ProtoObject* v) {
            h += mixWord(sparse_avl::keyWord(k) ^ mixWord(v->getHash(context)));
        });
        return h;
    }

    void ProtoSparseListObject::processElements(ProtoContext* context, void* self,
        void (*method)(ProtoContext*, void*, const ProtoObject*, const ProtoObject*)) const
    {
        forEachPair(this, [&](const ProtoObject* k, const ProtoObject* v) { method(context, self, k, v); });
    }

    void ProtoSparseListObject::processValues(ProtoContext* context, void* self,
        void (*method)(ProtoContext*, void*, const ProtoObject*)) const
    {
        forEachPair(this, [&](const ProtoObject*, const ProtoObject* v) { method(context, self, v); });
    }
```

If D4 = `compare`, replace the `isEqual` body's comparison with:

```cpp
            if (!equal) return;
            const ProtoObject* o = other->getAt(context, k);
            if (!o || o->compare(context, v) != 0) equal = false;
```

If D3 = throw, replace each `if (!key) return …;` with `if (!key) throw std::invalid_argument("ProtoSparseListObject: nullptr is not a valid key");` and add `#include <stdexcept>`.

In `core/ProtoContext.cpp`, after `newSparseList()` (line 679):

```cpp
    const ProtoSparseListObject* ProtoContext::newSparseListObject()
    {
        // Every fresh map starts as the inline Small form (all keys nullptr).
        return reinterpret_cast<const ProtoSparseListObject*>(
            (new(this) ProtoSparseListObjectSmallImplementation(this))->implAsObject(this));
    }
```

- [ ] **Step 5: Run the tests**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListObjectTest.*:SparseListObjectCells.*'
```
Expected: all PASS (10 + 3). `getIterator` is declared but not yet defined and is unused, so linking succeeds.

- [ ] **Step 6: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add headers/protoCore.h core/ProtoSparseListObject.cpp core/ProtoContext.cpp test/test_sparselistobject.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "feat: public ProtoSparseListObject API and ProtoContext::newSparseListObject

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Object-model integration — predicates, prototype, getPrototype, diagnostics, GC root

**Files:**
- Modify: `headers/protoCore.h:337-342` (predicate), `:381-382` (accessor), after `:1511` (`sparseListObjectPrototype`, D5)
- Modify: `core/ProtoObject.cpp:343-345` (`getPrototype`); add functions after `:1576`
- Modify: `core/ProtoSpace.cpp:126-161` (`cellTypeName`), `:418-419` (root), `:1188` (creation)
- Test: `test/test_sparselistobject.cpp` (append)

**Interfaces:**
- Consumes: Task 4.
- Produces: `bool ProtoObject::isSparseListObject(ProtoContext*) const;` `const ProtoSparseListObject* ProtoObject::asSparseListObject(ProtoContext*) const;` `ProtoObject* ProtoSpace::sparseListObjectPrototype` (D5 = own).

- [ ] **Step 1: Append the failing tests**

```cpp
TEST_F(SparseListObjectTest, ObjectIntegration) {
    const ProtoSparseListObject* m = c->newSparseListObject()->setAt(c, obj(), I(1));
    const ProtoObject* o = m->asObject(c);
    EXPECT_TRUE(o->isSparseListObject(c));
    EXPECT_EQ(o->asSparseListObject(c), m);
    EXPECT_EQ(o->asSparseList(c), nullptr);                 // not a ProtoSparseList
    EXPECT_FALSE(c->newSparseList()->asObject(c)->isSparseListObject(c));
    EXPECT_EQ(c->newSparseList()->asObject(c)->asSparseListObject(c), nullptr);
    EXPECT_FALSE(I(5)->isSparseListObject(c));
    // D5 = own prototype (recommended).  If D5 = shared, expect
    // space->sparseListPrototype instead.
    EXPECT_EQ(o->getPrototype(c), space->sparseListObjectPrototype);
    EXPECT_NE(space->sparseListObjectPrototype, nullptr);
    EXPECT_NE(space->sparseListObjectPrototype, space->sparseListPrototype);
    EXPECT_EQ(o->getHash(c), o->getHash(c));               // Cell::getHash, as ProtoSparseList
}

TEST_F(SparseListObjectTest, AvlFormIsAlsoRecognised) {
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (int i = 0; i < 10; ++i) m = m->setAt(c, obj(), I(i));
    ASSERT_EQ(formOf(m), CellType::SparseListObject);
    EXPECT_TRUE(m->asObject(c)->isSparseListObject(c));
    EXPECT_EQ(m->asObject(c)->getPrototype(c), space->sparseListObjectPrototype);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
```
Expected: compile FAILURE, `has no member named 'isSparseListObject'` / `'sparseListObjectPrototype'`.

- [ ] **Step 3: Declarations**

`headers/protoCore.h`, after `bool isNativeRangeIterator(ProtoContext* context) const;` (line 342):

```cpp
        bool isSparseListObject(ProtoContext* context) const;
```

After `const ProtoSparseListIterator* asSparseListIterator(ProtoContext* context) const;` (line 382):

```cpp
        const ProtoSparseListObject* asSparseListObject(ProtoContext* context) const;
```

After `ProtoObject* rangeIteratorPrototype{};` (line 1511), for D5 = own:

```cpp
        ProtoObject* sparseListObjectPrototype{};
```

- [ ] **Step 4: Implementation**

`core/ProtoObject.cpp`, in `getPrototype` after line 345:

```cpp
        case POINTER_TAG_SPARSE_LIST_OBJECT: return context->space->sparseListObjectPrototype;
```
(D5 = shared: `return context->space->sparseListPrototype;`)

After `asSparseList` (line 1576), mirroring `isSet` / `asSparseList` (they unwrap the `literalData` wrapper object):

```cpp
    bool ProtoObject::isSparseListObject(ProtoContext* context) const {
        if (!this) return false;
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag == POINTER_TAG_SPARSE_LIST_OBJECT) return true;
        if (pa.op.pointer_tag == POINTER_TAG_OBJECT) {
            const proto::ProtoString* dataName = context->space->literalData;
            const proto::ProtoObject* data = this->getAttribute(context, dataName, false);
            if (data && data != this) return data->isSparseListObject(context);
        }
        return false;
    }

    const ProtoSparseListObject* ProtoObject::asSparseListObject(ProtoContext* context) const {
        if (!this) return nullptr;
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag == POINTER_TAG_SPARSE_LIST_OBJECT)
            return reinterpret_cast<const ProtoSparseListObject*>(this);
        if (pa.op.pointer_tag == POINTER_TAG_OBJECT) {
            const proto::ProtoString* dataName = context->space->literalData;
            const proto::ProtoObject* data = this->getAttribute(context, dataName, false);
            if (data && data != this) return data->asSparseListObject(context);
        }
        return nullptr;
    }
```

`core/ProtoSpace.cpp`, in `cellTypeName` after line 158:

```cpp
                case CellType::SparseListObject: return "SparseListObject";
                case CellType::SparseListObjectSmall: return "SparseListObjectSmall";
                case CellType::SparseListObjectIterator: return "SparseListObjectIterator";
```

GC global roots, after line 419 (D5 = own; one O(1) global-structure root, the only STW addition):

```cpp
                addRootObj(space->sparseListObjectPrototype);
```

Prototype creation, after line 1188:

```cpp
        this->sparseListObjectPrototype = const_cast<ProtoObject*>(this->rootContext->newObject(false));
```

- [ ] **Step 5: Run**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListObject*:SparseListCharacterization.*'
```
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add headers/protoCore.h core/ProtoObject.cpp core/ProtoSpace.cpp test/test_sparselistobject.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "feat: ProtoSparseListObject in the object model (predicates, prototype, GC root)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Iterator (gated on D1)

**Files:**
- Modify: `headers/proto_internal.h` (iterator cell after the Task 3 classes; `ExpectedTag`; `BigCell`; `static_assert`)
- Modify: `headers/protoCore.h` (`ProtoSparseListObjectIterator` class, before `class ProtoSparseListObject`)
- Modify: `core/ProtoSparseListObject.cpp`
- D1 = b only: `core/ProtoObject.cpp:1584` and a new `asSparseListObjectIterator`; `headers/protoCore.h:382`; union member
- Test: `test/SparseListObjectIteratorTests.cpp`

**Interfaces:**
- Consumes: `sparse_avl::iteratorWithQueue`, `smallPromote` (Task 2); Task 4.
- Produces:

```cpp
    class ProtoSparseListObjectIterator {
    public:
        int hasNext(ProtoContext* context) const;
        const ProtoObject* nextKey(ProtoContext* context) const;
        const ProtoObject* nextValue(ProtoContext* context) const;
        const ProtoSparseListObjectIterator* advance(ProtoContext* context) const;
        // D1 = b only:
        const ProtoObject* asObject(ProtoContext* context) const;
    };
    // D1 = b only (ProtoObject):
    const ProtoSparseListObjectIterator* asSparseListObjectIterator(ProtoContext* context) const;
```

- [ ] **Step 1: Write the failing test** (`test/SparseListObjectIteratorTests.cpp`)

```cpp
#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <utility>
#include <vector>

using namespace proto;

namespace {
    using PairVec = std::vector<std::pair<const ProtoObject*, const ProtoObject*>>;
    void collect(ProtoContext*, void* self, const ProtoObject* k, const ProtoObject* v) {
        static_cast<PairVec*>(self)->emplace_back(k, v);
    }
    PairVec drain(ProtoContext* c, const ProtoSparseListObject* m) {
        PairVec out;
        for (const ProtoSparseListObjectIterator* it = m->getIterator(c); it && it->hasNext(c); it = it->advance(c))
            out.emplace_back(it->nextKey(c), it->nextValue(c));
        return out;
    }
}

TEST(SparseListObjectIterator, EmptyYieldsNothing) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    EXPECT_TRUE(drain(c, c->newSparseListObject()).empty());
}

TEST(SparseListObjectIterator, MatchesProcessElementsInBothForms) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (int n = 1; n <= 40; ++n) {
        m = m->setAt(c, (n % 3) ? c->newObject(false) : c->fromInteger(n), c->fromInteger(n));
        PairVec expected;
        m->processElements(c, &expected, collect);
        ASSERT_EQ(drain(c, m), expected) << "size " << n;   // n <= 3: Small, n >= 4: AVL
    }
}

#ifdef PSLO_ITERATOR_BOXED   // D1 = b: defined by this file when the maintainer chose (b)
TEST(SparseListObjectIterator, BoxedIteratorIsNotAProtoSparseListIterator) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject()->setAt(c, c->newObject(false), c->fromInteger(1));
    const ProtoObject* o = m->getIterator(c)->asObject(c);
    EXPECT_NE(o->asSparseListObjectIterator(c), nullptr);
    EXPECT_EQ(o->asSparseListIterator(c), nullptr);
    const ProtoObject* plain = c->newSparseList()->setAt(c, 1, c->fromInteger(1))->getIterator(c)->asObject(c);
    EXPECT_NE(plain->asSparseListIterator(c), nullptr);
    EXPECT_EQ(plain->asSparseListObjectIterator(c), nullptr);
}
#endif
```

For D1 = b, add `#define PSLO_ITERATOR_BOXED` at the top of this test file.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
```
Expected: compile FAILURE (`incomplete type 'const class proto::ProtoSparseListObjectIterator'`).

- [ ] **Step 3: Iterator cell (both options)**

`headers/proto_internal.h`, after `ProtoSparseListObjectSmallImplementation`:

```cpp
    class ProtoSparseListObjectIteratorImplementation final : public Cell {
    public:
        const int state;
        const ProtoSparseListObjectImplementation* current;
        const ProtoSparseListObjectIteratorImplementation* queue;

        CellType getType() const override { return CellType::SparseListObjectIterator; }

        ProtoSparseListObjectIteratorImplementation(ProtoContext* context, int s,
                                                    const ProtoSparseListObjectImplementation* c,
                                                    const ProtoSparseListObjectIteratorImplementation* q);

        int implHasNext() const;
        const ProtoObject* implNextKey() const;
        const ProtoObject* implNextValue() const;
        const ProtoSparseListObjectIteratorImplementation* implAdvance(ProtoContext* context) const;

        const ProtoObject* implAsObject(ProtoContext* context) const override;
        void processReferences(ProtoContext* context, void* self,
                               void (*method)(ProtoContext*, void*, const Cell*)) const override;
    };
```

`ExpectedTag` (after the Task 3 specialisations). D1 = a: the handle is the raw cell address (tag bits 0):

```cpp
    template<> struct ExpectedTag<const ProtoSparseListObjectIteratorImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
    template<> struct ExpectedTag<ProtoSparseListObjectIteratorImplementation> { static constexpr unsigned long value = POINTER_TAG_OBJECT; };
```
D1 = b: use `POINTER_TAG_SPARSE_LIST_ITERATOR` in both lines instead.

`BigCell`: add `ProtoSparseListObjectIteratorImplementation sparseListObjectIteratorCell;`. After the static_asserts: `static_assert(sizeof(ProtoSparseListObjectIteratorImplementation) <= 64, "ProtoSparseListObjectIteratorImplementation exceeds 64 bytes!");`

`headers/protoCore.h`, immediately before `class ProtoSparseListObject`:

```cpp
    /**
     * @class ProtoSparseListObjectIterator
     * @brief Ascending key-word iteration over a ProtoSparseListObject version.
     */
    class ProtoSparseListObjectIterator
    {
    public:
        int hasNext(ProtoContext* context) const;
        const ProtoObject* nextKey(ProtoContext* context) const;
        const ProtoObject* nextValue(ProtoContext* context) const;
        const ProtoSparseListObjectIterator* advance(ProtoContext* context) const;
    };
```
For D1 = b, also add `const ProtoObject* asObject(ProtoContext* context) const;` to that class, and add `const ProtoSparseListObjectIterator* asSparseListObjectIterator(ProtoContext* context) const;` to `ProtoObject` after line 382.

Append to `core/ProtoSparseListObject.cpp` (inside `namespace proto`):

```cpp
    //=========================================================================
    // ProtoSparseListObjectIteratorImplementation
    //=========================================================================
    ProtoSparseListObjectIteratorImplementation::ProtoSparseListObjectIteratorImplementation(
        ProtoContext* context, int s, const ProtoSparseListObjectImplementation* c,
        const ProtoSparseListObjectIteratorImplementation* q)
        : Cell(context), state(s), current(c), queue(q) {}

    int ProtoSparseListObjectIteratorImplementation::implHasNext() const {
        return state == ITERATOR_NEXT_THIS && current && !current->isEmpty;
    }

    const ProtoObject* ProtoSparseListObjectIteratorImplementation::implNextKey() const {
        return (state == ITERATOR_NEXT_THIS && current) ? current->key : nullptr;
    }

    const ProtoObject* ProtoSparseListObjectIteratorImplementation::implNextValue() const {
        return (state == ITERATOR_NEXT_THIS && current) ? current->value : nullptr;
    }

    const ProtoSparseListObjectIteratorImplementation*
    ProtoSparseListObjectIteratorImplementation::implAdvance(ProtoContext* context) const {
        if (state != ITERATOR_NEXT_THIS) return nullptr;
        if (current && current->next && !current->next->isEmpty) {
            ProtoContext::CriticalSection cs(context);
            return sparse_avl::iteratorWithQueue(context, current->next, queue);
        }
        return queue;
    }

    void ProtoSparseListObjectIteratorImplementation::processReferences(
        ProtoContext* context, void* self, void (*method)(ProtoContext*, void*, const Cell*)) const
    {
        if (current) method(context, self, current);
        if (queue) method(context, self, queue);
    }

    const ProtoSparseListObjectIterator* ProtoSparseListObject::getIterator(ProtoContext* context) const {
        ProtoContext::CriticalSection cs(context);
        const Node* root = isSmall(this)
            ? sparse_avl::smallPromote<Small, Node>(context, smallOf(this))   // as ProtoSparseList does
            : avlOf(this);
        const ProtoSparseListObjectIteratorImplementation* impl =
            sparse_avl::iteratorWithQueue(context, root,
                static_cast<const ProtoSparseListObjectIteratorImplementation*>(nullptr));
        return impl ? reinterpret_cast<const ProtoSparseListObjectIterator*>(impl->implAsObject(context)) : nullptr;
    }

    namespace {
        inline const ProtoSparseListObjectIteratorImplementation* iterImpl(const ProtoSparseListObjectIterator* it) {
            return toImpl<const ProtoSparseListObjectIteratorImplementation>(it);
        }
    }

    int ProtoSparseListObjectIterator::hasNext(ProtoContext*) const { if (!this) return 0; return iterImpl(this)->implHasNext(); }
    const ProtoObject* ProtoSparseListObjectIterator::nextKey(ProtoContext*) const { if (!this) return nullptr; return iterImpl(this)->implNextKey(); }
    const ProtoObject* ProtoSparseListObjectIterator::nextValue(ProtoContext*) const { if (!this) return nullptr; return iterImpl(this)->implNextValue(); }
    const ProtoSparseListObjectIterator* ProtoSparseListObjectIterator::advance(ProtoContext* context) const {
        if (!this) return nullptr;
        const auto* n = iterImpl(this)->implAdvance(context);
        return n ? reinterpret_cast<const ProtoSparseListObjectIterator*>(n->implAsObject(context)) : nullptr;
    }
```

**D1 = a** — `implAsObject` returns the untagged address. The public handle is that address; it is never given to the object model:

```cpp
    // D1 = (a): the iterator handle is the raw cell address and is never a
    // ProtoObject word.  This function exists only because Cell requires it;
    // it is used solely to form the C++ handle above.  No ProtoObject API
    // (getPrototype, getAttribute, asObject) is offered for this handle, so
    // no tag-0 non-ProtoObjectCell word can reach the attribute chain
    // (proto_internal.h, "Pointer tag layout", #92).
    const ProtoObject* ProtoSparseListObjectIteratorImplementation::implAsObject(ProtoContext*) const {
        return reinterpret_cast<const ProtoObject*>(this);
    }
```

**D1 = b** — tag 9 is shared:

```cpp
    const ProtoObject* ProtoSparseListObjectIteratorImplementation::implAsObject(ProtoContext*) const {
        ProtoObjectPointer p{};
        p.voidPointer = const_cast<ProtoSparseListObjectIteratorImplementation*>(this);
        p.op.pointer_tag = POINTER_TAG_SPARSE_LIST_ITERATOR;   // shared; told apart by CellType
        return p.oid;
    }
    const ProtoObject* ProtoSparseListObjectIterator::asObject(ProtoContext* context) const {
        if (!this) return nullptr;
        return iterImpl(this)->implAsObject(context);
    }
```
and in `core/ProtoObject.cpp` replace line 1584 with:

```cpp
    const ProtoSparseListIterator* ProtoObject::asSparseListIterator(ProtoContext* context) const {
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag != POINTER_TAG_SPARSE_LIST_ITERATOR) return nullptr;
        const Cell* cell = ProtoObject::asCellPointer(this);
        return cell->getType() == CellType::SparseListIterator ? reinterpret_cast<const ProtoSparseListIterator*>(this) : nullptr;
    }
    const ProtoSparseListObjectIterator* ProtoObject::asSparseListObjectIterator(ProtoContext* context) const {
        ProtoObjectPointer pa{}; pa.oid = this;
        if (pa.op.pointer_tag != POINTER_TAG_SPARSE_LIST_ITERATOR) return nullptr;
        const Cell* cell = ProtoObject::asCellPointer(this);
        return cell->getType() == CellType::SparseListObjectIterator ? reinterpret_cast<const ProtoSparseListObjectIterator*>(this) : nullptr;
    }
```
With D1 = b, the Task 2 performance gate (Step 8 commands) must be re-run after this step, because `asSparseListIterator` is on a `ProtoSparseList` path.

- [ ] **Step 4: Run**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListObject*:SparseListCharacterization.*:*SparseList*'
```
Expected: all PASS (2 iterator tests for D1 = a, 3 for D1 = b).

- [ ] **Step 5: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add headers/proto_internal.h headers/protoCore.h core/ProtoSparseListObject.cpp core/ProtoObject.cpp test/SparseListObjectIteratorTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "feat: ProtoSparseListObject iterator (no new tag; maintainer option D1)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: GC safety — keys referenced only by the collection survive forced collections

**Files:**
- Test: `test/SparseListObjectGCTests.cpp`

**Interfaces:**
- Consumes: Tasks 3-5; `ProtoSpace::setHeapLimits(int softCells, int hardCells)` (`headers/protoCore.h:1592`), `getGCCycleCount()` (`:1842`), `createRootSet`/`destroyRootSet` (`:1622-1623`), `ProtoRootSet::add/resolve/remove`, `ProtoContext::UnmanagedScope`, `space.gcStarted`.
- Produces: tests only.

- [ ] **Step 1: Write the tests**

The cycle-forcing helpers are copied from `test/GCRootScopeTests.cpp:34-60`, which are file-local there.

```cpp
// SparseListObjectGCTests.cpp — keys referenced only by a
// ProtoSparseListObject must survive collection cycles, both forms.
// Cycles are forced with a small hard heap limit (ProtoSpace::setHeapLimits),
// the pattern of test/GCRootScopeTests.cpp.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace proto;

namespace {

constexpr int kHeadroomCells = 40000;
constexpr int kGarbagePerBatch = 5000;

uint64_t forceCycles(ProtoSpace& space, ProtoContext* parent, uint64_t minCycles) {
    const uint64_t start = space.getGCCycleCount();
    space.setHeapLimits(/*soft=*/0, /*hard=*/space.heapSize + kHeadroomCells);
    for (int batch = 0; batch < 400 && space.getGCCycleCount() - start < minCycles; ++batch) {
        ProtoContext garbage(&space, parent, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < kGarbagePerBatch; ++i) (void) garbage.newObject(false);
    }
    space.setHeapLimits(0, 0);
    return space.getGCCycleCount() - start;
}

bool waitForIdleCollector(ProtoSpace& space, ProtoContext* ctx) {
    ProtoContext::UnmanagedScope parked(ctx);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (space.gcStarted.load()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// A key cell that counts every traversal by the collector.
class KeyProbeCell final : public Cell {
public:
    static std::atomic<unsigned long> traversals;
    explicit KeyProbeCell(ProtoContext* context) : Cell(context) {}
    void processReferences(ProtoContext*, void*, void (*)(ProtoContext*, void*, const Cell*)) const override {
        traversals.fetch_add(1, std::memory_order_relaxed);
    }
    const ProtoObject* implAsObject(ProtoContext*) const override { return PROTO_NONE; }
};
std::atomic<unsigned long> KeyProbeCell::traversals{0};

struct ContentCheck {
    const ProtoSparseListObject* map;
    int n;
    int visited;
    int bad;
    std::vector<bool> seen;
};

void checkPair(ProtoContext* c, void* self, const ProtoObject* key, const ProtoObject* value) {
    auto* chk = static_cast<ContentCheck*>(self);
    chk->visited++;
    const ProtoList* list = key->asList(c);
    if (!list || list->getSize(c) != 1) { chk->bad++; return; }
    const long i = list->getAt(c, 0)->asLong(c);
    if (i < 0 || i >= chk->n || chk->seen[i] || value->asLong(c) != i * 10 ||
        chk->map->getAt(c, key) != value) { chk->bad++; return; }
    chk->seen[i] = true;
}

}  // namespace

class SparseListObjectGC : public ::testing::TestWithParam<int> {};

// The key is referenced only by the collection (the map is pinned by a root
// set; the probe's allocating context has exited).  The collector must reach
// the probe through the map's processReferences.
TEST_P(SparseListObjectGC, KeyReferencedOnlyByTheCollectionIsTraced) {
    const int n = GetParam();
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoRootSet* rs = space.createRootSet("pslo-key-probe");
    ASSERT_NE(rs, nullptr);
    ProtoRootSet::Handle pinned = ProtoRootSet::kNullHandle;
    {
        ProtoContext sub(&space, &live, nullptr, nullptr, nullptr, nullptr);
        const ProtoSparseListObject* m = sub.newSparseListObject();
        for (int i = 0; i < n; ++i) {
            const ProtoObject* probe = reinterpret_cast<const ProtoObject*>(new (&sub) KeyProbeCell(&sub));
            m = m->setAt(&sub, probe, sub.fromInteger(i));
        }
        pinned = rs->add(m->asObject(&sub));
    }  // the probes are now reachable only as keys of the pinned map
    KeyProbeCell::traversals = 0;

    const uint64_t cycles = forceCycles(space, &live, 5);
    ASSERT_TRUE(waitForIdleCollector(space, &live));
    EXPECT_GE(cycles, 5u);
    EXPECT_GE(KeyProbeCell::traversals.load(), static_cast<unsigned long>(n))
        << "a key referenced only by the collection was not traced";

    const ProtoSparseListObject* m = rs->resolve(pinned)->asSparseListObject(&live);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->getSize(&live), static_cast<unsigned long>(n));
    rs->remove(pinned);
    space.destroyRootSet(rs);
}

// Real objects as keys, referenced only by the collection: after forced
// cycles every key is recovered from the map and its contents are intact.
TEST_P(SparseListObjectGC, KeysAndContentsSurviveForcedCollections) {
    const int n = GetParam();
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoRootSet* rs = space.createRootSet("pslo-contents");
    ASSERT_NE(rs, nullptr);
    ProtoRootSet::Handle pinned = ProtoRootSet::kNullHandle;
    {
        ProtoContext sub(&space, &live, nullptr, nullptr, nullptr, nullptr);
        const ProtoSparseListObject* m = sub.newSparseListObject();
        for (int i = 0; i < n; ++i) {
            const ProtoObject* key = sub.newList()->appendLast(&sub, sub.fromInteger(i))->asObject(&sub);
            m = m->setAt(&sub, key, sub.fromInteger(i * 10));
        }
        pinned = rs->add(m->asObject(&sub));
    }

    const uint64_t cycles = forceCycles(space, &live, 5);
    ASSERT_TRUE(waitForIdleCollector(space, &live));
    EXPECT_GE(cycles, 5u);

    const ProtoSparseListObject* m = rs->resolve(pinned)->asSparseListObject(&live);
    ASSERT_NE(m, nullptr);
    ContentCheck chk{m, n, 0, 0, std::vector<bool>(n, false)};
    m->processElements(&live, &chk, checkPair);
    EXPECT_EQ(chk.visited, n);
    EXPECT_EQ(chk.bad, 0);
    rs->remove(pinned);
    space.destroyRootSet(rs);
}

// 1-3 keys: Small form; 4 is the promotion boundary; 64 and 2000: AVL.
INSTANTIATE_TEST_SUITE_P(BothForms, SparseListObjectGC, ::testing::Values(1, 3, 4, 64, 2000));
```

- [ ] **Step 2: Mutation check — prove the test detects a missing key trace**

Temporarily delete the line `if (const Cell* c = ProtoObject::asCellPointer(key)) method(context, self, c);` in `ProtoSparseListObjectImplementation::processReferences`, and the `keys[i]` line in the Small form's `processReferences`. Then:

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='BothForms/SparseListObjectGC.KeyReferencedOnlyByTheCollectionIsTraced/*'
```
Expected: FAIL for all 5 parameters ("a key referenced only by the collection was not traced"). Restore both lines exactly with `git -C /home/gamarino/Documentos/proyectos/protoCore checkout core/ProtoSparseListObject.cpp`, and confirm `git diff` is empty for that file. **Never commit the mutation.**

- [ ] **Step 3: Run the real tests**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='BothForms/SparseListObjectGC.*' --gtest_repeat=5
```
Expected: 10 tests × 5 repeats PASS.

- [ ] **Step 4: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add test/SparseListObjectGCTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "test: ProtoSparseListObject keys referenced only by the map survive forced cycles

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Concurrency — threads build versions from one shared base while the GC runs

**Files:**
- Test: `test/SparseListObjectGCTests.cpp` (append)

**Interfaces:**
- Consumes: Task 4; `ProtoSpace::triggerGC()`; the thread-context pattern of `test/ConcurrentMarkSafetyTests.cpp:66-103` (`ProtoContext threadCtx{&space};`).
- Produces: a test only.

- [ ] **Step 1: Append the test**

```cpp
// Several threads derive independent versions from one shared base while a
// kicker thread keeps requesting collections.  The base must stay unchanged
// and every thread's version must hold exactly base + its own keys.
TEST(SparseListObjectConcurrency, VersionsFromASharedBaseWhileTheGcRuns) {
    ProtoSpace space;
    ProtoContext* root = space.rootContext;
    constexpr int kBase = 32;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 3000;

    std::vector<const ProtoObject*> baseKeys;
    const ProtoSparseListObject* base = root->newSparseListObject();
    for (int i = 0; i < kBase; ++i) {
        const ProtoObject* key = root->newList()->appendLast(root, root->fromInteger(-1 - i))->asObject(root);
        baseKeys.push_back(key);
        base = base->setAt(root, key, root->fromInteger(i));
    }

    std::atomic<bool> stopGc{false};
    std::atomic<unsigned long> errors{0};
    std::thread gcKicker([&]() {
        while (!stopGc.load(std::memory_order_relaxed)) {
            space.triggerGC();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            ProtoContext threadCtx{&space};
            const ProtoSparseListObject* mine = base;
            for (int i = 0; i < kPerThread; ++i) {
                const ProtoObject* key = threadCtx.newList()
                    ->appendLast(&threadCtx, threadCtx.fromInteger(t * 1000000L + i))->asObject(&threadCtx);
                mine = mine->setAt(&threadCtx, key, threadCtx.fromInteger(i));
                if (mine->getAt(&threadCtx, key) != threadCtx.fromInteger(i)) errors.fetch_add(1);
                if (i % 64 == 0) {
                    const int b = i % kBase;
                    const ProtoObject* bv = base->getAt(&threadCtx, baseKeys[b]);
                    if (base->getSize(&threadCtx) != kBase || !bv || bv->asLong(&threadCtx) != b) errors.fetch_add(1);
                    const ProtoObject* mv = mine->getAt(&threadCtx, baseKeys[b]);
                    if (!mv || mv->asLong(&threadCtx) != b) errors.fetch_add(1);
                }
            }
            if (mine->getSize(&threadCtx) != static_cast<unsigned long>(kBase + kPerThread)) errors.fetch_add(1);
        });
    }
    for (auto& th : workers) th.join();
    stopGc.store(true, std::memory_order_relaxed);
    gcKicker.join();

    EXPECT_EQ(errors.load(), 0u);
    EXPECT_EQ(base->getSize(root), static_cast<unsigned long>(kBase));
    for (int i = 0; i < kBase; ++i) EXPECT_EQ(base->getAt(root, baseKeys[i])->asLong(root), i);
}
```

- [ ] **Step 2: Run it repeatedly**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseListObjectConcurrency.*' --gtest_repeat=10
```
Expected: PASS 10/10. Any crash or non-zero `errors` is a GC-safety defect: stop and use superpowers:systematic-debugging. Do not weaken the test.

- [ ] **Step 3: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add test/SparseListObjectGCTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "test: concurrent ProtoSparseListObject versions from one base while GC runs

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Shared hashed-collection helper (ONLY if D2 = (i); code assumes D2a = ε, D2b = direct 54-bit, D2c = keep key)

If D2 = (ii), skip this task. If D2a ≠ (ε), stop and ask for a plan revision.

**Files:**
- Modify: `headers/protoCore.h` (after `class ProtoSparseListObject`)
- Create: `core/ProtoHashedCollection.cpp`
- Modify: `CMakeLists.txt` (add `core/ProtoHashedCollection.cpp` after `core/ProtoSparseListObject.cpp`)
- Test: `test/HashedCollectionTests.cpp`

**Interfaces:**
- Consumes: Task 4.
- Produces:

```cpp
    struct KeySemantics {
        bool (*isIdentityKey)(ProtoContext*, const ProtoObject* key);
        unsigned long (*hash)(ProtoContext*, const ProtoObject* key);
        bool (*equals)(ProtoContext*, const ProtoObject* a, const ProtoObject* b);
    };
    const ProtoSparseListObject* hashedPut(ProtoContext*, const ProtoSparseListObject*, const KeySemantics&, const ProtoObject* k, const ProtoObject* v);
    const ProtoObject* hashedGet(ProtoContext*, const ProtoSparseListObject*, const KeySemantics&, const ProtoObject* k);   // nullptr when absent
    const ProtoSparseListObject* hashedRemove(ProtoContext*, const ProtoSparseListObject*, const KeySemantics&, const ProtoObject* k);
    void hashedForEach(ProtoContext*, const ProtoSparseListObject*, void* self,
                       void (*fn)(ProtoContext*, void*, const ProtoObject* k, const ProtoObject* v));
```

- [ ] **Step 1: Write the failing tests** (`test/HashedCollectionTests.cpp`)

```cpp
#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <map>
#include <set>
#include <utility>

using namespace proto;

namespace {
    // Test language: integers and strings have value equality; everything
    // else is identity.
    bool testIsIdentity(ProtoContext* c, const ProtoObject* k) { return !k->isInteger(c) && !k->isString(c); }
    unsigned long testHash(ProtoContext* c, const ProtoObject* k) {
        return k->isInteger(c) ? static_cast<unsigned long>(k->asLong(c)) : k->getHash(c);
    }
    bool testEquals(ProtoContext* c, const ProtoObject* a, const ProtoObject* b) {
        if (a->isInteger(c) && b->isInteger(c)) return a->asLong(c) == b->asLong(c);
        if (a->isString(c) && b->isString(c)) return a->asString(c)->cmp_to_string(c, b->asString(c)) == 0;
        return a == b;
    }
    unsigned long constantHash(ProtoContext*, const ProtoObject*) { return 42; }

    const KeySemantics kTest{testIsIdentity, testHash, testEquals};
    const KeySemantics kColliding{testIsIdentity, constantHash, testEquals};

    struct Seen { std::multiset<std::pair<long, long>> pairs; };
    void record(ProtoContext* c, void* self, const ProtoObject* k, const ProtoObject* v) {
        static_cast<Seen*>(self)->pairs.insert({k->isInteger(c) ? k->asLong(c) : -1, v->asLong(c)});
    }
}

TEST(HashedCollection, IdentityAndHashedKeysCoexist) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoObject* o = c->newObject(false);
    const ProtoObject* s1 = c->fromUTF8String("a longer string key");
    const ProtoObject* s2 = c->fromUTF8String("a longer string key");   // equal, distinct object
    const ProtoSparseListObject* m = c->newSparseListObject();
    m = hashedPut(c, m, kTest, o, c->fromInteger(1));
    m = hashedPut(c, m, kTest, c->fromInteger(5), c->fromInteger(2));
    m = hashedPut(c, m, kTest, s1, c->fromInteger(3));
    EXPECT_EQ(hashedGet(c, m, kTest, o), c->fromInteger(1));
    EXPECT_EQ(hashedGet(c, m, kTest, c->fromInteger(5)), c->fromInteger(2));
    EXPECT_EQ(hashedGet(c, m, kTest, s2), c->fromInteger(3));           // value equality
    EXPECT_EQ(hashedGet(c, m, kTest, c->newObject(false)), nullptr);
    EXPECT_EQ(m->getSize(c), 3u);
}

TEST(HashedCollection, ForcedCollisionsKeepEveryEntry) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (long i = 1; i <= 10; ++i) m = hashedPut(c, m, kColliding, c->fromInteger(i), c->fromInteger(i * 2));
    EXPECT_EQ(m->getSize(c), 1u);                                    // one bucket
    for (long i = 1; i <= 10; ++i) EXPECT_EQ(hashedGet(c, m, kColliding, c->fromInteger(i)), c->fromInteger(i * 2));
    EXPECT_EQ(hashedGet(c, m, kColliding, c->fromInteger(11)), nullptr);
}

TEST(HashedCollection, PutWithAnEqualKeyReplacesTheValue) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject();
    m = hashedPut(c, m, kColliding, c->fromInteger(1), c->fromInteger(10));
    m = hashedPut(c, m, kColliding, c->fromInteger(2), c->fromInteger(20));
    m = hashedPut(c, m, kColliding, c->fromInteger(1), c->fromInteger(11));
    EXPECT_EQ(hashedGet(c, m, kColliding, c->fromInteger(1)), c->fromInteger(11));
    Seen seen;
    hashedForEach(c, m, &seen, record);
    EXPECT_EQ(seen.pairs, (std::multiset<std::pair<long, long>>{{1, 11}, {2, 20}}));
}

TEST(HashedCollection, RemoveFromACollisionBucket) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject();
    for (long i = 1; i <= 5; ++i) m = hashedPut(c, m, kColliding, c->fromInteger(i), c->fromInteger(i));
    const ProtoSparseListObject* before = m;
    m = hashedRemove(c, m, kColliding, c->fromInteger(3));
    EXPECT_EQ(hashedGet(c, m, kColliding, c->fromInteger(3)), nullptr);
    for (long i : {1L, 2L, 4L, 5L}) EXPECT_EQ(hashedGet(c, m, kColliding, c->fromInteger(i)), c->fromInteger(i));
    EXPECT_EQ(hashedGet(c, before, kColliding, c->fromInteger(3)), c->fromInteger(3));   // persistence
    EXPECT_EQ(hashedRemove(c, m, kColliding, c->fromInteger(99)), m);                     // absent: unchanged
    for (long i : {1L, 2L, 4L, 5L}) m = hashedRemove(c, m, kColliding, c->fromInteger(i));
    EXPECT_EQ(m->getSize(c), 0u);
}

TEST(HashedCollection, ForEachYieldsEveryPairExactlyOnce) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const ProtoSparseListObject* m = c->newSparseListObject();
    std::multiset<std::pair<long, long>> expected;
    for (long i = 0; i < 20; ++i) {
        m = hashedPut(c, m, kColliding, c->fromInteger(i), c->fromInteger(100 + i));
        expected.insert({i, 100 + i});
    }
    for (long i = 0; i < 5; ++i) {
        m = hashedPut(c, m, kColliding, c->newObject(false), c->fromInteger(200 + i));
        expected.insert({-1, 200 + i});
    }
    Seen seen;
    hashedForEach(c, m, &seen, record);
    EXPECT_EQ(seen.pairs, expected);
}

TEST(HashedCollection, IntegersAlwaysTakeTheHashedPath) {
    ProtoSpace space;
    ProtoContext* c = space.rootContext;
    const KeySemantics allIdentity{
        [](ProtoContext*, const ProtoObject*) { return true; }, testHash, testEquals};
    const ProtoSparseListObject* m = c->newSparseListObject();
    m = hashedPut(c, m, allIdentity, c->fromInteger(7), c->fromInteger(70));
    EXPECT_EQ(hashedGet(c, m, allIdentity, c->fromInteger(7)), c->fromInteger(70));
    const ProtoObject* slot = m->getAt(c, c->fromInteger(7));   // slot key = SmallInteger(7), value = [7, 70]
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(slot->asList(c), nullptr);
    EXPECT_EQ(slot->asList(c)->getSize(c), 2u);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
```
Expected: compile FAILURE, `'KeySemantics' does not name a type`, `'hashedPut' was not declared`.

- [ ] **Step 3: Declare in `headers/protoCore.h` (after `class ProtoSparseListObject`)**

```cpp
    /**
     * @brief A language's key semantics for the hashed-collection helper.
     *
     * isIdentityKey: true when the language's equality for this key is
     * identity.  hash / equals: the language's hash and equality, used only for
     * value-equality keys (equals only inside a collision bucket).  Callbacks
     * run outside any GC critical section and may allocate.
     */
    struct KeySemantics {
        bool (*isIdentityKey)(ProtoContext*, const ProtoObject* key);
        unsigned long (*hash)(ProtoContext*, const ProtoObject* key);
        bool (*equals)(ProtoContext*, const ProtoObject* a, const ProtoObject* b);
    };

    /**
     * Persistent hashed map operations over a ProtoSparseListObject
     * (protoScala/docs/platform/PSLO-SPEC.md §4):
     *  - identity key       -> slot key = the key itself, slot value = v
     *  - value-equality key -> slot key = SmallInteger word of the hash's low
     *                          54 bits, slot value = flat ProtoList
     *                          [k0, v0, k1, v1, ...] (one pair unless the hash
     *                          collides)
     * SmallInteger keys always take the value-equality path, so the two slot
     * kinds can never collide.  Putting an existing equal key keeps the stored
     * key and replaces its value.
     */
    const ProtoSparseListObject* hashedPut(ProtoContext* context, const ProtoSparseListObject* map,
                                           const KeySemantics& semantics, const ProtoObject* key, const ProtoObject* value);
    const ProtoObject* hashedGet(ProtoContext* context, const ProtoSparseListObject* map,
                                 const KeySemantics& semantics, const ProtoObject* key);
    const ProtoSparseListObject* hashedRemove(ProtoContext* context, const ProtoSparseListObject* map,
                                              const KeySemantics& semantics, const ProtoObject* key);
    void hashedForEach(ProtoContext* context, const ProtoSparseListObject* map, void* self,
                       void (*fn)(ProtoContext*, void*, const ProtoObject* key, const ProtoObject* value));
```

- [ ] **Step 4: Implement `core/ProtoHashedCollection.cpp`**

```cpp
/*
 * ProtoHashedCollection.cpp — the shared hashed-collection helper over
 * ProtoSparseListObject (PSLO-SPEC §4).  Language callbacks (hash, equals)
 * always run BEFORE any GC critical section is entered: they may allocate,
 * run user code and reach a safepoint.  Only the construction of the new
 * version runs inside the critical section.
 */

#include "../headers/proto_internal.h"

namespace proto
{
    namespace {
        constexpr unsigned long kSlotHashMask = (1UL << 54) - 1;

        // An embedded SmallInteger word carrying the hash's low 54 bits.
        // Built directly (never through fromInteger, which would allocate a
        // LargeInteger cell for values >= 2^53).
        const ProtoObject* hashSlotKey(unsigned long hash) {
            ProtoObjectPointer p{};
            p.op.pointer_tag = POINTER_TAG_EMBEDDED_VALUE;
            p.op.embedded_type = EMBEDDED_TYPE_SMALLINT;
            p.op.value = hash & kSlotHashMask;
            return p.oid;
        }

        bool isSmallIntegerWord(const ProtoObject* o) {
            ProtoObjectPointer p{};
            p.oid = o;
            return p.op.pointer_tag == POINTER_TAG_EMBEDDED_VALUE && p.op.embedded_type == EMBEDDED_TYPE_SMALLINT;
        }

        bool usesIdentitySlot(ProtoContext* context, const KeySemantics& semantics, const ProtoObject* key) {
            return !isSmallIntegerWord(key) && semantics.isIdentityKey(context, key);
        }

        // Index of the key of the pair equal to `key` in a flat bucket, or -1.
        int findInBucket(ProtoContext* context, const ProtoList* bucket,
                         const KeySemantics& semantics, const ProtoObject* key) {
            const int n = static_cast<int>(bucket->getSize(context));
            for (int i = 0; i + 1 < n; i += 2)
                if (semantics.equals(context, bucket->getAt(context, i), key)) return i;
            return -1;
        }
    }

    const ProtoSparseListObject* hashedPut(ProtoContext* context, const ProtoSparseListObject* map,
                                           const KeySemantics& semantics, const ProtoObject* key, const ProtoObject* value) {
        if (usesIdentitySlot(context, semantics, key)) return map->setAt(context, key, value);

        const ProtoObject* slot = hashSlotKey(semantics.hash(context, key));
        const ProtoObject* existing = map->getAt(context, slot);
        const ProtoList* bucket = existing ? existing->asList(context) : nullptr;
        const int at = bucket ? findInBucket(context, bucket, semantics, key) : -1;
        if (bucket && at >= 0 && bucket->getAt(context, at + 1) == value) return map;

        ProtoContext::CriticalSection cs(context);
        const ProtoList* newBucket;
        if (!bucket) {
            const ProtoObject* pair[2] = {key, value};
            newBucket = context->newList(2, pair);
        } else if (at >= 0) {
            newBucket = bucket->setAt(context, at + 1, value);       // keep the stored key (D2c)
        } else {
            newBucket = bucket->appendLast(context, key)->appendLast(context, value);
        }
        return map->setAt(context, slot, newBucket->asObject(context));
    }

    const ProtoObject* hashedGet(ProtoContext* context, const ProtoSparseListObject* map,
                                 const KeySemantics& semantics, const ProtoObject* key) {
        if (usesIdentitySlot(context, semantics, key)) return map->getAt(context, key);
        const ProtoObject* existing = map->getAt(context, hashSlotKey(semantics.hash(context, key)));
        if (!existing) return nullptr;
        const ProtoList* bucket = existing->asList(context);
        const int at = findInBucket(context, bucket, semantics, key);
        return at >= 0 ? bucket->getAt(context, at + 1) : nullptr;
    }

    const ProtoSparseListObject* hashedRemove(ProtoContext* context, const ProtoSparseListObject* map,
                                              const KeySemantics& semantics, const ProtoObject* key) {
        if (usesIdentitySlot(context, semantics, key)) return map->removeAt(context, key);
        const ProtoObject* slot = hashSlotKey(semantics.hash(context, key));
        const ProtoObject* existing = map->getAt(context, slot);
        if (!existing) return map;
        const ProtoList* bucket = existing->asList(context);
        const int at = findInBucket(context, bucket, semantics, key);
        if (at < 0) return map;

        ProtoContext::CriticalSection cs(context);
        if (bucket->getSize(context) == 2) return map->removeAt(context, slot);
        const ProtoList* newBucket = bucket->removeAt(context, at)->removeAt(context, at);
        return map->setAt(context, slot, newBucket->asObject(context));
    }

    namespace {
        struct ForEachState {
            void* self;
            void (*fn)(ProtoContext*, void*, const ProtoObject*, const ProtoObject*);
        };

        void visitSlot(ProtoContext* context, void* raw, const ProtoObject* slotKey, const ProtoObject* slotValue) {
            auto* state = static_cast<ForEachState*>(raw);
            if (!isSmallIntegerWord(slotKey)) {
                state->fn(context, state->self, slotKey, slotValue);
                return;
            }
            const ProtoList* bucket = slotValue->asList(context);
            const int n = static_cast<int>(bucket->getSize(context));
            for (int i = 0; i + 1 < n; i += 2)
                state->fn(context, state->self, bucket->getAt(context, i), bucket->getAt(context, i + 1));
        }
    }

    void hashedForEach(ProtoContext* context, const ProtoSparseListObject* map, void* self,
                       void (*fn)(ProtoContext*, void*, const ProtoObject*, const ProtoObject*)) {
        ForEachState state{self, fn};
        map->processElements(context, &state, visitSlot);
    }
}
```

- [ ] **Step 5: Run**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='HashedCollection.*'
```
Expected: 6 tests PASS.

- [ ] **Step 6: Commit**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add headers/protoCore.h core/ProtoHashedCollection.cpp CMakeLists.txt test/HashedCollectionTests.cpp
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "feat: shared hashed-collection helper over ProtoSparseListObject

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: Final protoCore verification — full suite, debug and ASan builds, performance non-regression

**Files:** none (verification only).

**Interfaces:** consumes everything above.

- [ ] **Step 1: Full release suite**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_release --output-on-failure 2>&1 | tail -30 | tee /home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/protocore-tests-after.txt
```
Expected: every new test passes. The set of other failures equals `protocore-tests-before.txt` (no new failure).

- [ ] **Step 2: Debug build (the `toImpl` tag assertions are active in `!NDEBUG`)**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build-dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build-dbg --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build-dbg/test/proto_tests --gtest_filter='SparseList*:BothForms/*:HashedCollection.*:*Sparse*'
```
Expected: all PASS, and no "Type mismatch in toImpl conversion" abort.

- [ ] **Step 3: ASan build**

```bash
grep -m3 -E "CMAKE_BUILD_TYPE|CMAKE_CXX_FLAGS:" /home/gamarino/Documentos/proyectos/protoCore/build_asan/CMakeCache.txt
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_asan --target proto_tests -j4
/home/gamarino/Documentos/proyectos/protoCore/build_asan/test/proto_tests --gtest_filter='SparseList*:BothForms/*:HashedCollection.*:*Sparse*'
```
Expected: all PASS with no ASan report. If `build_asan` has no cache, skip this step and note that in the report. Do not reconfigure it with new flags.

- [ ] **Step 4: Performance non-regression of ProtoSparseList (final)**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo; B=/home/gamarino/Documentos/proyectos/protoCore/build_release
perf stat -r 3 -e cycles,instructions $B/sparse_list_benchmark > $S/sparse-after.out 2> $S/perf-sparse-after.txt
perf stat -r 3 -e cycles,instructions $B/object_access_benchmark > $S/object-after.out 2> $S/perf-object-after.txt
grep -c VERIFIED $S/sparse-after.out; grep -c "Checksum verified" $S/object-after.out
grep -E "cycles" $S/perf-sparse-before.txt $S/perf-sparse-after.txt $S/perf-object-before.txt $S/perf-object-after.txt
```
Expected: `3`, `3`, and after mean cycles ≤ before mean × 1.01 for both. Apply the same escalation rule as in Task 2 Step 8: interleave; if still more than 1 % slower, STOP and report to the maintainer.

- [ ] **Step 5: Confirm nothing outside the plan changed in ProtoSparseList's public surface**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore diff main -- headers/protoCore.h | grep -nE "^[-+].*(class ProtoSparseList\b|ProtoSparseList\*|ProtoSparseListIterator)" || echo "no ProtoSparseList API lines changed"
git -C /home/gamarino/Documentos/proyectos/protoCore diff main -- headers/proto_internal.h | grep -nE "^-" | grep -vE "^-\s*SparseListSmall$" | head -20
```
Expected: `no ProtoSparseList API lines changed`. The only removed line in `proto_internal.h` is the `SparseListSmall` enum terminator, which is re-added with a comma. For D1 = b, the diff also shows the `asSparseListIterator` change in `core/ProtoObject.cpp`, as agreed. (Replace `main` with the branch the work started from, as shown by `git -C /home/gamarino/Documentos/proyectos/protoCore merge-base --fork-point`, if it is not `main`.)

---

### Task 11: Version bump, changelog, spec status

**Files:**
- Modify: `protoCore/CMakeLists.txt:8`
- Modify: `protoCore/CHANGELOG.md` (top)
- Modify: `protoScala/docs/platform/PSLO-SPEC.md` (status line, line 3)

**Interfaces:** none.

- [ ] **Step 1: Bump the version**

`CMakeLists.txt` line 8: `VERSION 1.2.0` → `VERSION 1.3.0`. `SOVERSION` stays `1` unless D7 says `2`, in which case change line 63 to `SOVERSION 2`.

- [ ] **Step 2: Changelog**

Insert under `## [Unreleased]` in `CHANGELOG.md`, directly after the `### Added` heading:

```markdown
- **`ProtoSparseListObject`** (protoCore 1.3.0) — a persistent AVL map
  identical to `ProtoSparseList` except that its key is a `const ProtoObject*`
  the garbage collector traces: an object referenced only as a key stays
  alive. Keys are ordered and compared by their word (identity, tag included);
  embedded values (SmallInteger, booleans, chars, None) are valid keys and are
  never traced; `nullptr` is not a valid key. `getAt` returns `nullptr` for an
  absent key, so a stored `PROTO_NONE` stays distinguishable. Small inline
  form up to three entries, AVL beyond, exactly like `ProtoSparseList`, whose
  algorithms it shares through `core/SparseListAlgorithms.h`
  (`ProtoSparseList`'s behaviour, API, ABI and performance are unchanged).
  New API: `ProtoContext::newSparseListObject`,
  `ProtoObject::isSparseListObject` / `asSparseListObject`,
  `ProtoSpace::sparseListObjectPrototype`, `ProtoSparseListObjectIterator`.
  Uses one new pointer tag (27); the tag table now records the platform tag
  budget and the maintainer-approval rule. ABI change: every embedder must be
  rebuilt. Specification: protoScala/docs/platform/PSLO-SPEC.md.
```
If Task 9 ran, append one more bullet:

```markdown
- **Hashed-collection helper** — `KeySemantics`, `hashedPut`, `hashedGet`,
  `hashedRemove`, `hashedForEach` over `ProtoSparseListObject`: identity keys
  are stored directly; value-equality keys are stored under a SmallInteger
  hash word with a flat `[k0, v0, k1, v1, …]` bucket list for collisions.
  Language callbacks run outside GC critical sections.
```

- [ ] **Step 3: Spec status**

In `protoScala/docs/platform/PSLO-SPEC.md`, replace the status line (line 3):

```markdown
> **Status:** implemented in protoCore 1.3.0 (branch `feature/pslo-p1`, 2026-09-22). Maintainer decisions are recorded in §7. This is protoCore work
```

- [ ] **Step 4: Rebuild and re-run the new tests**

```bash
cmake -S /home/gamarino/Documentos/proyectos/protoCore -B /home/gamarino/Documentos/proyectos/protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build /home/gamarino/Documentos/proyectos/protoCore/build_release -j4
ls -l /home/gamarino/Documentos/proyectos/protoCore/build_release/libprotoCore.so.1.3.0
/home/gamarino/Documentos/proyectos/protoCore/build_release/test/proto_tests --gtest_filter='SparseList*:BothForms/*:HashedCollection.*'
```
Expected: `libprotoCore.so.1.3.0` exists and all tests PASS.

- [ ] **Step 5: Commit (two repositories)**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore add CMakeLists.txt CHANGELOG.md
git -C /home/gamarino/Documentos/proyectos/protoCore commit -m "release: protoCore 1.3.0 — ProtoSparseListObject

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
git -C /home/gamarino/Documentos/proyectos/protoScala add docs/platform/PSLO-SPEC.md
git -C /home/gamarino/Documentos/proyectos/protoScala commit -m "docs: PSLO spec implemented in protoCore 1.3.0

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: Full clean rebuild and test of every embedder

A stale binary against the new protoCore crashes (ABI lesson), so every embedder is rebuilt from clean. **protoJS: `-j1` builds and sequential test262 only.** Ask the user before starting (long, machine-heavy), and again before any full test262 sweep.

**Files:** none (build and verification only). The output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo/`.

**Interfaces:** consumes `protoCore/build_release/libprotoCore.so.1.3.0`. protoJS, protoST, protoClojure and protoScala link it (`PROTOCORE_LIBRARY` in their caches). protoPython compiles its own copy of `../protoCore` inside `protoPython/build_release/protoCore`.

- [ ] **Step 1: protoCore clean rebuild and full suite**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake --build $P/protoCore/build_release --clean-first -j4 2>&1 | tail -3
ctest --test-dir $P/protoCore/build_release --output-on-failure 2>&1 | tail -15 > $S/final-protoCore.txt; tail -3 $S/final-protoCore.txt
```
Expected: the build succeeds. The failures are the same as `protocore-tests-before.txt`.

- [ ] **Step 2: protoPython**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake -S $P/protoPython -B $P/protoPython/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/protoPython/build_release --clean-first -j4 2>&1 | tail -3
ctest --test-dir $P/protoPython/build_release --output-on-failure 2>&1 | tail -15 > $S/final-protoPython.txt; tail -3 $S/final-protoPython.txt
```
Expected: the build succeeds. The pass count equals `embedders-before-protoPython.txt`.

- [ ] **Step 3: protoJS (sequential: `-j1` build, `-j1` ctest)**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake -S $P/protoJS -B $P/protoJS/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/protoJS/build_release --clean-first -j1 2>&1 | tail -3
ctest --test-dir $P/protoJS/build_release -j1 --output-on-failure 2>&1 | tail -15 > $S/final-protoJS.txt; tail -3 $S/final-protoJS.txt
```
Expected: the build succeeds. The pass count equals `embedders-before-protoJS.txt`.

test262 smoke run: sequential, one pattern, **ask the user first**. The runner uses `TEST262_CONCURRENCY` (default 1); set it explicitly:

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && TEST262_CONCURRENCY=1 TEST262_PATTERNS=built-ins/Map TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: the pass rate matches the most recent `built-ins/Map` snapshot under `protoJS/tests/test262/reports/`. A full sweep is run only if the user asks for it.

- [ ] **Step 4: protoST**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake -S $P/protoST -B $P/protoST/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/protoST/build_release --clean-first -j4 2>&1 | tail -3
ctest --test-dir $P/protoST/build_release --output-on-failure 2>&1 | tail -15 > $S/final-protoST.txt; tail -3 $S/final-protoST.txt
```
Expected: the build succeeds. The result equals `embedders-before-protoST.txt`.

- [ ] **Step 5: protoClojure**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake -S $P/protoClojure -B $P/protoClojure/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $P/protoClojure/build_release --clean-first -j4 2>&1 | tail -3
ctest --test-dir $P/protoClojure/build_release -j1 --output-on-failure 2>&1 | tail -15 > $S/final-protoClojure.txt; tail -3 $S/final-protoClojure.txt
```
Expected: the build succeeds. The result equals `embedders-before-protoClojure.txt`.

- [ ] **Step 6: protoScala**

```bash
P=/home/gamarino/Documentos/proyectos; S=$P/.agent_scratch/p1-pslo
cmake -S $P/protoScala -B $P/protoScala/build_release
cmake --build $P/protoScala/build_release --clean-first -j4 2>&1 | tail -3
ctest --test-dir $P/protoScala/build_release --output-on-failure 2>&1 | tail -15 > $S/final-protoScala.txt; tail -3 $S/final-protoScala.txt
```
Expected: the build succeeds. The result equals `embedders-before-protoScala.txt` (Map/Set fixtures remain `XFAIL` until protoScala Phase 3).

- [ ] **Step 7: Compare and report**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p1-pslo
for p in protoPython protoJS protoST protoClojure protoScala; do echo "== $p"; tail -3 $S/embedders-before-$p.txt; echo "--"; tail -3 $S/final-$p.txt; done
```
Expected: no embedder has fewer passing tests than before. Any difference is investigated with superpowers:systematic-debugging. First confirm it is not a stale binary: rerun Steps 2-6 for that embedder. Report the table to the maintainer. Do not merge `feature/pslo-p1`; merging is the maintainer's call (superpowers:finishing-a-development-branch).

---

## Self-review against PSLO-SPEC (done while writing; gaps fixed inline)

| Spec item | Where |
|---|---|
| §2 new type, key a traced `const ProtoObject*`, `ProtoSparseList` unchanged | Tasks 2-5; Task 2 characterization + perf gate; Task 10 Steps 4-5 |
| §2 ordering and equality by word; non-moving GC → the key is recovered from the node | `sparse_avl::keyWord`; ordering test (Task 4); GC recovery tests (Task 7) |
| §2 trace the key only when `asCellPointer(key) != nullptr` | Task 3 `processReferences`; cell tests; mutation check (Task 7 Step 2) |
| §2 `nullptr` not a valid key | Task 4 (`NullKeyIsRejected`, D3) |
| §2.1 full API incl. `getHash`, `processValues`, `getIterator`, `newSparseListObject`, `isSparseListObject`/`asSparseListObject`; `getAt` → `nullptr` | Tasks 4-6 |
| §2.2 one algorithmic implementation, two node classes, `CriticalSection` in mutators, order-independent hash | Task 2 templates; Task 4 trampolines; `IsEqualAndHashIgnoreInsertionOrderAndForm` |
| §3 at most one tag; internal nodes by CellType; iterator no second tag; budget comment | Task 3 (tag 27, comment block); Task 6 (D1) |
| §3 rule 4 language encodings reuse encodings | Task 9 `hashSlotKey` (embedded SmallInteger word) |
| §4 helper (if adopted) | Task 9, gated on D2 |
| §5 tests: parity Small/AVL/promotion, GC safety under low heap limit, embedded keys not traced, persistence, concurrency, helper tests | Tasks 3, 4, 6, 7, 8, 9 |
| §6 minor version bump; full rebuild of every embedder | Tasks 11, 12 |
