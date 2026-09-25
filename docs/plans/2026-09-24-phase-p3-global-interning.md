# Phase P3 — Global interning and the module list as a GC root (protoCore) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **This plan is written, not executed.** Two sibling agents are building against protoCore as it is written, and one is committing in protoScala. Nothing in protoCore may be touched until the maintainer says the family is quiet. Task 1 Step 1 is the gate.

**Goal:** Implement the maintainer's ruling of 2026-09-24 — *"Hacer la internación global y la lista de módulos como raíz."* Two changes to protoCore:

1. **All string interning becomes process-global** rather than per-`ProtoSpace`, so the address of an interned name is the same in every space of the process.
2. **The module list becomes a process-global GC root**, so a loaded module anchors its own contents through its variables, for the process rather than for one space — under the ruled identity **provider + path + version**.

Then rebuild every embedder from clean and compare its suite against the stated baseline.

---

## Motivation — two silent failures this closes

### 1. Interning is half-global, and the half that fails is silent

Interning today is per space. `ProtoString::createSymbol` reaches the table through `ctx->space->symbolTable` (`protoCore/core/ProtoString.cpp:1356` and `:1380`), and every runtime in the family owns its own `ProtoSpace`:

| Runtime | Where its `ProtoSpace` lives |
|---|---|
| protoScala | `protoScala/src/repl/Session.h:84` — `proto::ProtoSpace space_;` (first member, destroyed last) |
| protoST | `protoST/src/runtime/STRuntime.cpp:185` — `STRuntime::Impl::space`, registered by `registerSTRuntime(&space, this)` |
| protoPython | `protoPython/src/library/PythonEnvironment.cpp:16521` — `static proto::ProtoSpace s_processSpace`, singleton enforced |
| protoJS | `protoJS/src/JSContext.h:234` |
| protoClojure | `protoClojure/src/main.cpp:73` and `src/repl/Repl.cpp:136` |

An attribute key **is the address of an interned symbol**. Two spaces therefore disagree about the key for the same name — **except** that protoCore embeds a short ASCII string in the pointer word itself (`INLINE_STRING_MAX_BYTES = 6`, `protoCore/headers/proto_internal.h:300`) and `createSymbol` short-circuits to an inline string for those (`core/ProtoString.cpp:1334-1348`), which is bit-identical in every space.

So the failure is **half-global with silent partial success**, which is the worst available state. protoST's own source records it verbatim (`protoST/src/modules/STModuleProvider.cpp:107-116`), measured 2026-09-24 during Track Y:

> An attribute key is the ADDRESS of an interned symbol, and protoCore interns per ProtoSpace (`ctx->space->symbolTable`, ProtoString.cpp), so a name the caller interns in its own space is a DIFFERENT pointer from the one protoST stored the binding under — unless the name is short enough for protoCore to embed it in the pointer word, in which case the two agree by accident. That accident is the trap: `value` (5 bytes) would resolve and `Counter` (7 bytes) would silently miss.

A 7-byte name misses **with no error at all**: `getAttribute` returns `PROTO_NONE`, which is also a legitimate value (memory: PROTO_NONE ambiguity). Task 1 Step 4 is the fixture that proves the bug before the fix.

### 2. The module list is not the root the ruling describes

Measured in the tree today, not inferred:

- **`SharedModuleCache` is process-global and is *not* a GC root.** `core/ModuleCache.cpp:19-39` is a function-local-static `std::map<std::string, const ProtoObject*>` that outlives every space and holds raw module pointers **no collector ever reads**.
- **Retention comes from somewhere else, and it is per importing space.** `getImportModuleImpl` pushes the module into the **calling** space's `moduleRoots` (`core/ModuleResolver.cpp:23-27` on a cache hit, `:116-119` on a fresh load), and Phase 2 root collection iterates that vector under stop-the-world (`core/ProtoSpace.cpp:452-457`). So "a global list" holds, but "and therefore perennial" is delivered **once per importing space**, not globally. **Making the module list a root is closing a real gap, not formalising something that already works.**
- **A prefixed cross-runtime import reaches neither mechanism.** protoScala's `Session::loadForeign` calls `provider->tryLoad(logicalPath, &ctx)` directly (`protoScala/src/repl/Session.cpp:472-497`), so nothing is inserted into `SharedModuleCache` and nothing is added to any `moduleRoots`. Retention rests entirely on two **per-runtime** roots inside protoST: the module in a `liveRegistry` pinned in a `ProtoRootSet` created on protoST's own `ProtoSpace`, and the classes in that runtime's `globals`. **Destroying the `STRuntime` while an importer still holds its values drops the only anchor.** The tests survive only because they construct it first and therefore destroy it last, and nothing enforces that order (Track Y design note under R5).
- **The identity is wrong, and it aliases silently.** `SharedModuleCache` is keyed by **path alone**, provider prefix stripped, so `import st.counter_lib` and a local `counter_lib.scala` collapse to **one** module with the first load winning for both. This is the same failure class as the 6-versus-7-byte bug: wrong answer, no error. §D11 records the maintainer's ruling on the key.

## Rationale — the maintainer's reasoning, recorded

The ruling rests on four observations, established over the discussion of 2026-09-24. They are the premises of this plan and every design choice below is answerable to them.

1. **Interned objects are perennial — never collected — so a global table has no lifecycle problem.** There is nothing to hand back, nothing to reference-count and nothing to unregister when a space dies. A per-space table buys no lifecycle benefit it could lose by going global.
2. **Loading a module is for the process, not for a space or a runtime.** The module list is therefore global, and being global it is perennial; a module anchors its contents through its variables.
3. **The mechanism for perennial cells already exists.** An allocation with a null `ProtoContext` is already an ordinary `posix_memalign` that nothing ever frees — `ProtoContext::allocCell`, `core/ProtoContext.cpp:501-506`, the branch commented *"Absolute fall back (rare or error)"* — and it is chained to no young generation because `addCell2Context` guards the chaining with `if (this)` (`core/ProtoContext.cpp:551-560`). This change **uses that mechanism deliberately rather than inventing anything.**
4. **That comment is wrong and must be corrected.** It describes the perennial path as an error path and invites a future reader to delete it. `SymbolTable::intern` and `ProtoString::createSymbol` depend on it by contract (`core/SymbolTable.cpp:10-18`, `core/ProtoString.cpp:1362-1375`). Task 2 Step 1 rewrites it.

## The distinction this plan must get right

**"Never freed" is not the same as "is a GC root."** A perennial cell is not swept, but it is also not *scanned*: the collector never reaches it, so the references it holds do not keep their targets alive. Three consequences, and the whole shape of this plan follows from them:

- A **symbol** is self-contained bytes — a `ProtoStringImplementation` over `StringLeafNode`s, all built with a null context. Its references point only at its own perennial nodes. **Perennial allocation alone is sufficient**; it needs no root, and that is exactly why the collector skips the `SymbolTable` today (`core/ProtoSpace.cpp:483-490`, `docs/GarbageCollector.md` Phase 2 "Not scanned").
- The **module list** holds module objects whose contents are **ordinary collectable objects** allocated in a space's heap through a real context. Making the list merely unfreed would keep the *list* alive and let the collector free everything it points at. It must be a **real root the mark enters through**. Task 9 Step 1 tests this with two mutations, the second of which makes a module cell perennial-but-unrooted — the direct test of the distinction.
- **Interned tuples are the hard case** and §D5 resolves them explicitly. `ProtoTuple` is interned and its entries are never removed, but per `headers/proto_internal.h:1088-1095`, *unlike a symbol, a tuple references heap objects, so the table is a GC root.* A globally interned tuple whose elements are not kept alive is a correctness hazard, and the answer is not "make it perennial".

How the tuple interner is rooted **today** — the pattern PMQ-SPEC §3 constraint 3 names, and the one this plan copies for modules:

- `TupleInterner::intern` allocates a first-sight node as **a young cell of `context`**, protected by that context until a later cycle's table snapshot covers the entry (`core/ProtoTuple.cpp:105-116`).
- GC Phase 2, under stop-the-world, calls `TupleInterner::captureForGC()`, which records each of the 64 shards' `published` count and **dereferences no entry** — O(SHARD\_COUNT) (`core/ProtoTuple.cpp:121-125`, called from `core/ProtoSpace.cpp:464`).
- GC Phase 4, **after the world has resumed** (`stwFlag.store(false)` at `core/ProtoSpace.cpp:634`), calls `forEachCaptured` and pushes exactly those entries onto the mark worklist while mutators keep appending (`core/ProtoTuple.cpp:127-137`, called from `core/ProtoSpace.cpp:649-653`).
- Entries live in append-only chunks that never move, and an entry is fully written before `published` is stored with release ordering, so the concurrent walk reads a stable view.

**Architecture:** four pieces, of which two are new machinery.

- **`globalSymbolTable()`** — one leaked, function-local-static `SymbolTable` for the lifetime of the process. `ProtoSpace`'s constructor points its existing `symbolTable` field at it instead of allocating one, and its destructor stops deleting it. **All eleven existing `ctx->space->symbolTable` call sites are untouched.** No `ProtoSpace` layout change, no exported-ABI change.
- **`ModuleIdentity`** — the ruled key: provider GUID + logical path + version, rendered as one canonical string. It re-keys `SharedModuleCache` and closes the silent-aliasing hole.
- **`ModuleRootTable`** — a new global structure in the exact shape of `TupleInterner`: 8 shards, append-only chunks, `published` counts, `captureForGC()` under stop-the-world (O(8) counter reads, zero dereferences), `forEachCaptured(space, …)` in concurrent mark. Each entry carries the `const ProtoSpace* owner` whose heap holds the module, and the walk visits only its own space's entries.
- **`TupleInterner` is not changed.** It stays per-space, and Task 10 adds the test that keeps it that way.

**Tech Stack:** C++20, CMake ≥ 3.16, GoogleTest 1.14 (`file(GLOB)` test registration in `protoCore/test/CMakeLists.txt`), Catch2 for protoST's unit tests, AddressSanitizer, ThreadSanitizer, `perf stat -r 3`.

**Spec:** there is no P3 platform spec yet. This plan is the spec of record until Task 14 writes `protoScala/docs/platform/GLOBAL-INTERNING-SPEC.md`, in the shape of `PROTOMAP-SPEC.md` and `PMQ-SPEC.md` (problem, semantics, GC constraints, tag budget — none consumed here — tests, rollout, decisions). Related: `protoCore/docs/GarbageCollector.md`, `protoCore/docs/MODULE_DISCOVERY.md`, `protoScala/docs/DECISIONS-LOG.md`, `protoScala/docs/INTEROP.md`.

---

## Global Constraints

- **Workspace safety.** Create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. Scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning/`. `perf stat` only, never `perf record`. No `rm -rf` outside that scratch directory and the build directories. No `cmake --install`.
- **Do not start while the family is building.** Task 1 Step 1 stops unless every repository is clean and the maintainer has confirmed the family is quiet. A `ProtoSpace`-semantics change landing under a sibling's build produces the ABI-mismatch class of crash (memory: ABI mismatch crashes stale binaries) with no useful diagnostic.
- **The binding GC constraint: no new stop-the-world work proportional to a collection's size.** The collector stops user threads only to take stack roots, the mutables-tree root and global-structure roots; everything else is traced concurrently (`docs/GarbageCollector.md` Phases 2-4). This is the rule that governed `ProtoMap` (P1 D5) and `ProtoMPSCQueue` (P2 D1) and it is **binding here too**. A global root set scanned at every pause in every space must cost **O(1) per pause**, not O(modules) and not O(symbols). §D6 shows O(1) is achievable and that this phase makes the pause *cheaper*; §D12 names the one thing that stands in the way and must be cleared first.
- **Every GC claim carries a test that fails if the premise is false.** This project does not accept a GC design that rests on argument. The `ProtoMPSCQueue` retain chain and the `newList` pause fix were both validated that way, and protoST's S15 fix exposed a use-after-free that 848 passing tests could not reach. Every claim below names the **mutation** that must turn a **named test** red; a claim without one is not implemented.
- **Forcing a GC cycle is NOT the same as submitting the young generation, and a reclamation assertion must be consistent with the garbage created.** Track Y found on 2026-09-24 that a helper which forces cycles without calling `ProtoContext::safepoint()` leaves the young chains unsubmitted: a cycle reclaims 4-7 cells instead of the 205,120 the helper created, so any `reclaimed > 0` assertion passes near-vacuously. That is the **third** instance of one pattern in two days (protoST's S15, the `newList` critical section, this helper). Therefore, in this plan, **every GC test goes through the `forceCycles` helper of Task 3 Step 1, which calls `safepoint()` and returns `{cycles, reclaimed, created}`, and asserts through `ASSERT_CYCLES_DID_REAL_WORK`, never against `reclaimed > 0`.** A P3 suite that inherited this flaw would validate nothing.
- **Every benchmark and measurement self-reports.** Any measuring step prints the work it performed and the runner verifies it; a silent failure looks like a success (memory: benchmarks must self-report, silent bench failures fooled the harness). `ASSERT_CYCLES_DID_REAL_WORK` prints its three numbers for exactly this reason.
- **Every embedder is rebuilt from clean** and its suite compared to this baseline:

  | Project | Baseline | Build discipline |
  |---|---|---|
  | protoCore | **438/438** ctest | `-j4` maximum |
  | protoPython | **582/583** — the lone `protopy_import_site` failure is **pre-existing** (a `.pth` in a sibling `venv/`) and is not attributed to this work | `build_release/`, never `build-release/`, which is corrupted |
  | protoST | **854/854** | `-j4` maximum |
  | protoClojure | **391/391** | `-j4` maximum |
  | protoScala | **currently 1204/1204, moving — re-check the head of `main` in Task 1 and record the number actually observed** | `-j4` maximum |
  | protoJS | **ctest 34/34**, plus test262 `built-ins/{Object,Reflect,Proxy}` at **3619 passed** with no newly failing test | **no `-j` at all**; test262 **sequential** (`TEST262_CONCURRENCY=1`); **ask the maintainer before any full sweep** |

  "From clean" means a fresh build directory, not an incremental rebuild. The `ProtoSpace` layout is unchanged by this plan, so a stale binary will *link* — and be silently wrong about symbol identity. The clean rebuild is what protects against that.
- **Three existing tests assert the OPPOSITE of what this phase delivers and must be inverted, not deleted.** They are the coverage that found the bug; retiring them would retire the coverage. Task 12 Step 2 names all three (§D13).
- **Do not change an existing object model to get the new semantic.** protoCore is extended by adding a type or a structure with the new semantic (memory: extend protoCore by new type). `ModuleIdentity` and `ModuleRootTable` are new; `SymbolTable` and `TupleInterner` keep their semantics.
- **Purity over performance.** Reject any variant that fragments protoCore's conceptual model for a measured win (memory: purity > performance). The model this plan must keep intact: *a name is a name in this process; a module is provider + path + version in this process; an object belongs to the space whose heap allocated it.*
- **Git:** work on branch `feature/global-interning-p3` in `protoCore` and, for Task 7 and Task 12 Step 2, `feature/p3-root-migration` in the affected runtime. Never push. Stage explicitly by path (`git add <file> <file>`), never `git add -A` or `git add .`. Commit with the configured identity (name **"Gustavo Marino"**; never "Gustavo Adrian Marino"; never override `user.email`). End every commit message with:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```
- **Version:** protoCore `2.1.0` → `2.2.0`; `PROTOCORE_ABI_SOVERSION` stays `2` (§D7).
- All code comments, documentation and commit messages in **professional English**, regardless of the language of the request.

---

## Task 0: Maintainer decisions

Thirteen items. **D11 is already ruled by the maintainer and is recorded as ruled, not asked.** The other twelve are recommendations, not agent decisions — unlike P1 and P2 there is no overnight authorisation for this phase, because the change is process-global and every runtime in the family is downstream of it. The executor asks before Task 2.

### D1 — Does a process-global root set replace or supplement per-space roots? — **Recommendation: supplement; the module table is the only global root, and it replaces `ProtoSpace::moduleRoots` as the *module* mechanism**

**(a) A single process-global root set that replaces per-space roots.**
- Kills the per-space prototype roots too, which is wrong: `objectPrototype` and its thirty siblings are per-space cells (`headers/protoCore.h:1929-1962`) and a space must root its own, not another's.
- Makes every collector traverse every other space's live graph on every cycle. Space A's concurrent mark would read cells while space B's sweep frees them from B's segments. **Rejected.**

**(b) Supplement: the global module table is the only new global root; everything else stays per-space. — RECOMMENDED.**
- Each entry carries `const ProtoSpace* owner`, and `forEachCaptured(space, …)` visits only entries owned by the collecting space. A module loaded into space B is rooted by B's collector, not by A's.
- The global-ness the ruling asks for is in the **table**: the module is loaded once for the process, found once under its `ModuleIdentity`, and is a root from then on. The *tracing* stays where the cells are.
- `ProtoSpace::moduleRoots` and `moduleRootsMutex` are retained as fields for layout stability and held empty **only after §D12 is settled** — see D12, which is the gating item.

**(c) Supplement, with no owner filtering: every space roots every module.**
- Preserves today's behaviour exactly, including the cache-hit branch that already roots another space's module in this space (`core/ModuleResolver.cpp:23-27`). Simpler by one field.
- But it makes every collector traverse every module graph in the process on every cycle, and the cross-space traversal hazard becomes the default. **Not recommended.**

### D2 — How the symbol table goes global — **Recommendation: one leaked function-local static, reached through `globalSymbolTable()`, with `ProtoSpace::symbolTable` pointing at it**

**(a) `ProtoSpace::symbolTable` keeps its type and points at the singleton. — RECOMMENDED.**
- `globalSymbolTable()` returns a reference to a `SymbolTable` created by `static SymbolTable* t = new SymbolTable(); return *t;` — function-local static initialisation is thread-safe since C++11, and the deliberate leak avoids the static-destruction-order fiasco: symbol cells are perennial and other statics (`SharedModuleCache`, `ProviderRegistry`) may still be alive at exit.
- **All eleven existing call sites are untouched**: `core/ProtoString.cpp:1356`, `:1380`, and `core/ProtoObject.cpp:785`, `:796`, `:1010-1011`, `:1141-1142`, `:1243-1244`, `:2147-2151`, `:2438-2442`. The diff is two lines in `ProtoSpace`'s constructor and destructor.
- It also preserves an **ordering property the census flagged**: those six `ProtoObject.cpp` guards on `context->space->symbolTable` being non-null double as "the space is mid-construction or mid-teardown" sentinels, and the rationale comment at `core/ProtoObject.cpp:995-1002` records that a weak intern there once produced `Array.isArray === undefined`. Keeping the field means the sentinel still fires exactly when it used to: the field is null before the constructor reaches it and non-null after, per space, regardless of what other spaces have done.
- No `ProtoSpace` layout change, no exported-ABI change, no embedder source change.
- **Fixes a leak rather than creating one.** `~SymbolTable` frees only the `Bucket` nodes (`core/SymbolTable.cpp:27-37`), leaking every symbol cell — so each destroyed space already leaked its whole symbol set. One table leaks one set.

**(b) Delete the field; call `globalSymbolTable()` at every site.** More honest, at the cost of an eleven-site diff, a layout change, and the loss of the mid-construction sentinel above. The field can be removed in a later major.

**(c) Keep per-space tables and add a global one they fall back to.** Two tables means two possible canonical pointers for one name, which is the bug this phase closes, restated. **Rejected.**

### D3 — Thread-safety discipline for the now cross-space-contended tables — **Recommendation: unchanged locking; the discipline is that both tables' locks stay strict leaf locks**

- `SymbolTable` is already 64 shards, each an independent bucket chain behind its own `std::mutex`, and **no Cell is allocated while a shard lock is held**: `normalizeForSymbol` and `implAsSymbol` run before the lock, and the double-checked re-check inside the critical section handles the race (`core/SymbolTable.cpp:126-183`). Going global raises contention on the same 64 shards; it changes no ordering.
- The load-bearing invariant to state in the source and never break: **a shard mutex is a leaf lock and is never ordered against `ProtoSpace::globalMutex`.** With one table serving several spaces, breaking that would deadlock *two* collectors instead of one.
- The read path is allocation-free by construction, which is what makes a foreign space's context safe to pass in: `lookupUTF8` hashes raw bytes and allocates nothing (`core/SymbolTable.cpp:100-120`), and `contentEqual` reads through `ProtoString::toUTF8String`, which is a `RopeCharacterIterator` walk into a `std::string` with no cell allocation (`core/ProtoString.cpp:1209-1220`). **A test pins this** (Task 3 Step 3), because the day a string read allocates, a global table read from space A's context would allocate into space A's heap on behalf of space B.
- **The census makes this less theoretical than it looks:** several call sites already pass a stack-local *child* context (`&frame`, `&scope`, `&wrapScope` in protoClojure; `&stCtx`, `&foreignCtx` in protoST). That is fine today because only `->space` matters, and it stays fine — but it means the effective interning scope was already "whatever space that frame belongs to", never the frame.
- `ModuleRootTable` copies the `TupleInterner` discipline: per-shard mutex for append, `published` stored with release after the entry is fully written, chunk links atomic with acquire loads on the walk. Nothing allocates under its lock.
- **`ProtoSpace::globalMutex` is already `static`** (`headers/protoCore.h:2360`), so two co-resident spaces already serialise their stop-the-world windows on one process-wide recursive mutex. Pre-existing; see "Recorded, not asked".

### D4 — What `ProtoSpace` teardown must no longer free — **Recommendation: the symbol table, and only the symbol table**

- `~ProtoSpace` currently does `delete symbolTable; symbolTable = nullptr;` (`core/ProtoSpace.cpp:1348-1349`). With a shared table, the **first** space to die would free the table the others are using — a use-after-free a single-space test suite can never reach, which is the S15 class of bug.
- **What must change:** remove the `delete`, and the assignment with it; the field is a borrowed pointer now and the object it points at outlives every space.
- **What must NOT change:** `delete tupleInterner` stays (§D5). `freeStringInternMap(this)` stays. `delete rootContext`, `delete gcContext`, the root-set sweep and the `DirtySegment` drain all stay.
- **Test / mutation:** `GlobalInterning.SymbolSurvivesTheSpaceThatInternedIt` under ASan; restoring `delete symbolTable` turns it red with a use-after-free.

### D5 — Does tuple interning go global, and how do its elements stay alive? — **Recommendation: NO. Tuple interning stays per-space. Intern globally what is keyed by *content*; keep per-space what is keyed by *address*.**

This is the hard case the ruling does not settle, and the recommendation rests on a property of the key, not on an estimate of cost.

- `TupleInterner`'s key is **the slot pointers** — `hashSlots` mixes the `TUPLE_SIZE` element addresses, and `find` confirms with `memcmp` over the slot array (`core/ProtoTuple.cpp:30-56`). A symbol's key is its **bytes**.
- Content is space-independent, so a global symbol table produces cross-space identity. **Addresses are not**: two spaces building "the same" tuple of their own objects hold different element pointers and land in different buckets. A global tuple table would therefore produce **no cross-space identity at all** for the ordinary case, while importing the hazards below.
- **The one case where it *would* alias is the hazard, and it is new since P3.** Symbols are now the same pointers in every space, so a tuple of symbols built in space A and in space B would have identical slots and a global table would return **one** node — whose cells live in whichever space created it first. The other space's mark would traverse into a foreign heap, and its elements would be kept alive by a foreign collector. New cross-space coupling, created by this phase, for zero benefit.
- **It is not a use-after-free today only by accident.** `~ProtoSpace` never frees the `posix_memalign` blocks that back its cells (`core/ProtoSpace.cpp:1325-1369` frees contexts, root sets and `DirtySegment` structs, not the cell heap; the only cell-block allocation is `core/ProtoSpace.cpp:1767` and nothing frees it). A global tuple table holding entries into a dead space would read leaked-but-mapped memory rather than crashing. **A design that is safe because of a leak is not a design.**
- **If the maintainer wants it global anyway**, the minimum that keeps elements alive is: (i) each `Entry` carries `const ProtoSpace* owner`, set to the space of the creating context; (ii) `forEachCaptured(space, …)` visits only its own entries, so the owning collector traces the elements; (iii) a `purgeSpace(const ProtoSpace*)` called from `~ProtoSpace` removes every entry of the dying space — O(entries) at teardown, never at a pause — because otherwise an entry survives its heap; and (iv) the aliasing case above is accepted and documented. Three new mechanisms to obtain an identity the key cannot express. **Not recommended.**
- **Test / mutation:** `GlobalInterning.TupleInternerStaysPerSpace` asserts that the tuple of two now-globally-interned symbols has a **different** `ProtoTuple` address in two spaces, *and* that per-space interning still holds inside one space. Switching `TupleInterner` to a singleton turns the first assertion red. This test exists to stop a future reader from "completing" the change.

### D6 — How pause cost stays O(1) — **Recommendation: symbols cost nothing; modules use the `TupleInterner` capture/walk split. The pause gets strictly cheaper — once D12 is cleared.**

| Root | Pause cost today | Pause cost after |
|---|---|---|
| `SymbolTable` | **0** — perennial, never scanned (`core/ProtoSpace.cpp:483-490`) | **0**, unchanged. Global-ness changes the table's *residency*, not whether the collector reads it. |
| Module list | **O(modules)** — `for (const ProtoObject* mod : space->moduleRoots) addRootObj(mod);` under stop-the-world, holding `moduleRootsMutex` (`core/ProtoSpace.cpp:452-457`) | **O(8)** — `globalModuleRootTable().captureForGC()`, eight relaxed counter reads, zero dereferences. Entries are pushed onto the worklist in **Phase 4**, after `stwFlag.store(false)` (`core/ProtoSpace.cpp:634`), exactly where the tuple interner's are. |
| Tuple interner | O(64) counter reads | unchanged |

- **O(1) is achievable and this phase makes the pause smaller, not larger.** The module loop is *existing* linear pause work that `docs/GarbageCollector.md`'s own per-component cost table does not even list — it names prototypes, `mutableRoot[256]`, embedder root sets, the tuple interner, `SymbolTable` and `stringInternMap`, and omits `moduleRoots` entirely (`docs/GarbageCollector.md:599-613`). Task 13 fixes the table.
- **But it is only achievable once §D12 is cleared.** protoPython uses `space->moduleRoots` as a general-purpose, *removable*, slot-replacing embedder root set, so the loop cannot simply be deleted. D12 is the gate, and Task 8 (the collector change) is deliberately sequenced after Task 7 (the migration).
- **Why the concurrent walk is sound for modules**, by the same four properties that make it sound for tuples: entries live in append-only chunks that never move; an entry's `module` and `owner` words are written *before* `published` is stored with release ordering, so a walker that sees the count sees the entry; a module appended after the capture is not visited this cycle but is a young cell of the loading context and protected by it until the next cycle's capture (`core/ProtoTuple.cpp:105-116`, PMQ-SPEC §3 constraint 3); and the walk runs while mutators append, because appending only ever extends past the captured count.
- **Test / mutation — two tests, because one alone is trivially green:**
  - `ModuleRootGC.CaptureUnderStopTheWorldIsConstant` loads 0 and then 2000 module entries and asserts `ModuleRootTable::lastCaptureShardReads() == ModuleRootTable::SHARD_COUNT` in both cases. **Mutation:** restore the per-module loop in Phase 2 → `lastCaptureShardReads()` stays 0 while roots are added → red.
  - `ModuleRootGC.TheWalkNeverRunsInsideThePause` asserts `ModuleRootTable::stwVisitViolations() == 0` after forcing cycles with 2000 entries; `forEachCaptured` increments that counter when it observes `space->stwFlag` set. **Mutation:** move the `forEachCaptured` call from Phase 4 up into Phase 2 → positive → red.
- **Deliberately not a timing test.** A wall-clock pause assertion on DEV12 is noise (memory: micro-opts can hurt icache layout — use `perf stat -r 3` cycles before claiming anything). Task 11 Step 4 *measures* the pause with `PROTOCORE_GC_PROFILE=1` and records it; the two tests above are what *gates* the property.

### D7 — Version and SOVERSION — **Recommendation: `2.1.0` → `2.2.0`, `PROTOCORE_ABI_SOVERSION` stays `2`**

- **The change is additive at the ABI level, not breaking.** The `ProtoSpace` layout is unchanged: no field added, removed or reordered (`symbolTable` keeps its type and slot; `moduleRoots` and `moduleRootsMutex` are retained). `SymbolTable` and the new `ModuleRootTable` live in `headers/proto_internal.h`. The public additions are one new class (`ModuleIdentity`) and four non-virtual `ProtoSpace` methods, which add no vtable and no layout. Task 8 Step 6 proves the layout with an `offsetof` diff.
- A minor bump matches the `SameMajorVersion` `protoCoreConfigVersion.cmake` Phase I generated, and `find_package(protoCore 2.2 REQUIRED CONFIG)` keeps working for a 2.1 consumer.
- **The clean rebuild is mandatory anyway, and this is the reason to insist on it loudly:** unlike P1 and P2, a stale embedder binary here **links and runs**, and is simply wrong about symbol identity in a multi-space process. There is no load-time error to catch it. Argue for `SOVERSION 3` if the maintainer values that over packaging churn; 2.1.0 is one day old.
- **Open question this inherits:** whether protoCore's existing `[Unreleased]` CHANGELOG section (Phase I packaging) folds into 2.1.0 or becomes 2.1.1 is still open (`protoScala/docs/DECISIONS-LOG.md`, "Still open"). Task 14 must not decide it; it appends `[2.2.0]` above whatever `[Unreleased]` then contains.

### D8 — Symbols interned before the change, in a process that creates several spaces — **Recommendation: the second space finds them; `literalData` and its siblings become shared, and that is the fix working**

- There is no "before the change" state to migrate: the global table is populated lazily on first `intern`, and the first space to construct populates it.
- `ProtoSpace`'s constructor interns `"__data__"` (8 bytes), `"setAttribute"` (12) and `"callMethod"` (10) (`core/ProtoSpace.cpp:1253-1255`) — all beyond `INLINE_STRING_MAX_BYTES`, so **today they are different addresses in every space** and after this change they are one. That is precisely the intended semantic: `space->literalData` is an attribute key. **Test:** `GlobalInterning.CachedLiteralsAreSharedAcrossSpaces`.
- **Second-space cost:** a space constructed after another interned N names re-interns nothing. **Test:** `GlobalInterning.ASecondSpaceInternsNothingItsPredecessorAlreadyDid`.
- **Ordering caveat to document:** the global table's lifetime begins at the *first* `ProtoSpace` construction in the process and never ends. A process that constructs and destroys spaces in a loop accumulates symbols monotonically — which is what "perennial" always meant, only now the accumulation is not reset by the last space dying. Task 3 Step 5 asserts the growth is bounded by distinct spellings, not by space count.
- **A consequence the census surfaced, and the reason Task 12 Step 3 exists.** `protoCore/test/SymbolInternTests.cpp:41-66` asserts `allocatedCellsCount` and `heapSize` are unchanged across 100k `createSymbol` calls, and `AFreshNameIsInternedOnceAndStaysCanonical` at `:94` presumes `"freshly_interned_name"` is genuinely fresh *in its space*. With one table, freshness depends on test execution order and on `--gtest_repeat`. Those cases must be made order-independent (use a per-test unique spelling) rather than left to pass by luck.

### D9 — Is protoST's `buildCallerFacade` simplified in this phase? — **Recommendation: no. Only its comment is corrected, and only after the maintainer agrees.**

- Global interning makes the *key rebuild* in `buildCallerFacade` redundant: `createSymbol(callerCtx, b.name.c_str())` will return the pointer protoST already stored the binding under (`protoST/src/modules/STModuleProvider.cpp:117-143`).
- **But the facade is still needed**, and this is the honest finding: it also re-parents the namespace object to `callerCtx->space->objectPrototype`, and **prototypes stay per-space**. A protoST object handed to protoScala still carries parent links into protoST's prototype chain. Global interning fixes *names*, not *prototypes*. Deleting the facade would trade a silent attribute miss for a silent prototype mismatch.
- **Recommended scope:** rebuild and re-verify protoST (Task 12), and update the comment at `STModuleProvider.cpp:107-116` so it no longer describes a bug protoCore has fixed. Also `protoScala/docs/INTEROP.md:143`, `:175-176` and `docs/DESIGN.md:907`, which document the rule "Never cache interned symbols in function-local statics; symbols are per space" — that rule is **reversed** by this phase and must be rewritten, or five doc sets will contradict the kernel.
- **Alternative:** leave protoST entirely untouched and open a Track Y follow-up. Prefer this if any sibling agent is still active in protoST.

### D10 — Which module APIs are exposed publicly? — **Recommendation: four additive `ProtoSpace` methods plus the `ModuleIdentity` class**

- `void addModuleRoot(const ProtoObject* module);` — root a module this space loaded.
- `static unsigned long moduleRootCount();` — diagnostics and tests.
- `const ProtoObject* registerModule(const ModuleIdentity&, const ProtoObject*);` — publish a module the embedder loaded itself, under the ruled identity, and root it here. **This is what closes the `Session::loadForeign` gap** (§D13).
- `static const ProtoObject* findModule(const ModuleIdentity&);` — the cache probe, for an embedder that resolves before loading.
- Four non-virtual methods and one new class: additive, no layout change, no vtable change.
- **Alternative:** keep them internal to `proto_internal.h` for one release. Smaller public surface, but then protoScala's `loadForeign` cannot use them and the cross-runtime gap stays open for another release. **Not recommended.**

### D11 — Module identity — **RULED BY THE MAINTAINER, 2026-09-24: `provider + path + version`. Not path alone.**

This is recorded as **ruled**, not asked. The rationale, because it is what makes the triple necessary rather than tidy:

- Modules are **process-level and perennial** — they never unload — so two coexisting versions of the same module are two modules for the life of the process, and identity must be able to tell them apart.
- **Diamond dependencies will produce exactly that situation** once modules have dependencies.
- **Path-alone identity aliases silently.** `SharedModuleCache` is keyed by path with the provider prefix stripped (`core/ModuleResolver.cpp:17`, `core/ModuleCache.cpp:24-27`), so `import st.counter_lib` and a local `counter_lib.scala` collapse into one module with the first load winning for both. That is the same failure class as the 6-versus-7-byte interning bug this phase exists to fix: wrong answer, no error.
- **Half the answer already exists in the tree:** protoScala's `Session::loadForeign` keys on `providerSpec + "/" + logicalPath` (`protoScala/src/repl/Session.cpp:474`). The kernel's key becomes the authority so the two stop disagreeing.

**The representation, and why it is future-proof.** There is no module manifest today, so versions do not exist yet. **This plan does not invent one** — that is future work and out of scope. What P3 must do is fix the *shape of the key* now so that introducing versions later cannot silently re-alias any module that exists today.

- **Chosen representation:** three components, rendered as one canonical string with `\x1F` (ASCII unit separator) between them:
  ```
  <providerGUID> "\x1F" <logicalPath> "\x1F" <version>
  ```
  `\x1F` is the separator because it cannot occur in a provider GUID, in a POSIX or Windows path, or in any version syntax — unlike `/`, which is in every path, and `:`, which is in the `provider:` spec and in Windows drive letters.
- **The provider component is the GUID (`ModuleProvider::getGUID()`), not the alias.** An alias is a user-facing nickname, resolved by `getProviderForSpec` with alias-before-GUID precedence (`core/ProviderRegistry.cpp:56-66`), and can be re-pointed at a different provider; a GUID is the provider's stable identity (`core/ModuleProvider.h:25`). For a filesystem chain entry the key uses that `FileSystemProvider`'s GUID, which is derived from its base path — so `./counter_lib.scala` and `/usr/share/protoScala/counter_lib.scala` are two modules, which is correct.
- **The version of a module that declares none is the EMPTY STRING**, and that is a permanent, first-class identity value meaning *"this module declares no version"*. It is **not** a wildcard and **not** a synonym for any declared version.
- **The forward-compatibility rule:** an unversioned module's key is `G\x1FP\x1F` today and is **byte-identical** after manifests exist. Introducing versions therefore cannot re-alias any module that exists now. A module that later declares `1.0.0` gets `G\x1FP\x1F1.0.0`, a *different* module from `G\x1FP\x1F` — which is correct under the perennial model: two coexisting versions are two modules for the life of the process, and an unversioned request and a versioned request are two different requests.
- **Why not `"0.0.0"`, `"unversioned"` or `"latest"`:** each is a string a future manifest could legitimately declare, so reserving one would alias a declared value onto the unversioned module — the same wrong-answer-no-error class the ruling is closing. The empty string is the only value a manifest that requires a non-empty version cannot produce, so it is the only safe reservation. **A future resolver must reject an empty declared version**, and that rule is recorded in the spec (Task 14 Step 3).
- **The behaviour change this forces, stated plainly.** Today `sharedModuleCacheGet(path)` runs **before** the resolution chain loop, so a cache hit short-circuits the chain and therefore ignores which provider *would* have won. Under a provider-qualified key the probe must move **inside** the loop, once per chain entry, because the provider is not known until an entry is selected. That changes resolution order in one observable way: a module already loaded from a *later* chain entry no longer shadows an *earlier* entry that could serve it. That is a fix — chain order is the user's stated precedence — but it is a behaviour change and Task 5 Step 4 tests it explicitly rather than discovering it in a runtime's suite.
- **One sentence of motivation, recorded and deliberately not planned.** The intended distribution model is a library of installable UMD modules with a `uv`/`pip`-like resolver and virtual environments, where an environment is resolved once before the process loads anything and is then immutable — which fits the perennial model — and where dependencies cross languages, each dragged runtime adding a `ProtoSpace` and therefore a term to the process sizing rule. **P3 supplies the identity those things will rely on; it does not build them.**

### D12 — `ProtoSpace::moduleRoots` cannot simply be retired: protoPython uses it as a general-purpose, removable embedder root set — **Recommendation: migrate protoPython to `ProtoRootSet` and `addModuleRoot` in this phase, then retire the loop. THIS IS THE GATING ITEM.**

This is the single biggest risk in the phase and the draft of this plan missed it. Measured in protoPython today — **eight writers and three readers**, and most of what they pin is not a module:

| Site | What it does |
|---|---|
| `src/library/PythonEnvironment.cpp:22640-22660` | bulk-adds ~20 prototypes (`objectPrototype`, `typePrototype`, `intPrototype`, …) |
| `src/library/PythonEnvironment.cpp:16558-16575` | **erases** those entries in `~PythonEnvironment` via `std::remove` |
| `src/library/PythonEnvironment.cpp:23962-23970` | **slot replacement** under `moduleRootsMutex`: finds `old`, overwrites it with `updated`, else pushes |
| `src/library/PythonEnvironment.cpp:18063` | pins `s_globalThreadRootsDict` |
| `src/library/PythonEnvironment.cpp:22287` | pins a `sent` object |
| `src/library/PythonEnvironment.cpp:28687` | pins interned symbols (with `g_internPool`/`g_internRoots` above it) |
| `src/library/Compiler.cpp:5562` | pins a `ByteBuffer` so a finalizer cannot run |
| `src/library/ExecutionEngine.cpp:1367` | pins a bytecode cache object |

**`ModuleRootTable` is append-only and perennial and is therefore NOT a substitute for this.** Entries are never removed by design, so `~PythonEnvironment`'s erase and the builtins slot-replacement have no counterpart. The options:

**(a) Migrate protoPython in this phase, then retire the loop. — RECOMMENDED.**
- The removable pins (`~PythonEnvironment`'s twenty prototypes, the builtins slot, the thread-roots dict, the `sent` object, the ByteBuffer, the bytecode cache) go to a **`ProtoRootSet`**, which exists for precisely this and supports `add`/`remove`/`resolve` (`headers/protoCore.h:1870-1920`). Its doc comment already says it exists so an embedder need not "invent its own anchor scheme".
- The interned symbols at `:28687` need **no root at all** — symbols are perennial — so that site is deleted, and with it `g_internRoots`.
- Genuine module objects, if any remain, go to `addModuleRoot`.
- Cost: ~11 sites in a sibling repository, and one `ProtoRootSet` created in `PythonEnvironment`'s constructor and destroyed in its destructor. Benefit: the O(modules) pause work goes away, protoPython stops abusing a field named "module roots" for twenty prototypes, and the erase/replace semantics become the API's rather than hand-rolled `std::remove` on a vector the collector reads.
- **Risk to name:** this is GC root removal. Losing a root does not fail a test, it corrupts memory later. Task 7 therefore requires the protoPython suite **plus** an ASan run **plus** a GC-pressure run under a hard heap limit, and it is sequenced *before* the collector change so protoPython is never in a state where the loop is gone and the roots are not migrated.

**(b) Keep iterating `moduleRoots` under stop-the-world.**
- Zero embedder change, and the phase's other half still works. But it leaves O(modules) — in protoPython's case O(prototypes + pins) — inside the pause, which the binding constraint forbids growing and which this phase was going to remove. It also leaves two module mechanisms.
- **Acceptable only as an explicit maintainer decision to defer**, with the pause-cost goal deferred with it and D6's table annotated as not yet achieved.

**(c) Move the `moduleRoots` iteration out of the pause: capture `size()` under stop-the-world, walk in Phase 4.**
- No embedder change and the pause becomes O(1). But `std::vector` is not safe to walk while another thread pushes (reallocation moves the buffer), and protoPython *erases* and *replaces* entries, so a concurrent walk would read a moved buffer or a stale slot. Making it safe means replacing the vector — i.e. doing (a)'s work inside protoCore, on a field embedders write directly. **Rejected as unsound.**

**(d) Split: P3 adds the table, `ModuleIdentity` and `addModuleRoot` and keeps the loop; P3b retires it after protoPython migrates.**
- Smallest blast radius and each phase independently verifiable. The honest fallback if the maintainer will not have protoPython touched in this phase. The pause-cost claim then belongs to P3b, and D6's table must say so.

### D13 — The cross-runtime registration gap — **Recommendation: close it with `registerModule`, and invert the three tests that assert the old behaviour**

- **The gap:** protoScala's `Session::loadForeign` calls `provider->tryLoad(logicalPath, &ctx)` directly (`protoScala/src/repl/Session.cpp:472-497`), bypassing `SharedModuleCache` and every `moduleRoots`. Retention rests on protoST's own `liveRegistry` `ProtoRootSet` and its `globals` — both destroyed with the `STRuntime`. A host that destroys the `STRuntime` while an importer still holds its values drops the only anchor. The tests survive only because construction order happens to make the runtime destroyed last, and nothing enforces that.
- **Recommended:** `ProtoSpace::registerModule(id, module)` publishes into the same cache and root table the kernel path uses, and Task 6 Step 4 changes `Session::loadForeign` to call it. The module is then rooted by the *importing* space, which is the space whose lifetime the importer controls — so destroying the `STRuntime` no longer drops it.
- **Three existing tests assert the OPPOSITE of what this phase delivers.** They must be inverted, not deleted; they are the coverage that found the bug.
  1. `protoST/tests/unit/test_cross_runtime_provider.cpp:97` — `REQUIRE(counterKey != counterKeyInST);` with the comment *"The same name interned in protoST's space is a DIFFERENT pointer — that is the whole reason the namespace has to be rebuilt."* This becomes `REQUIRE(counterKey == counterKeyInST);` and the comment is rewritten to say that P3 made it one pointer and that the facade is still needed for **prototypes** (§D9).
  2. `protoScala/tests/unit/protost_interop.cpp:145-163` — the negative assertion at `:163` still holds, but the reasoning comment at `:150-152` becomes false and must be rewritten.
  3. `protoScala/tests/conformance/25-interop/stand-in-provider-long-keyword.scala` — a conformance test whose entire purpose is the >6-byte-name-crosses-space trap. It would **start passing for the wrong reason**, silently retiring its coverage. It must be re-pointed at what still fails after P3 (a prototype-chain mismatch across spaces) or explicitly retired with a note, never left to pass by accident.
- **Alternative:** leave `loadForeign` alone and record the STRuntime-lifetime hazard as a known issue. Smaller diff, but it leaves a live use-after-free shape in the family's flagship interop path.

### Recorded, not asked (facts the maintainer should see)

- **This phase removes linear pause work that already existed, and the documented pause profile has been understating the pause.** The module-root loop at `core/ProtoSpace.cpp:452-457` runs inside the stop-the-world window, holds `moduleRootsMutex` there, and is O(modules). `docs/GarbageCollector.md`'s per-component cost table (`:599-613`) does not list it, so the claim "every term is either constant or scales with thread/stack quantities the application controls" (`:625-628`) is **not currently true**. After this phase, with D12 cleared, it is.
- **The cache-hit branch already roots another space's module in this space** (`core/ModuleResolver.cpp:23-27`), whatever space's heap holds it. Cross-space marking is pre-existing, not introduced here, and D1(b) preserves it.
- **Global interning does not make objects portable across spaces.** It fixes attribute *keys*. Prototypes, `PROTO_NONE`, the mutables tree and every per-space callback remain per-space. §D9 is the concrete consequence, and it is why the facade survives.
- **`ProtoSpace::globalMutex` is `static`**, so two co-resident spaces already serialise their stop-the-world windows and their `getFreeCells` OS fallbacks on one process-wide recursive mutex. A family that now genuinely expects several spaces per process should know.
- **`ProtoSpace` teardown does not free its cell heap.** Nothing frees the blocks from `core/ProtoSpace.cpp:1767`. That leak is what keeps several cross-space hazards from being crashes today, including the one §D5 declines. A future teardown that *does* free the heap will need §D5's `purgeSpace` for any global structure holding a cell of a dying space.
- **protoPython already has a process-global intern layer in front of protoCore's per-space one.** `src/library/PythonEnvironment.cpp:93` holds `static std::map<std::string, const proto::ProtoString*> g_internPool;` plus `g_internRoots` (`:95`) and a `thread_local` cache, never cleared, with an explicit guard at `:28647-28656`: when `s_threadEnv` is null the space is "ephemeral (unit-test scope)" so the pointer must *not* be cached, "corrupting subsequent tests". **This phase makes that whole two-tier structure and its guard unnecessary.** Collapsing it is not in scope, but the guard should be revisited once P3 lands, and the `g_internRoots`/`moduleRoots.push_back(sObj)` half is deleted by Task 7 because symbols need no root.
- **Static and `thread_local` caches of raw symbol pointers become correct.** `protoJS/src/JSSymbols.cpp:25` (`static const ProtoString* const s_sym`, whose comment at `:20-22` already *assumes* global semantics and is only valid because protoJS has one space), `protoJS/src/DatePrototype.cpp:243`, `EventLoopBindings.cpp:23`, `ProtoCoreNativeBindings.cpp:167`, `ArrayElementsStorage.h:145` (an `unordered_map` keyed on raw symbol pointers), `protoST/src/primitives/exception_prims.cpp:83,89,95,101` (four `call_once` caches capturing the first `ctx`), `protoST/tests/unit/test_execution_engine.cpp:959-966`. Every one was a latent bug whenever a process created more than one space; after this phase they are sound. **That is evidence for the phase, not against it** — but they are where a regression would surface first, which is why protoJS is the runtime whose result matters most.
- **`protoJS/src/ProxyBuiltin.cpp:17,24`** uses function-local `static` caches built from `ctx->fromUTF8String` + `asString` rather than `createSymbol`. That is a separate correctness smell (an uninterned key in a cached slot) and is not fixed by this phase; note it for a protoJS follow-up.
- **A stale backup file.** `protoScala/src/runtime/ActorPrimitives.cpp.bak` holds six `createSymbol` calls and is excluded from the census only by its suffix. It should be deleted, in protoScala, by whoever owns that repository.

---

## File Structure

protoCore (`/home/gamarino/Documentos/proyectos/protoCore`):

| File | Change | Responsibility |
|---|---|---|
| `headers/proto_internal.h` | modify | `globalSymbolTable()`, `globalSymbolCount()`; `ModuleRootTable` + `globalModuleRootTable()`; the leaf-lock invariant; the D5 note on `TupleInterner` |
| `headers/protoCore.h` | modify | `ModuleIdentity`; `ProtoSpace::addModuleRoot`, `moduleRootCount`, `registerModule`, `findModule`; the `moduleRoots` retirement comment |
| `core/SymbolTable.cpp` | modify | `globalSymbolTable()`, `entryCount()`; file-header rewrite |
| `core/ModuleRoots.cpp` | **create** | `ModuleRootTable` and its pause-cost diagnostics |
| `core/ModuleIdentity.cpp` | **create** | `ModuleIdentity` and its canonical key |
| `core/ModuleCache.cpp` / `.h` | modify | keyed by `ModuleIdentity::asKey()`, not the bare path |
| `core/ModuleResolver.cpp` | modify | the probe moves inside the chain loop; both root writers become `addModuleRoot` |
| `core/ProtoSpace.cpp` | modify | ctor borrows the global table; dtor stops freeing it; Phase 2 capture; Phase 4 walk; the four new methods |
| `core/ProtoContext.cpp` | modify | correct the "Absolute fall back (rare or error)" comment |
| `CMakeLists.txt` | modify | the two new sources; `VERSION 2.2.0` |
| `test/GlobalInterningTests.cpp` | **create** | cross-space identity, the 5-vs-7-byte fixture, teardown, literals, allocation-free reads, the tuple guard, concurrency |
| `test/ModuleRootGCTests.cpp` | **create** | module roots are real roots; the two pause-cost tests; owner filtering |
| `test/ModuleIdentityTests.cpp` | **create** | the triple, the empty-version rule, the chain-order behaviour change |
| `test/SymbolInternTests.cpp` | modify | order-independent spellings (D8); residency bounded by distinct spellings |
| `docs/GarbageCollector.md` | modify | Phase 2/4 root lists, the per-component cost table, "Not scanned" |
| `docs/MODULE_DISCOVERY.md` | modify | the module list is a process-global root; identity is provider + path + version |
| `CHANGELOG.md` | modify | `[2.2.0]` |

protoPython (`/home/gamarino/Documentos/proyectos/protoPython`), Task 7 only:

| File | Change | Responsibility |
|---|---|---|
| `src/library/PythonEnvironment.cpp` | modify | six `moduleRoots` sites → one `ProtoRootSet`; the symbol-rooting site deleted |
| `src/library/PythonEnvironment.h` | modify | the `ProtoRootSet*` member and its handles |
| `src/library/Compiler.cpp` | modify | the ByteBuffer pin → `ProtoRootSet` |
| `src/library/ExecutionEngine.cpp` | modify | the bytecode-cache pin → `ProtoRootSet` |

protoScala (`/home/gamarino/Documentos/proyectos/protoScala`):

| File | Change | Responsibility |
|---|---|---|
| `src/repl/Session.cpp` | modify | `loadForeign` publishes through `registerModule` (D13) |
| `tests/unit/protost_interop.cpp` | modify | the reasoning comment at `:150-152` |
| `tests/conformance/25-interop/stand-in-provider-long-keyword.scala` | modify | re-pointed so it does not pass for the wrong reason |
| `docs/INTEROP.md`, `docs/DESIGN.md` | modify | the "symbols are per space" rule is reversed |
| `docs/platform/GLOBAL-INTERNING-SPEC.md` | **create** | the P3 platform spec, with `## 7. Decisions (P3)` |
| `docs/DECISIONS-LOG.md` | modify | one row per decision, plus the ruling itself |

protoST: `src/modules/STModuleProvider.cpp` (comment only, D9) and `tests/unit/test_cross_runtime_provider.cpp:97` (assertion inverted, D13).

---

### Task 1: Baseline — the gate, the branch, and the two fixtures that prove the bug

**Files:**
- Output (not committed): `/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning/`
- Create: `protoCore/test/GlobalInterningTests.cpp` (the fixture only)

**Interfaces:** consumes nothing. Produces a clean `feature/global-interning-p3` branch; `protocore-tests-before.txt`, `protoscala-tests-before.txt`, `tsan-baseline.txt`, `pause-before.txt`; and a test file in which **one case fails and one passes**, on purpose.

- [ ] **Step 1: The gate — confirm the family is quiet**

```bash
for r in protoCore protoScala protoST protoPython protoClojure protoJS; do
  printf '=== %s ===\n' "$r"
  git -C /home/gamarino/Documentos/proyectos/$r status --short
  git -C /home/gamarino/Documentos/proyectos/$r log --oneline -1
done
```
Expected: `protoCore` clean at `983bbf98` or later. **Stop and ask the maintainer if anything else is dirty.** Two sibling agents were building against protoCore and one was committing in protoScala when this plan was written. Do not proceed on inference — the maintainer must say the family is quiet.

- [ ] **Step 2: Branch and scratch directory**

```bash
git -C /home/gamarino/Documentos/proyectos/protoCore switch -c feature/global-interning-p3
mkdir -p /home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
```
Done when `git -C .../protoCore branch --show-current` prints `feature/global-interning-p3`.

- [ ] **Step 3: Record the before-numbers**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake -S $P -B $P/build_p3 -DCMAKE_BUILD_TYPE=Release && cmake --build $P/build_p3 -j4
ctest --test-dir $P/build_p3 --output-on-failure 2>&1 | tail -20 | tee $S/protocore-tests-before.txt

Q=/home/gamarino/Documentos/proyectos/protoScala
cmake -S $Q -B $Q/build_p3 -DCMAKE_BUILD_TYPE=Release && cmake --build $Q/build_p3 -j4
ctest --test-dir $Q/build_p3 2>&1 | tail -5 | tee $S/protoscala-tests-before.txt
```
Done when `protocore-tests-before.txt` contains `438/438` (record and report any other number; a failure here is pre-existing and not attributed to this work) and `protoscala-tests-before.txt` records the observed count for Task 12.

- [ ] **Step 4: The fixture that proves the bug — one case red, one green, today**

```cpp
// GlobalInterningTests.cpp — P3: interning is process-global.
//
// Phase P3 makes ProtoString::createSymbol return one canonical pointer per
// spelling PER PROCESS, not per ProtoSpace.  Before P3 the guarantee was
// half-global with silent partial success: protoCore embeds a short ASCII
// string in the pointer word (INLINE_STRING_MAX_BYTES == 6), so a 5-byte name
// matched across spaces BY ACCIDENT while a 7-byte name missed with no error at
// all — getAttribute simply returned PROTO_NONE, which is also a value.
//
// Measured 2026-09-24 during protoST Track Y; the trap is recorded verbatim in
// protoST/src/modules/STModuleProvider.cpp:107-116.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <cstdio>
#include <string>

using namespace proto;

namespace {

// A space plus a context in it, parented to its root context.
struct Space {
    ProtoSpace   space;
    ProtoContext ctx{&space, space.rootContext, nullptr, nullptr, nullptr, nullptr};
};

}  // namespace

// The 5-byte case: inside INLINE_STRING_MAX_BYTES, so the pointer word carries
// the bytes and the two spaces agree WITHOUT any table.  This case passes both
// before and after P3, and it is in the suite to document the accident that made
// the 7-byte failure so hard to see.
TEST(GlobalInterning, ShortNameMatchedAcrossSpacesEvenBeforeP3) {
    Space a, b;
    const ProtoString* sa = ProtoString::createSymbol(&a.ctx, "value");   // 5 bytes
    const ProtoString* sb = ProtoString::createSymbol(&b.ctx, "value");
    ASSERT_NE(sa, nullptr);
    EXPECT_EQ(sa, sb) << "a name within INLINE_STRING_MAX_BYTES is embedded in "
                         "the pointer word and never reaches a symbol table";
}

// The 7-byte case: beyond INLINE_STRING_MAX_BYTES, so the name is a real
// interned cell.  BEFORE P3 THIS TEST FAILS.  That failure is the bug.
TEST(GlobalInterning, LongNameIsOnePointerAcrossSpaces) {
    Space a, b;
    const ProtoString* sa = ProtoString::createSymbol(&a.ctx, "Counter");  // 7 bytes
    const ProtoString* sb = ProtoString::createSymbol(&b.ctx, "Counter");
    ASSERT_NE(sa, nullptr);
    ASSERT_NE(sb, nullptr);
    EXPECT_EQ(sa, sb) << "interning must be process-global (P3)";
}

// The same failure as the family actually experiences it: a binding written in
// one space is unreadable from another, and the read reports ABSENCE rather than
// an error.  BEFORE P3 THIS TEST FAILS for "Counter" and would pass for "value".
TEST(GlobalInterning, AttributeWrittenInOneSpaceIsReadableFromAnother) {
    Space a, b;
    const ProtoString* keyA = ProtoString::createSymbol(&a.ctx, "Counter");
    const ProtoObject* holder =
        a.ctx.newObject(false)->setAttribute(&a.ctx, keyA, a.ctx.fromInteger(42));
    ASSERT_NE(holder, nullptr);

    const ProtoString* keyB = ProtoString::createSymbol(&b.ctx, "Counter");
    const ProtoObject* got = holder->getAttribute(&b.ctx, keyB);
    ASSERT_NE(got, nullptr);
    EXPECT_NE(got, PROTO_NONE)
        << "the read reported absence, not an error: this is the silent failure P3 closes";
    EXPECT_EQ(got->asLong(&b.ctx), 42);
}
```

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake --build $P/build_p3 -j4
$P/build_p3/test/proto_tests --gtest_filter='GlobalInterning.*' 2>&1 | tee $S/fixture-before.txt
```
**Done when** `fixture-before.txt` shows `ShortNameMatchedAcrossSpacesEvenBeforeP3` **PASSED** and both `LongNameIsOnePointerAcrossSpaces` and `AttributeWrittenInOneSpaceIsReadableFromAnother` **FAILED**. If the long-name cases pass on the unmodified tree, **stop**: the premise of this phase is wrong and the maintainer must see the evidence before anything changes.

- [ ] **Step 5: ThreadSanitizer and pause baselines**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake -S $P -B $P/build_p3_tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build $P/build_p3_tsan -j4
$P/build_p3_tsan/test/proto_tests --gtest_filter='Swarm*:GCStress*:ConcurrentMark*:SymbolIntern*' 2>&1 | tee $S/tsan-baseline.txt
grep -c "WARNING: ThreadSanitizer" $S/tsan-baseline.txt
PROTOCORE_GC_PROFILE=1 $P/build_p3/test/proto_tests --gtest_filter='GCStress*' 2>&1 | tee $S/pause-before.txt
```
protoCore has pre-existing benign races (a cell's constructor writes reference fields after `Cell(context)` has published it to the young chain — `core/ProtoSpace.cpp:676-678`). Done when both files exist and the warning count and the distinct stack tops are recorded.

- [ ] **Step 6: Commit the fixture as a known-failing test**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add test/GlobalInterningTests.cpp
git commit  # "test(interning): a cross-space fixture that fails for a 7-byte name"
```
Done when `git status --short` is clean. The commit message states that two cases are expected to fail until Task 2.

---

### Task 2: The global symbol table

**Files:** modify `headers/proto_internal.h`, `core/SymbolTable.cpp`, `core/ProtoSpace.cpp`, `core/ProtoContext.cpp`.

**Interfaces:** produces
```cpp
namespace proto {
    SymbolTable&  globalSymbolTable();
    unsigned long globalSymbolCount();
}
```
Consumes the existing `SymbolTable` class, unchanged.

- [ ] **Step 1: Correct the perennial-path comment in `allocCell` (the maintainer's point 4)**

In `core/ProtoContext.cpp`, replace line 503's comment:

```cpp
        } else {
             // Perennial allocation.  A null `this` is not an error path: it is
             // how protoCore allocates a Cell that lives for the lifetime of the
             // PROCESS.  The Cell comes straight from posix_memalign, is never
             // enrolled in a thread freelist, and addCell2Context chains it to no
             // young generation because its chaining is guarded by `if (this)`.
             // The collector therefore never sees it as a sweep candidate and
             // never frees it.
             //
             // Two callers depend on this BY CONTRACT and would break if this
             // branch were removed:
             //   * SymbolTable::intern / normalizeForSymbol — every interned
             //     symbol is perennial, so a name's canonical pointer is stable
             //     for the life of the process (core/SymbolTable.cpp);
             //   * ProtoString::createSymbol, which builds the candidate string
             //     the same way (core/ProtoString.cpp).
             //
             // A perennial Cell is NOT a GC root.  It is never swept, but it is
             // also never SCANNED, so references it holds do not keep their
             // targets alive.  That is sufficient for a symbol, whose nodes are
             // all perennial too, and INSUFFICIENT for anything that points at
             // ordinary heap objects — see ModuleRootTable, which is a real root.
             int result = posix_memalign(reinterpret_cast<void**>(&newCell), 64, sizeof(BigCell));
             if (result != 0) return nullptr;
        }
```
Done when `grep -n "rare or error" core/ProtoContext.cpp` prints nothing and `grep -c "Perennial allocation" core/ProtoContext.cpp` prints 1.

- [ ] **Step 2: Declare the accessors in `headers/proto_internal.h`**

Immediately after the `SymbolTable` class (which ends at `:1078`, before the `// ---- TupleInterner ----` banner at `:1080`):

```cpp
    // The process-wide symbol table.
    //
    // P3 (2026-09-24, maintainer's ruling): all interning is GLOBAL.  An
    // attribute key is the address of an interned symbol, so a per-space table
    // made two spaces of one process disagree about the key for the same name —
    // except for names within INLINE_STRING_MAX_BYTES, which are embedded in the
    // pointer word and matched by accident.  Half-global identity with silent
    // partial failure was the worst available state; one table makes it uniform.
    //
    // Interned objects are perennial, so a global table has no lifecycle
    // problem: there is nothing to hand back when a space dies.  The instance is
    // created on first use and DELIBERATELY NEVER DESTROYED — its cells outlive
    // every space, and a static destructor would race the exit-time teardown of
    // SharedModuleCache and ProviderRegistry.
    //
    // Thread-safety: 64 shards, each an independent bucket chain behind its own
    // std::mutex.  A shard mutex is a STRICT LEAF LOCK and is never ordered
    // against ProtoSpace::globalMutex; no Cell is allocated while one is held.
    // With one table serving several spaces, breaking that invariant would
    // deadlock two collectors, not one.
    SymbolTable& globalSymbolTable();

    // Number of symbols this process has interned.  Diagnostics and tests only
    // (P3 D8: a second ProtoSpace must intern nothing its predecessor already
    // did).  Walks every bucket chain; not for a hot path.
    unsigned long globalSymbolCount();
```

and, in `SymbolTable`'s private section after `normalizeForSymbol`:

```cpp
        friend unsigned long globalSymbolCount();
        unsigned long entryCount() const;
```
Done when `grep -c "globalSymbolTable\|globalSymbolCount\|entryCount" headers/proto_internal.h` prints 4.

- [ ] **Step 3: Define them in `core/SymbolTable.cpp`**

```cpp
// ---------------------------------------------------------------------------
// globalSymbolTable — the one table of this process.
//
// Function-local static: initialisation is thread-safe since C++11, and the
// pointer is leaked on purpose so the table is never destroyed (see the
// declaration in proto_internal.h).
// ---------------------------------------------------------------------------
SymbolTable& globalSymbolTable() {
    static SymbolTable* table = new SymbolTable();
    return *table;
}

unsigned long SymbolTable::entryCount() const {
    unsigned long total = 0;
    for (int i = 0; i < SHARD_COUNT; ++i) {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(shards[i].mutex));
        for (const Bucket* b = shards[i].head; b; b = b->next) ++total;
    }
    return total;
}

unsigned long globalSymbolCount() { return globalSymbolTable().entryCount(); }
```

and rewrite the file header's first paragraph:

```cpp
/*
 * SymbolTable.cpp — the process-wide 64-shard concurrent string interning table.
 *
 * P3 (2026-09-24): ONE table per PROCESS, reached through globalSymbolTable().
 * ProtoSpace::symbolTable is a BORROWED pointer to it and ~ProtoSpace must NOT
 * free it.  Note that ~SymbolTable frees only the Bucket nodes, never the symbol
 * cells (they are perennial) — so before P3 every destroyed space leaked its
 * whole symbol set.  One table leaks one set.
 *
 * Every interned string (symbol) is PERENNIAL. ...
 */
```
Done when `cmake --build $P/build_p3 -j4` succeeds and `nm -C $P/build_p3/libprotoCore.so | grep -c globalSymbolTable` is at least 1.

- [ ] **Step 4: Borrow the table in the constructor**

Replace `core/ProtoSpace.cpp:1251`:

```cpp
        // P3: interning is process-global.  This is a BORROWED pointer to the one
        // table of this process; the destructor must not free it.  Keeping the
        // field (rather than calling globalSymbolTable() at each use) leaves all
        // eleven existing `ctx->space->symbolTable` call sites untouched, keeps
        // the ProtoSpace layout unchanged, and preserves the mid-construction
        // sentinel that the six null checks in core/ProtoObject.cpp rely on: the
        // field is null before this line runs and non-null after, per space,
        // regardless of what other spaces have done.
        symbolTable = &globalSymbolTable();
```
Done when `grep -n "new SymbolTable()" core/ProtoSpace.cpp` prints nothing.

- [ ] **Step 5: Stop freeing it in the destructor (D4)**

Replace `core/ProtoSpace.cpp:1348-1349`:

```cpp
        // P3: `symbolTable` is a BORROWED pointer to the process-global table
        // (globalSymbolTable()).  It must NOT be deleted: the first space to die
        // would free the table every other space of this process is still using,
        // and a single-space test suite cannot reach that use-after-free.
        //
        // `tupleInterner` below IS owned by this space and is still deleted —
        // tuple interning stays per-space (P3 D5), because a tuple's key is its
        // element ADDRESSES, which are per-space, while a symbol's key is its
        // BYTES, which are not.
        delete tupleInterner;
        tupleInterner = nullptr;
```
Done when `grep -n "delete symbolTable" core/ProtoSpace.cpp` prints nothing and `grep -c "delete tupleInterner" core/ProtoSpace.cpp` prints 1.

- [ ] **Step 6: The fixture turns green**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
cmake --build $P/build_p3 -j4 && $P/build_p3/test/proto_tests --gtest_filter='GlobalInterning.*'
```
**Done when** all three cases from Task 1 Step 4 pass and `ctest --test-dir $P/build_p3` still reports the Task 1 baseline plus the new cases.

---

### Task 3: The symbol claims, each with its mutation — and the rigorous GC helper

**Files:** modify `test/GlobalInterningTests.cpp`, `test/SymbolInternTests.cpp`.

**Interfaces:** consumes `ProtoString::createSymbol`, `globalSymbolCount()`, `ProtoSpace::setHeapLimits`, `ProtoSpace::getGCCycleCount`, `ProtoSpace::reclaimedLastCycle`, `ProtoContext::safepoint` (`headers/protoCore.h:1671`). Produces `forceCycles` + `ASSERT_CYCLES_DID_REAL_WORK` and five named tests.

- [ ] **Step 1: The GC helper every P3 test uses — and why it is written this way**

Append to `test/GlobalInterningTests.cpp`:

```cpp
namespace {

constexpr int kHeadroomCells   = 40000;
constexpr int kGarbagePerBatch = 5000;

struct CycleReport {
    uint64_t      cycles;     // complete GC cycles observed
    unsigned long reclaimed;  // cells the last completed cycle swept
    long          created;    // cells this helper deliberately made garbage
};

// Forces at least `minCycles` COMPLETE cycles that actually did work.
//
// Two traps, both found in this family inside two days:
//
//  * FORCING A CYCLE IS NOT THE SAME AS SUBMITTING THE YOUNG GENERATION.  A
//    helper that allocates in a child context and drops it WITHOUT calling
//    ProtoContext::safepoint() leaves the young chain unsubmitted: the cycle
//    reclaims single-digit cells instead of the ~205,000 the helper created, and
//    any `reclaimed > 0` assertion passes near-vacuously.  Measured by Track Y
//    on 2026-09-24 — the third instance of this pattern after protoST's S15 and
//    the newList critical section.  Hence the safepoint() calls below.
//
//  * A RECLAMATION ASSERTION MUST BE CONSISTENT WITH THE GARBAGE CREATED, not
//    merely positive.  Hence `created` is returned and every caller asserts
//    against it through ASSERT_CYCLES_DID_REAL_WORK.
CycleReport forceCycles(ProtoSpace& space, ProtoContext* parent, uint64_t minCycles) {
    const uint64_t start = space.getGCCycleCount();
    long created = 0;
    space.setHeapLimits(/*soft=*/0, /*hard=*/space.heapSize + kHeadroomCells);
    for (int batch = 0; batch < 400 && space.getGCCycleCount() - start < minCycles; ++batch) {
        ProtoContext garbage(&space, parent, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < kGarbagePerBatch; ++i) {
            (void) garbage.newObject(false);
            ++created;
            // Submit the young generation at a point the embedder considers
            // safe.  WITHOUT THIS THE HELPER MEASURES NOTHING.
            if ((i & 1023) == 0) garbage.safepoint();
        }
        garbage.safepoint();
    }
    space.setHeapLimits(0, 0);
    return CycleReport{ space.getGCCycleCount() - start,
                        space.reclaimedLastCycle.load(std::memory_order_relaxed),
                        created };
}

std::string readBack(ProtoContext* c, const ProtoString* s) {
    std::string out;
    s->toUTF8String(c, out);
    return out;
}

}  // namespace

// Every GC claim in P3 asserts through this, never against `reclaimed > 0`.
// It also SELF-REPORTS its three numbers, so a run that measured nothing is
// visible in the log rather than reported as a pass.
#define ASSERT_CYCLES_DID_REAL_WORK(rep, minCycles)                              \
    do {                                                                         \
        std::fprintf(stderr, "[gc] cycles=%lu reclaimed=%lu created=%ld\n",      \
                     (unsigned long)(rep).cycles, (rep).reclaimed, (rep).created); \
        ASSERT_GE((rep).cycles, (uint64_t)(minCycles))                           \
            << "no collection ran; the test proves nothing";                     \
        ASSERT_GT((rep).reclaimed, (unsigned long)((rep).created / 10))          \
            << "the cycles reclaimed " << (rep).reclaimed << " cells against "   \
            << (rep).created << " created: the young generation was never "      \
            << "submitted, so this test would pass with the GC disabled";        \
    } while (0)
```

**Calibrate the `/ 10` factor before relying on it.** Run the helper once and record the observed `reclaimed`/`created` ratio in `$S/gc-helper-calibration.txt`; the threshold must sit well below the observed ratio and well above the 4-7-cells-out-of-205,120 failure mode (a ratio of ~3e-5). If the observed ratio is below 0.1, lower the divisor and record why — but never to the point where the unsubmitted case would pass. **Done when** `gc-helper-calibration.txt` records the observed ratio, the chosen threshold, and the ratio the unsubmitted case produces (measured by deleting both `safepoint()` calls in a throwaway edit).

- [ ] **Step 2: Claim — a symbol is perennial, so it survives any number of cycles**

```cpp
// A symbol is PERENNIAL: its cells come from posix_memalign with a null
// ProtoContext, so no cycle can reclaim them.  Perennial allocation alone is
// sufficient here — and only here — because a symbol's references point only at
// its own perennial nodes.
//
// MUTATION THAT MUST TURN THIS RED: in SymbolTable::normalizeForSymbol and in
// ProtoString::createSymbol, pass the caller's context instead of /*ctx=*/nullptr
// to ProtoStringImplementation::fromUTF8Bytes.  The symbol's cells become young
// cells of a context that then dies, and the canonical pointer is swept.
TEST(GlobalInterning, SymbolsSurviveManyCollectionCycles) {
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const char* kName = "PerennialAttributeName";   // 22 bytes, well past inline
    const ProtoString* first = ProtoString::createSymbol(&live, kName);
    ASSERT_NE(first, nullptr);

    const CycleReport rep = forceCycles(space, &live, 3);
    ASSERT_CYCLES_DID_REAL_WORK(rep, 3);

    EXPECT_EQ(ProtoString::createSymbol(&live, kName), first)
        << "the canonical pointer changed across a GC cycle";
    EXPECT_EQ(readBack(&live, first), kName) << "the symbol's own nodes were reclaimed";
}
```
Done when the test passes and the stated mutation turns it red. Record both runs in `$S/claim-perennial.txt` and revert.

- [ ] **Step 3: Claim — a symbol outlives the space that interned it (D4)**

```cpp
// The table is process-global, so a symbol interned by a space that has since
// been destroyed is still canonical and still readable.  Run under ASan.
//
// MUTATION THAT MUST TURN THIS RED: restore `delete symbolTable;` in
// ~ProtoSpace.  ASan reports a use-after-free on the bucket chain; without ASan
// the second createSymbol returns a wild pointer.
TEST(GlobalInterning, SymbolSurvivesTheSpaceThatInternedIt) {
    const char* kName = "NameInternedByADeadSpace";
    const ProtoString* fromDeadSpace = nullptr;
    {
        ProtoSpace a;
        ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
        fromDeadSpace = ProtoString::createSymbol(&ca, kName);
        ASSERT_NE(fromDeadSpace, nullptr);
    }   // space a is gone

    ProtoSpace b;
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(ProtoString::createSymbol(&cb, kName), fromDeadSpace);
    EXPECT_EQ(readBack(&cb, fromDeadSpace), kName);
}

// A symbol interned by space A is read through space B's context every time B
// looks a name up (contentEqual walks the stored symbol's rope).  That read MUST
// allocate nothing, or a global table would allocate into the reading space's
// heap on behalf of another.
//
// MUTATION THAT MUST TURN THIS RED: make ProtoString::toUTF8String build an
// intermediate protoCore string (flatten the rope through strConcat before
// iterating) — the String.prototype rope-flatten anti-pattern.  heapSize and
// allocatedCellsCount then move.
TEST(GlobalInterning, CrossSpaceLookupAllocatesNothing) {
    ProtoSpace a;
    ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
    const char* kName = "ALongNameInternedInSpaceA";
    const ProtoString* canonical = ProtoString::createSymbol(&ca, kName);
    ASSERT_NE(canonical, nullptr);

    ProtoSpace b;
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);
    ASSERT_EQ(ProtoString::createSymbol(&cb, kName), canonical);   // warm the path

    const int  heapBefore  = b.heapSize;
    const auto cellsBefore = cb.allocatedCellsCount;
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ(ProtoString::createSymbol(&cb, kName), canonical);

    EXPECT_EQ(b.heapSize, heapBefore) << "the reading space grew its heap";
    EXPECT_EQ(cb.allocatedCellsCount, cellsBefore)
        << "the reading context allocated cells to look up a foreign symbol";
}
```

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake -S $P -B $P/build_p3_asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build $P/build_p3_asan -j4
$P/build_p3_asan/test/proto_tests --gtest_filter='GlobalInterning.*' 2>&1 | tee $S/claim-teardown-asan.txt
```
Done when the ASan run is clean, the same run with `delete symbolTable` restored reports `use-after-free`, and `allocatedCellsCount` is exactly equal (the diagnostic recipe from the protoJS uninterned-key work is exact, not statistical).

- [ ] **Step 4: Claim — shared literals, and no per-space re-interning (D8)**

```cpp
// The cached literals ProtoSpace's constructor interns ("__data__" 8 bytes,
// "setAttribute" 12, "callMethod" 10) are all past INLINE_STRING_MAX_BYTES, so
// before P3 each space had its OWN pointer for them.  After P3 they are shared,
// which is the fix working: literalData is an attribute key.
//
// MUTATION THAT MUST TURN THIS RED: revert ProtoSpace's constructor to
// `symbolTable = new SymbolTable();`.
TEST(GlobalInterning, CachedLiteralsAreSharedAcrossSpaces) {
    ProtoSpace a, b;
    EXPECT_EQ(a.literalData,         b.literalData);
    EXPECT_EQ(a.literalSetAttribute, b.literalSetAttribute);
    EXPECT_EQ(a.literalCallMethod,   b.literalCallMethod);
}

// A space constructed after another has interned N names re-interns none of
// them: the table grows with distinct SPELLINGS, not with space count.  The
// spellings carry the test's own name so the case is independent of execution
// order and of --gtest_repeat (P3 D8).
TEST(GlobalInterning, ASecondSpaceInternsNothingItsPredecessorAlreadyDid) {
    const std::string prefix = "P3SecondSpaceProbe";
    ProtoSpace a;
    {
        ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < 500; ++i)
            ASSERT_NE(ProtoString::createSymbol(&ca, (prefix + std::to_string(i)).c_str()),
                      nullptr);
    }
    const unsigned long afterA = globalSymbolCount();

    ProtoSpace b;
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);
    for (int i = 0; i < 500; ++i)
        ASSERT_NE(ProtoString::createSymbol(&cb, (prefix + std::to_string(i)).c_str()),
                  nullptr);

    EXPECT_EQ(globalSymbolCount(), afterA)
        << "the second space re-interned names the first had already interned";
}
```
Done when both pass. Note in the file that `globalSymbolCount()` is process-wide, so neither test may assert an absolute count — only that it does not change.

- [ ] **Step 5: Make `test/SymbolInternTests.cpp` order-independent, and add the residency claim (D8)**

The existing file measures resident memory from `/proc/self/statm` because perennial cells appear in neither `allocatedCellsCount` nor `heapSize`. Two changes:

1. **Order-independence.** `SymbolInternTests.cpp:41-66` asserts `allocatedCellsCount` and `heapSize` are unchanged across 100k `createSymbol` calls, and `AFreshNameIsInternedOnceAndStaysCanonical` at `:94` presumes `"freshly_interned_name"` is genuinely fresh *in its space*. With one table, freshness now depends on execution order and on `--gtest_repeat`. Give every such spelling a suffix unique to the test invocation (a static counter, or `::testing::UnitTest::GetInstance()->current_test_info()->name()` plus a counter), so the case cannot pass or fail by luck.
2. **The residency claim:**

```cpp
// P3: residency grows with distinct spellings, not with the number of spaces.  A
// host that builds a runtime per test case (protoST does) must not pay for the
// same 2000 names once per case.
TEST(SymbolIntern, ResidencyDoesNotGrowWithSpaceCount) {
    auto internAll = [](ProtoSpace& s) {
        ProtoContext c(&s, s.rootContext, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < 2000; ++i)
            (void) ProtoString::createSymbol(
                &c, ("P3ResidencyProbe" + std::to_string(i)).c_str());
    };
    { ProtoSpace warm; internAll(warm); }         // populate the global table
    const long before = residentKb();
    for (int rep = 0; rep < 5; ++rep) { ProtoSpace s; internAll(s); }
    const long after = residentKb();
    std::fprintf(stderr, "[residency] grew %ld KB over 5 spaces x 2000 names\n",
                 after - before);
    // Five spaces x 2000 names x ~500 bytes would be ~5 MB if each re-interned.
    // Allow 1 MB for the five spaces' own heaps.
    EXPECT_LT(after - before, 1024L)
        << "grew " << (after - before) << " KB: names are being re-interned per space";
}
```
Done when both parts pass, `--gtest_repeat=3` on the whole `SymbolIntern.*` filter passes, and the constructor mutation of Step 4 makes the residency case fail with a growth in the megabytes. Record the measured KB in `$S/claim-residency.txt` — the number is the self-report.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/proto_internal.h core/SymbolTable.cpp core/ProtoSpace.cpp core/ProtoContext.cpp \
        test/GlobalInterningTests.cpp test/SymbolInternTests.cpp
git commit  # "feat(interning): one process-global symbol table (P3, maintainer's ruling)"
```
Done when `ctest --test-dir $P/build_p3 --output-on-failure` reports no failure other than the pre-existing ones recorded in Task 1 Step 3, and `--gtest_repeat=3` on `GlobalInterning.*:SymbolIntern.*` passes.

---

### Task 4: `ModuleRootTable` — the global module list

**Files:** modify `headers/proto_internal.h`, `CMakeLists.txt`; create `core/ModuleRoots.cpp`.

**Interfaces:** produces, in `headers/proto_internal.h`:
```cpp
namespace proto {
    class ModuleRootTable {
    public:
        static constexpr int    SHARD_COUNT = 8;
        static constexpr size_t CHUNK_SIZE  = 64;

        struct Entry {
            const ProtoObject* module;   // written before publication
            const ProtoSpace*  owner;    // the space whose heap holds it
        };
        struct Chunk {
            Entry               entries[CHUNK_SIZE];
            std::atomic<Chunk*> next{nullptr};
        };
        struct Shard {
            std::mutex          mutex;
            std::atomic<Chunk*> first{nullptr};
            Chunk*              last = nullptr;    // shard mutex
            std::atomic<size_t> published{0};
            size_t              gcCaptured = 0;    // GC thread only
        };
        Shard shards[SHARD_COUNT];

        ModuleRootTable() = default;
        ~ModuleRootTable();
        ModuleRootTable(const ModuleRootTable&) = delete;
        ModuleRootTable& operator=(const ModuleRootTable&) = delete;

        void   add(const ProtoObject* module, const ProtoSpace* owner);
        void   captureForGC();
        void   forEachCaptured(const ProtoSpace* space, void* user,
                               void (*visit)(void* user, const ProtoObject* module)) const;
        size_t size() const;

        static unsigned long lastCaptureShardReads();
        static unsigned long stwVisitViolations();
        static void          resetDiagnostics();
    };

    ModuleRootTable& globalModuleRootTable();
}
```

- [ ] **Step 1: Declare the class, with the doc block that carries the design**

Place it immediately after the `TupleInterner` class (which ends at `headers/proto_internal.h:1150`):

```cpp
    // ---- ModuleRootTable ------------------------------------------------------
    // The process-global list of loaded modules, and a GC root.
    //
    // P3 (2026-09-24, maintainer's ruling): "la lista de módulos como raíz".
    // Loading a module is for the PROCESS, not for a space or a runtime, so the
    // list is global and therefore perennial; a module anchors its contents
    // through its variables.  A module's identity is provider + path + version —
    // see ModuleIdentity in protoCore.h — and that identity is held by
    // SharedModuleCache; this table holds only the reachability.
    //
    // What this FIXES, measured before the change: SharedModuleCache is already
    // process-global and is NOT a root (core/ModuleCache.cpp), so retention came
    // from getImportModuleImpl pushing the module into the CALLING space's
    // moduleRoots — once per importing space, not globally — and a prefixed
    // cross-runtime import that calls a provider directly reached neither
    // mechanism at all.
    //
    // Why this is a ROOT and not merely perennial memory.  A perennial cell is
    // never swept, but it is also never SCANNED: the references it holds do not
    // keep their targets alive.  A symbol gets away with perennial allocation
    // alone because its nodes are all perennial too.  A module object's contents
    // are ORDINARY COLLECTABLE OBJECTS in a space's heap, so an unfreed list of
    // modules would keep the list alive and let the collector free everything it
    // points at.  The mark must ENTER through this table.
    //
    // Pause cost.  Structure and protocol are copied from TupleInterner, for the
    // reason PMQ-SPEC section 3 states: the only work allowed inside the pause is
    // thread stack roots, the mutables-tree root and the roots of global
    // structures, and none of it may be proportional to a collection's size.
    //   * Phase 2 (stop-the-world) calls captureForGC(), which reads each of the
    //     SHARD_COUNT published counters and DEREFERENCES NO ENTRY: O(8).
    //   * Phase 4 (after the world resumes) calls forEachCaptured(), which pushes
    //     exactly the captured entries onto the mark worklist while mutators keep
    //     appending.
    // This REPLACES the `for (mod : space->moduleRoots) addRootObj(mod)` loop
    // that ran inside the pause and was O(modules).  The pause gets cheaper.
    //
    // Why the concurrent walk is sound: entries live in append-only chunks that
    // never move; an entry's `module` and `owner` are written BEFORE `published`
    // is stored with release ordering, so a walker that sees the count sees the
    // entry; and a module added after the capture is a young cell of the loading
    // context, protected by it until the next cycle's capture.
    //
    // Per-owner filtering.  Each entry records the ProtoSpace whose heap holds
    // the module, and forEachCaptured visits only the collecting space's own
    // entries.  The table is global — a module is found once for the process —
    // while the TRACING stays where the cells are, so a collector never traverses
    // another space's heap on account of this table.
    //
    // APPEND-ONLY BY DESIGN.  Entries are never removed, because a loaded module
    // never unloads.  This table is therefore NOT a general-purpose embedder root
    // set: anything that must be unpinned belongs in a ProtoRootSet
    // (protoCore.h), which supports add/remove/resolve.  See P3 D12.
    //
    // Thread-safety: per-shard mutex for append only, a strict leaf lock; no Cell
    // is allocated while it is held.
```
Done when `cmake --build $P/build_p3 -j4` still succeeds (declaration only).

- [ ] **Step 2: Implement `core/ModuleRoots.cpp`**

```cpp
/*
 * ModuleRoots.cpp — the process-global module list, and a GC root.
 *
 * P3 (2026-09-24). See ModuleRootTable in headers/proto_internal.h for the
 * design and for why this is a root rather than merely unfreed memory.
 */

#include "../headers/proto_internal.h"

#include <algorithm>

namespace proto {

namespace {
    // Diagnostics for the pause-cost tests (P3 D6). Process-wide and relaxed:
    // read only by tests, never by the collector's logic.
    std::atomic<unsigned long> g_lastCaptureShardReads{0};
    std::atomic<unsigned long> g_stwVisitViolations{0};

    // Shard by the module pointer, so concurrent loads of different modules
    // rarely contend. The low 6 bits are dropped: cells are 64-byte aligned.
    inline int shardFor(const ProtoObject* module) {
        const auto bits = reinterpret_cast<uintptr_t>(module);
        return static_cast<int>((bits >> 6) % ModuleRootTable::SHARD_COUNT);
    }
}

ModuleRootTable::~ModuleRootTable() {
    for (Shard& shard : shards) {
        Chunk* chunk = shard.first.load(std::memory_order_relaxed);
        while (chunk) {
            Chunk* next = chunk->next.load(std::memory_order_relaxed);
            delete chunk;
            chunk = next;
        }
    }
}

void ModuleRootTable::add(const ProtoObject* module, const ProtoSpace* owner) {
    if (!module || module == PROTO_NONE || !owner) return;
    Shard& shard = shards[shardFor(module)];
    std::lock_guard<std::mutex> lock(shard.mutex);

    // Already published for this owner? The cache-hit path of
    // getImportModuleImpl re-roots a module on every import of the same
    // identity, so this de-duplication is load-bearing, not defensive.
    size_t remaining = shard.published.load(std::memory_order_relaxed);
    for (Chunk* c = shard.first.load(std::memory_order_relaxed); c && remaining;
         c = c->next.load(std::memory_order_relaxed)) {
        const size_t n = std::min(remaining, CHUNK_SIZE);
        for (size_t i = 0; i < n; ++i)
            if (c->entries[i].module == module && c->entries[i].owner == owner) return;
        remaining -= n;
    }

    const size_t count = shard.published.load(std::memory_order_relaxed);
    const size_t index = count % CHUNK_SIZE;
    if (index == 0) {
        Chunk* chunk = new Chunk();
        if (shard.last) shard.last->next.store(chunk, std::memory_order_release);
        else            shard.first.store(chunk, std::memory_order_release);
        shard.last = chunk;
    }
    Entry& entry = shard.last->entries[index];
    entry.module = module;
    entry.owner  = owner;
    // Publish last: the GC walks only published entries, so both words above
    // must be visible before the count is.
    shard.published.store(count + 1, std::memory_order_release);
}

// GC Phase 2, under stop-the-world. O(SHARD_COUNT) counter reads; no entry is
// dereferenced. This is the ONLY work this table does inside the pause.
void ModuleRootTable::captureForGC() {
    unsigned long reads = 0;
    for (Shard& shard : shards) {
        shard.gcCaptured = shard.published.load(std::memory_order_acquire);
        ++reads;
    }
    g_lastCaptureShardReads.store(reads, std::memory_order_relaxed);
}

// GC Phase 4, after the world has resumed. Visits every entry the last
// captureForGC() covered whose owner is `space`.
void ModuleRootTable::forEachCaptured(
        const ProtoSpace* space, void* user,
        void (*visit)(void* user, const ProtoObject* module)) const {
    // The walk belongs to the concurrent mark and must NEVER run inside the
    // pause (P3 D6). Observing stwFlag here is what the pause-cost test reads.
    if (space && space->stwFlag.load(std::memory_order_relaxed))
        g_stwVisitViolations.fetch_add(1, std::memory_order_relaxed);

    for (const Shard& shard : shards) {
        size_t remaining = shard.gcCaptured;
        for (const Chunk* c = shard.first.load(std::memory_order_acquire); c && remaining;
             c = c->next.load(std::memory_order_acquire)) {
            const size_t n = std::min(remaining, CHUNK_SIZE);
            for (size_t i = 0; i < n; ++i)
                if (c->entries[i].owner == space) visit(user, c->entries[i].module);
            remaining -= n;
        }
    }
}

size_t ModuleRootTable::size() const {
    size_t total = 0;
    for (const Shard& shard : shards)
        total += shard.published.load(std::memory_order_acquire);
    return total;
}

unsigned long ModuleRootTable::lastCaptureShardReads() {
    return g_lastCaptureShardReads.load(std::memory_order_relaxed);
}
unsigned long ModuleRootTable::stwVisitViolations() {
    return g_stwVisitViolations.load(std::memory_order_relaxed);
}
void ModuleRootTable::resetDiagnostics() {
    g_lastCaptureShardReads.store(0, std::memory_order_relaxed);
    g_stwVisitViolations.store(0, std::memory_order_relaxed);
}

// The one table of this process. Function-local static, leaked on purpose for
// the same reason globalSymbolTable() is: its entries are perennial and it must
// outlive every ProtoSpace and every other static.
ModuleRootTable& globalModuleRootTable() {
    static ModuleRootTable* table = new ModuleRootTable();
    return *table;
}

} // namespace proto
```

- [ ] **Step 3: Add the file to the build**

Add `core/ModuleRoots.cpp` to the `protoCore` source list in `CMakeLists.txt`, next to `core/ModuleCache.cpp`.

```bash
grep -n "ModuleRoots.cpp" /home/gamarino/Documentos/proyectos/protoCore/CMakeLists.txt
cmake -S $P -B $P/build_p3 -DCMAKE_BUILD_TYPE=Release && cmake --build $P/build_p3 -j4
nm -C $P/build_p3/libprotoCore.so | grep -c "globalModuleRootTable"
```
Done when the grep finds the entry, the build succeeds and `nm` prints at least 1.

- [ ] **Step 4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/proto_internal.h core/ModuleRoots.cpp CMakeLists.txt
git commit  # "feat(modules): ModuleRootTable, the process-global module list (P3)"
```

---

### Task 5: `ModuleIdentity` — provider + path + version (D11, ruled)

**Files:** modify `headers/protoCore.h`, `core/ModuleCache.h`, `core/ModuleCache.cpp`, `core/ModuleResolver.cpp`, `CMakeLists.txt`; create `core/ModuleIdentity.cpp`, `test/ModuleIdentityTests.cpp`.

**Interfaces:** produces, in `headers/protoCore.h`:
```cpp
    /**
     * @brief A module's identity in the process-global module list:
     *        provider + logical path + version.
     *
     * Ruled by the maintainer on 2026-09-24.  Path alone aliases silently: with
     * the provider prefix stripped, `provider:st/counter_lib` and a local
     * `counter_lib` collapsed into one module with the first load winning for
     * both — the same wrong-answer-no-error class as the 6-versus-7-byte
     * interning bug.
     *
     * Modules are process-level and perennial: they never unload, so two
     * coexisting versions of one module are two modules for the life of the
     * process and identity must tell them apart.  Diamond dependencies will
     * produce exactly that once modules have dependencies.
     *
     * The provider component is the provider's GUID (ModuleProvider::getGUID()),
     * never its alias: an alias is a user-facing nickname that can be re-pointed
     * at a different provider, a GUID is stable.
     *
     * A module that declares NO version has the EMPTY version, and that is a
     * permanent, first-class identity value meaning "declares no version".  It
     * is NOT a wildcard and NOT a synonym for any declared version.  There is no
     * module manifest yet; fixing the key's shape now is what guarantees that
     * introducing one later cannot re-alias any module that exists today, since
     * an unversioned module's key is byte-identical before and after.  A future
     * resolver MUST reject an empty declared version, because "" is reserved.
     */
    class ModuleIdentity
    {
    public:
        ModuleIdentity(std::string providerGUID, std::string logicalPath, std::string version);

        /** provider + path with the empty version: a module that declares none. */
        static ModuleIdentity unversioned(std::string providerGUID, std::string logicalPath);

        const std::string& getProviderGUID() const;
        const std::string& getLogicalPath()  const;
        /** "" means "this module declares no version". */
        const std::string& getVersion()      const;

        /**
         * @brief The canonical single-string key, stable across releases:
         *        providerGUID + '\x1F' + logicalPath + '\x1F' + version.
         *
         * '\x1F' (ASCII unit separator) is the separator because it cannot occur
         * in a GUID, in a POSIX or Windows path, or in any version syntax —
         * unlike '/', which is in every path, and ':', which is in the
         * `provider:` spec and in Windows drive letters.
         */
        const std::string& asKey() const;

        bool operator==(const ModuleIdentity& other) const;

    private:
        std::string providerGUID_;
        std::string logicalPath_;
        std::string version_;
        std::string key_;
    };
```
and, in `core/ModuleCache.h`:
```cpp
const ProtoObject* sharedModuleCacheGet(const ModuleIdentity& id);
void sharedModuleCacheInsert(const ModuleIdentity& id, const ProtoObject* module);
```

- [ ] **Step 1: Declare `ModuleIdentity`**

Place the class in `headers/protoCore.h` immediately before `class ModuleProvider` (`:1003`), with the doc block above verbatim. Done when the header compiles standalone: `g++ -std=c++20 -fsyntax-only -I$P $P/headers/protoCore.h`.

- [ ] **Step 2: Implement `core/ModuleIdentity.cpp`**

```cpp
/*
 * ModuleIdentity.cpp — provider + path + version (P3 D11, ruled 2026-09-24).
 */

#include "../headers/protoCore.h"

namespace proto {

namespace {
    // ASCII unit separator: see the class doc for why not '/' or ':'.
    constexpr char kSep = '\x1F';
}

ModuleIdentity::ModuleIdentity(std::string providerGUID, std::string logicalPath,
                               std::string version)
    : providerGUID_(std::move(providerGUID)),
      logicalPath_(std::move(logicalPath)),
      version_(std::move(version)) {
    key_.reserve(providerGUID_.size() + logicalPath_.size() + version_.size() + 2);
    key_ += providerGUID_;
    key_ += kSep;
    key_ += logicalPath_;
    key_ += kSep;
    key_ += version_;   // empty for a module that declares no version
}

ModuleIdentity ModuleIdentity::unversioned(std::string providerGUID,
                                           std::string logicalPath) {
    return ModuleIdentity(std::move(providerGUID), std::move(logicalPath),
                          std::string());
}

const std::string& ModuleIdentity::getProviderGUID() const { return providerGUID_; }
const std::string& ModuleIdentity::getLogicalPath()  const { return logicalPath_;  }
const std::string& ModuleIdentity::getVersion()      const { return version_;      }
const std::string& ModuleIdentity::asKey()           const { return key_;          }

bool ModuleIdentity::operator==(const ModuleIdentity& other) const {
    return key_ == other.key_;
}

} // namespace proto
```
Add `core/ModuleIdentity.cpp` to `CMakeLists.txt`. Done when the build links and `nm -C $P/build_p3/libprotoCore.so | grep -c "ModuleIdentity::asKey"` is at least 1.

- [ ] **Step 3: Re-key `SharedModuleCache`**

In `core/ModuleCache.h` and `core/ModuleCache.cpp`, change both functions to take `const ModuleIdentity&` and use `id.asKey()` as the `std::map` key. Keep the `PROTO_RESOLVE_DIAG` trace and print the key, with the `\x1F` bytes rendered as `|` so the log is readable:

```cpp
void sharedModuleCacheInsert(const ModuleIdentity& id, const ProtoObject* module) {
    if (std::getenv("PROTO_RESOLVE_DIAG")) {
        std::string shown = id.asKey();
        for (char& c : shown) if (c == '\x1F') c = '|';
        fprintf(stderr, "DEBUG: sharedModuleCacheInsert(%s, %p)\n", shown.c_str(), (void*)module);
    }
    getCache().insert(id.asKey(), module);
}
```
Done when `grep -n "const std::string& logicalPath" core/ModuleCache.h` prints nothing.

- [ ] **Step 4: Move the cache probe inside the resolution-chain loop**

This is the behaviour change D11 names. In `core/ModuleResolver.cpp`:

- Delete the pre-loop `sharedModuleCacheGet(key)` and its whole cache-hit branch (`:20-43`).
- Inside the chain loop, for each entry, determine the provider **first**, build the identity from that provider's GUID, probe the cache, and only call `tryLoad` on a miss:

```cpp
    for (unsigned long i = 0; i < chainSize; ++i) {
        // ... existing entryStr extraction ...

        ModuleProvider* provider = nullptr;
        std::unique_ptr<FileSystemProvider> owned;   // a filesystem entry's provider
        if (entryStr.size() >= 9 && entryStr.compare(0, 9, "provider:") == 0) {
            provider = ProviderRegistry::instance().getProviderForSpec(entryStr);
        } else {
            owned = std::make_unique<FileSystemProvider>(entryStr);
            provider = owned.get();
        }
        if (!provider) continue;

        // P3 D11: a module's identity is provider + path + version, so the cache
        // cannot be probed before an entry has selected a provider.  Moving the
        // probe here also fixes a real ordering bug: a module already loaded from
        // a LATER chain entry no longer shadows an EARLIER entry that can serve
        // it.  Chain order is the user's stated precedence.
        //
        // The version is empty: there is no module manifest yet, and "" is the
        // reserved, permanent identity of a module that declares none, so this
        // key is byte-identical once versions exist (P3 D11).
        const ModuleIdentity id =
            ModuleIdentity::unversioned(provider->getGUID(), key);

        if (const ProtoObject* cached = sharedModuleCacheGet(id)) {
            module = cached;
            break;
        }
        module = provider->tryLoad(key, ctx);
        if (module != nullptr && module != PROTO_NONE) {
            sharedModuleCacheInsert(id, module);
            break;
        }
        module = nullptr;
    }
```
Then replace the post-loop rooting and wrapper construction so it runs once for both the hit and the miss — the wrapper build is identical in the two old branches (`:33-42` and `:121-126`), so the duplication goes away. Keep the `ProtoContext::CriticalSection cs(ctx);` around the wrapper build; it is load-bearing (`wrapper` and `attrName` are held in C++ locals across three allocating calls).

Done when `grep -c "sharedModuleCacheGet" core/ModuleResolver.cpp` prints 1 and `grep -c "CriticalSection" core/ModuleResolver.cpp` prints 1.

- [ ] **Step 5: `test/ModuleIdentityTests.cpp`**

```cpp
// ModuleIdentityTests.cpp — P3 D11: a module's identity is provider + path +
// version, ruled by the maintainer on 2026-09-24.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"

using namespace proto;

// The three components are all part of the key.  Path alone aliased silently.
TEST(ModuleIdentity, ProviderIsPartOfTheIdentity) {
    const ModuleIdentity fromST   = ModuleIdentity::unversioned("guid-st",    "counter_lib");
    const ModuleIdentity fromDisk = ModuleIdentity::unversioned("guid-fs-cwd", "counter_lib");
    EXPECT_NE(fromST.asKey(), fromDisk.asKey())
        << "provider:st/counter_lib and a local counter_lib must be two modules";
    EXPECT_FALSE(fromST == fromDisk);
}

TEST(ModuleIdentity, VersionIsPartOfTheIdentity) {
    const ModuleIdentity v1 = ModuleIdentity("g", "p", "1.0.0");
    const ModuleIdentity v2 = ModuleIdentity("g", "p", "2.0.0");
    EXPECT_NE(v1.asKey(), v2.asKey())
        << "two coexisting versions are two modules for the life of the process";
}

// The forward-compatibility rule.  An unversioned module's key must be
// byte-identical before and after manifests exist, or introducing versions would
// silently re-alias every module loaded today.
TEST(ModuleIdentity, TheUnversionedKeyIsFrozen) {
    const ModuleIdentity u = ModuleIdentity::unversioned("g", "p");
    EXPECT_EQ(u.getVersion(), "");
    EXPECT_EQ(u.asKey(), std::string("g") + '\x1F' + "p" + '\x1F')
        << "the unversioned key's byte sequence is frozen; changing it re-aliases "
           "every module already loaded in the field";
    // "" is reserved and is NOT a synonym for any declared version.
    EXPECT_NE(u.asKey(), ModuleIdentity("g", "p", "0.0.0").asKey());
    EXPECT_NE(u.asKey(), ModuleIdentity("g", "p", "latest").asKey());
    EXPECT_NE(u.asKey(), ModuleIdentity("g", "p", "unversioned").asKey());
}

// The separator cannot be produced by any component, so no two distinct triples
// can render to one key.
TEST(ModuleIdentity, TheSeparatorCannotBeForged) {
    // A path containing a '/' or a ':' must not be confusable with a different
    // provider/path split.
    EXPECT_NE(ModuleIdentity::unversioned("a", "b/c").asKey(),
              ModuleIdentity::unversioned("a/b", "c").asKey());
    EXPECT_NE(ModuleIdentity::unversioned("a", "b:c").asKey(),
              ModuleIdentity::unversioned("a:b", "c").asKey());
}
```

Add one behaviour test for the probe relocation of Step 4, in `test/test_module_discovery.cpp` (the existing home for resolution tests): a module present under **two** chain entries must resolve from the **earlier** one even when the later one was loaded first. Read that file's existing fixture and follow its temp-directory and `setResolutionChain` idiom exactly rather than inventing one.

Done when all four identity cases and the ordering case pass, and `ctest --test-dir $P/build_p3` reports the Task 1 baseline plus the new cases. **If the ordering case reveals that any runtime depended on the old shadowing behaviour, stop and report it** — that is a behaviour change the maintainer must see, not absorb.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/protoCore.h core/ModuleIdentity.cpp core/ModuleCache.h core/ModuleCache.cpp \
        core/ModuleResolver.cpp CMakeLists.txt test/ModuleIdentityTests.cpp test/test_module_discovery.cpp
git commit  # "feat(modules): identity is provider + path + version (P3 D11, ruled)"
```

---

### Task 6: Close the cross-runtime registration gap (D13)

**Files:** modify `headers/protoCore.h`, `core/ProtoSpace.cpp`, `protoScala/src/repl/Session.cpp`; create `test/ModuleRegistrationTests.cpp`.

**Interfaces:** produces, in `class ProtoSpace`:
```cpp
        /**
         * @brief Register `module` as a process-global module root owned by this
         *        space.
         *
         * P3: the module list is global and is a GC root.  The entry records this
         * space as the owner of the module's cells, so this space's collector —
         * and only this space's — traces it.  Entries are never removed: a loaded
         * module is perennial, for the process.
         *
         * Thread-safe.  Allocates no Cell.
         */
        void addModuleRoot(const ProtoObject* module);

        /** @brief Module roots this process holds.  Diagnostics and tests. */
        static unsigned long moduleRootCount();

        /**
         * @brief Publish a module this embedder loaded itself, under the ruled
         *        identity, and root it in this space.  Returns the published
         *        module — which is the one already published under `id` if there
         *        is one, so two importers of the same identity share a module.
         *
         * For an embedder that calls a ModuleProvider directly instead of going
         * through getImportModule.  Before P3 such a load reached neither
         * SharedModuleCache nor any moduleRoots, and its only anchor was inside
         * the providing runtime — so destroying that runtime dropped it.
         */
        const ProtoObject* registerModule(const ModuleIdentity& id, const ProtoObject* module);

        /** @brief The module published under `id`, or nullptr. */
        static const ProtoObject* findModule(const ModuleIdentity& id);
```

- [ ] **Step 1: Declare the four methods**

Add them in `headers/protoCore.h` immediately after `getImportModule` (`:2096`). Leave `moduleRoots` and `moduleRootsMutex` **untouched for now** — retiring them is Task 8, gated on Task 7.

- [ ] **Step 2: Define them in `core/ProtoSpace.cpp`**

```cpp
    void ProtoSpace::addModuleRoot(const ProtoObject* module) {
        globalModuleRootTable().add(module, this);
    }

    unsigned long ProtoSpace::moduleRootCount() {
        return static_cast<unsigned long>(globalModuleRootTable().size());
    }

    const ProtoObject* ProtoSpace::registerModule(const ModuleIdentity& id,
                                                   const ProtoObject* module) {
        if (!module || module == PROTO_NONE) return PROTO_NONE;
        // Publish-or-adopt: if this identity is already served, root and return
        // the existing module so two importers share one module, which is what
        // "a module is loaded once for the process" means.
        if (const ProtoObject* existing = sharedModuleCacheGet(id)) {
            addModuleRoot(existing);
            return existing;
        }
        sharedModuleCacheInsert(id, module);
        addModuleRoot(module);
        return module;
    }

    const ProtoObject* ProtoSpace::findModule(const ModuleIdentity& id) {
        return sharedModuleCacheGet(id);
    }
```
`core/ProtoSpace.cpp` must include `ModuleCache.h` for these; check whether it already does and add the include if not.

- [ ] **Step 3: `test/ModuleRegistrationTests.cpp`**

```cpp
// P3 D13: an embedder that loads a module itself can publish it under the ruled
// identity and have it rooted in ITS OWN space.
//
// Before P3, a prefixed cross-runtime import (protoScala's Session::loadForeign)
// called provider->tryLoad directly, so it reached neither SharedModuleCache nor
// any moduleRoots, and its only anchor was inside the PROVIDING runtime.
// Destroying that runtime while an importer still held its values dropped the
// only anchor; the tests survived only because construction order happened to
// make the provider destroyed last, and nothing enforced that.
//
// MUTATION THAT MUST TURN THIS RED: make registerModule insert into the cache
// without calling addModuleRoot.  The module is then found by identity and
// collected anyway — the "never freed is not a root" distinction again.
TEST(ModuleRegistration, ARegisteredModuleIsRootedInTheRegisteringSpace) {
    // Space `provider` plays the providing runtime; space `importer` plays the
    // importer.  The module is built in `importer`'s heap (as buildCallerFacade
    // builds its facade in the caller's context) and registered by `importer`,
    // then `provider` is DESTROYED while `importer` still holds the identity.
    // Several cycles later the module's contents are intact.
    // Implementation: reuse buildModuleLike / moduleIntact / forceCycles from
    // ModuleRootGCTests.cpp by declaring them in a small shared test header, or
    // duplicate them locally — do NOT make one test file depend on another's
    // link order.
}

// Two importers of the same identity share one module.
TEST(ModuleRegistration, TheSameIdentityYieldsTheSameModule) {
    ProtoSpace a, b;
    ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);
    const ModuleIdentity id = ModuleIdentity::unversioned("guid-test", "shared_lib");

    const ProtoObject* first  = a.registerModule(id, ca.newObject(false));
    const ProtoObject* second = b.registerModule(id, cb.newObject(false));
    EXPECT_EQ(first, second) << "a module is loaded once for the process";
    EXPECT_EQ(ProtoSpace::findModule(id), first);
}
```
Write the first body against `ModuleRootGCTests.cpp` (Task 9), so this step runs **after** Task 9 or duplicates the three helpers locally. Prefer duplication; the plan explicitly does not want cross-test-file link dependencies.

- [ ] **Step 4: Change protoScala's `loadForeign` to publish through `registerModule`**

**Ask the maintainer before touching protoScala** — a sibling agent was committing there. On a branch `feature/p3-root-migration` in protoScala, in `src/repl/Session.cpp:472-497`:

```cpp
    // P3 D13: publish through the kernel so the module is in the process-global
    // list under the ruled identity (provider GUID + path + version) and is
    // rooted in THIS space — the space whose lifetime this importer controls.
    // Before P3 the only anchor was inside the providing runtime, so destroying
    // it dropped the module while this Session still held its values.
    //
    // The local cache below stays: it is this Session's ModuleExports view, not
    // the module's identity. Its key becomes the kernel's key so the two cannot
    // disagree (the old `providerSpec + "/" + logicalPath` was half the answer).
    const proto::ModuleIdentity id =
        proto::ModuleIdentity::unversioned(provider->getGUID(), logicalPath);
    mod = space_.registerModule(id, mod);
```
and change `cacheKey` to `id.asKey()`. Note that `providerSpec` is an alias-or-GUID spec while the identity takes the resolved provider's GUID, so the key must be built **after** `getProviderForSpec` succeeds — which is where the snippet above sits.

Done when protoScala builds and `ctest --test-dir $Q/build_p3` matches the Task 1 Step 3 number, and `PROTO_RESOLVE_DIAG=1` on a cross-runtime import prints one `sharedModuleCacheInsert` line with the GUID-qualified key.

- [ ] **Step 5: Commit, in two repositories**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/protoCore.h core/ProtoSpace.cpp test/ModuleRegistrationTests.cpp
git commit  # "feat(modules): registerModule/findModule close the cross-runtime gap (P3 D13)"

cd /home/gamarino/Documentos/proyectos/protoScala
git add src/repl/Session.cpp
git commit  # "fix(interop): publish foreign modules through the kernel (P3 D13)"
```

---

### Task 7: Migrate protoPython off `space->moduleRoots` — the gating task (D12)

**Files:** modify `protoPython/src/library/PythonEnvironment.h`, `PythonEnvironment.cpp`, `Compiler.cpp`, `ExecutionEngine.cpp`.

**Interfaces:** consumes `ProtoSpace::createRootSet`, `ProtoSpace::destroyRootSet`, and `ProtoRootSet::add` / `remove` / `resolve` / `size` — **note the real spelling** (`headers/protoCore.h:1882-1896`); the P2 plan's snippets say `addRoot`/`removeRoot`, which do not exist. Produces a `ProtoRootSet*` member on `PythonEnvironment` and zero `moduleRoots` references in protoPython.

**Why this task exists and why it comes before Task 8.** protoPython uses `space->moduleRoots` as a general-purpose, *removable*, slot-replacing embedder root set — eight writers and three readers, and most of what they pin is not a module. `ModuleRootTable` is append-only by design and is not a substitute. Task 8 removes the stop-the-world loop that scans `moduleRoots`, so protoPython must be migrated **first** or it spends a commit with its roots silently gone. **Losing a GC root does not fail a test; it corrupts memory later.**

- [ ] **Step 1: Branch, and get protoPython's own baseline**

```bash
R=/home/gamarino/Documentos/proyectos/protoPython; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
git -C $R switch -c feature/p3-root-migration
rm -rf $R/build_release && cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $R/build_release -j4
ctest --test-dir $R/build_release --output-on-failure 2>&1 | tail -30 | tee $S/protopython-before.txt
```
Done when `protopython-before.txt` reports **582/583** with the pre-existing `protopy_import_site` as the only failure.

- [ ] **Step 2: Enumerate every site, so none is missed**

```bash
grep -rn "moduleRoots\|moduleRootsMutex" /home/gamarino/Documentos/proyectos/protoPython/src \
  | tee /home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning/protopython-roots.txt
```
Expected, as measured 2026-09-24: `PythonEnvironment.cpp:16558` (erase in the destructor), `:18056-18063` (thread-roots dict), `:22287` (`sent`), `:22640` (bulk-add of ~20 prototypes), `:23962-23970` (builtins slot replacement), `:28687` (interned symbols), `Compiler.cpp:5559-5562` (ByteBuffer pin), `ExecutionEngine.cpp:1367` (bytecode cache). Done when the file lists every site and the step's report classifies each as *removable pin*, *module*, or *symbol (no root needed)*.

- [ ] **Step 3: Add the root set to `PythonEnvironment`**

In `PythonEnvironment.h`:

```cpp
    // P3 D12: protoPython's GC pins live in a ProtoRootSet, which is what
    // protoCore provides for exactly this (protoCore.h: "Lets a runtime built on
    // protoCore pin ProtoObjects as GC roots without smuggling them into
    // setAttribute-on-globals").  They used to live in ProtoSpace::moduleRoots,
    // a std::vector the collector iterated INSIDE the stop-the-world window,
    // which protoPython also erased from and slot-replaced by hand.
    proto::ProtoRootSet* gcRoots_ = nullptr;
    // The builtins module's pin, replaced in place when the module is rebuilt.
    proto::ProtoRootSet::Handle builtinsRoot_ = proto::ProtoRootSet::kNullHandle;
```

In the constructor, after `space_` is available:
```cpp
    gcRoots_ = space_->createRootSet("protoPython");
```
In `~PythonEnvironment`, replacing the `remove_if_match` block at `:16558-16575`:
```cpp
    // The whole set goes at once; ProtoSpace::destroyRootSet unregisters it
    // before any further GC cycle can iterate it.
    if (gcRoots_) { space_->destroyRootSet(gcRoots_); gcRoots_ = nullptr; }
```

- [ ] **Step 4: Convert each writer**

- `:22640-22660` — the ~20 prototype `addRoot` lambda becomes `if (obj) gcRoots_->add(obj);`.
- `:18063` — `gcRoots_->add(s_globalThreadRootsDict);`.
- `:22287` — `gcRoots_->add(sent);`.
- `:23962-23970` — the builtins slot replacement becomes, with no mutex of its own (`ProtoRootSet` is thread-safe):
  ```cpp
  // Root the new version and release the old one, so an echoed value does not
  // add a root for good.  ProtoRootSet::add/remove are safe from any thread.
  const proto::ProtoRootSet::Handle fresh = gcRoots_->add(updated);
  if (builtinsRoot_ != proto::ProtoRootSet::kNullHandle) gcRoots_->remove(builtinsRoot_);
  builtinsRoot_ = fresh;
  ```
  **Add before remove**, never the reverse: a pause landing between a remove and an add would see neither version rooted.
- `Compiler.cpp:5562` and `ExecutionEngine.cpp:1367` — `ctx->space->...` becomes the environment's `gcRoots_->add(...)`. If those files have no `PythonEnvironment&` in scope, add a small accessor rather than reaching into the space.
- `:28687` — **delete the site and `g_internRoots` with it.** Symbols are perennial: `SymbolTable::intern` allocates with a null `ProtoContext`, so a symbol cell is never a sweep candidate and needs no root. Rooting one was always a no-op that cost a pause iteration. Say so in the deleted code's place.

- [ ] **Step 5: Verify — suite, ASan, and GC pressure**

```bash
R=/home/gamarino/Documentos/proyectos/protoPython; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
grep -rn "moduleRoots" $R/src | grep -v "^.*: *//"        # must be empty
cmake --build $R/build_release -j4
ctest --test-dir $R/build_release --output-on-failure 2>&1 | tail -30 | tee $S/protopython-migrated.txt

rm -rf $R/build_asan && cmake -S $R -B $R/build_asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build $R/build_asan -j4
ctest --test-dir $R/build_asan --output-on-failure 2>&1 | tail -30 | tee $S/protopython-asan.txt

# GC pressure: a low ProtoSpace limit makes cycles actually run, so a lost root
# becomes a crash or a wrong answer rather than a latent one.
PROTOCORE_MEMORY_LIMIT=200000 ctest --test-dir $R/build_release --output-on-failure 2>&1 \
  | tail -30 | tee $S/protopython-gcpressure.txt
```
**Done when** the grep is empty, `protopython-migrated.txt` reports 582/583 with the same single pre-existing failure, `protopython-asan.txt` reports no `ERROR: AddressSanitizer`, and the GC-pressure run reports no new failure. Read `protoPython/CLAUDE.md` for the repository's own env-var spelling of the memory limit and use that, not the guess above, if they differ. **A suite that passes without the ASan and pressure runs is not evidence here** — that is the whole lesson of protoST's S15.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoPython
git add src/library/PythonEnvironment.h src/library/PythonEnvironment.cpp \
        src/library/Compiler.cpp src/library/ExecutionEngine.cpp
git commit  # "refactor(gc): protoPython's pins move to a ProtoRootSet (P3 D12)"
```

---

### Task 8: Wire the table into the collector, and delete the linear pause work

**Files:** modify `core/ProtoSpace.cpp`, `core/ModuleResolver.cpp`, `headers/protoCore.h`.

**Blocked on:** Task 7. Do not start until `grep -rn moduleRoots` is empty across all five runtimes' `src/`.

- [ ] **Step 1: Confirm the gate**

```bash
grep -rn "moduleRoots" /home/gamarino/Documentos/proyectos/{protoPython,protoJS,protoST,protoClojure,protoScala}/src
```
Done when this prints nothing. **If it prints anything, stop**: removing the loop would drop that root.

- [ ] **Step 2: Retire the fields in comment**

In `headers/protoCore.h`, replacing the declaration at `:2350-2351`:

```cpp
        // P3: RETIRED.  The module list is the process-global ModuleRootTable
        // (core/ModuleRoots.cpp); removable embedder pins belong in a
        // ProtoRootSet (see createRootSet above).  These two fields are retained
        // so the ProtoSpace layout does not change — the same treatment
        // `tupleRoot` and `stringInternMap` already get — and are held empty and
        // never iterated.  Do not add to them: call addModuleRoot() for a module,
        // or createRootSet() for anything that must be unpinned.
        std::vector<const ProtoObject*> moduleRoots;
        std::mutex moduleRootsMutex;
```

- [ ] **Step 3: Replace the O(modules) stop-the-world loop**

In `core/ProtoSpace.cpp`, delete the block at `:452-457` and put in its place, immediately before the tuple-interner capture:

```cpp
                // Module roots (P3).  The module list is process-global and is a
                // root: a module's contents are ordinary collectable objects, so
                // an unfreed list would not keep them alive.
                //
                // Record only each shard's published entry count here —
                // O(SHARD_COUNT) under stop-the-world, no entry dereferenced —
                // exactly as the tuple interner does below.  Phase 4 pushes the
                // captured entries after the world resumes.
                //
                // This REPLACED a `for (mod : space->moduleRoots) addRootObj(mod)`
                // loop that ran HERE, inside the pause, holding moduleRootsMutex,
                // and was O(modules).  It was the one term in the documented pause
                // profile that scaled with the program, and it was missing from
                // the cost table.  Do not put a per-module loop back into this
                // window.
                globalModuleRootTable().captureForGC();
```

- [ ] **Step 4: Push the captured entries in Phase 4**

In `core/ProtoSpace.cpp`, immediately before the existing tuple-interner block at `:648-653` — which is already after `stwFlag.store(false)` at `:634`:

```cpp
                // Module roots recorded by Phase 2 are roots (P3).  Only this
                // space's own entries: the table is global, the tracing is not.
                globalModuleRootTable().forEachCaptured(
                    space, &workList, [](void* user, const ProtoObject* module) {
                        /* push `module` onto the worklist using the SAME
                           ProtoObject* -> Cell* conversion the neighbouring
                           root code uses */
                    });
```
**Interface check, not a guess:** read `addRootObj`'s body, defined just above the Phase-2 root block in `core/ProtoSpace.cpp`, and use **exactly** that conversion. If `addRootObj` is in scope at Phase 4, call it instead of pushing to `workList` by hand. Do not invent a `Cell::fromObject`. Done when the build compiles with no cast written by hand.

- [ ] **Step 5: Redirect `ModuleResolver`'s rooting**

`core/ModuleResolver.cpp` now has one post-loop rooting site (Task 5 Step 4 merged the two). Make it:

```cpp
        // P3: the module list is process-global and is a GC root.  A module found
        // in the cache may have been loaded by another space; it is rooted in
        // THIS space as well, which is what the pre-P3 code did through
        // space->moduleRoots and what keeps a cross-space import alive.
        space->addModuleRoot(module);
```

```bash
grep -rn "moduleRoots" /home/gamarino/Documentos/proyectos/protoCore/core /home/gamarino/Documentos/proyectos/protoCore/headers
```
Done when the only hits are the two retired field declarations and the retirement comment — no `push_back`, no iteration, no `moduleRootsMutex` lock anywhere.

- [ ] **Step 6: Prove the layout did not change**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cat > $S/p3_layout.cpp <<'EOF'
#include "headers/protoCore.h"
#include <cstddef>
#include <cstdio>
int main() {
    printf("sizeof(ProtoSpace)=%zu\n", sizeof(proto::ProtoSpace));
    printf("symbolTable=%zu\n",   offsetof(proto::ProtoSpace, symbolTable));
    printf("tupleInterner=%zu\n", offsetof(proto::ProtoSpace, tupleInterner));
    printf("moduleRoots=%zu\n",   offsetof(proto::ProtoSpace, moduleRoots));
    printf("rootSets_=%zu\n",     offsetof(proto::ProtoSpace, rootSets_));
    return 0;
}
EOF
g++ -std=c++20 -I$P $S/p3_layout.cpp -o $S/p3_layout && $S/p3_layout | tee $S/layout-after.txt
git -C $P stash && g++ -std=c++20 -I$P $S/p3_layout.cpp -o $S/p3_layout_before \
  && $S/p3_layout_before > $S/layout-before.txt; git -C $P stash pop
diff $S/layout-before.txt $S/layout-after.txt
```
`protoCore/check_offset.cpp` already does this for `moduleRoots` and is the precedent. **Done when the `diff` is empty** — that is the evidence for D7's "additive, not ABI-breaking" claim. If it is not empty, stop: the version decision changes.

- [ ] **Step 7: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/protoCore.h core/ProtoSpace.cpp core/ModuleResolver.cpp
git commit  # "perf(gc): module roots captured in O(1) under STW, walked in mark (P3)"
```
Done when `ctest --test-dir $P/build_p3 --output-on-failure` still reports the Task 1 baseline plus the new cases.

---

### Task 9: The module-root claims — including "never freed ≠ is a root"

**Files:** create `test/ModuleRootGCTests.cpp`.

**Interfaces:** consumes `ProtoSpace::addModuleRoot`, `ProtoSpace::moduleRootCount`, `ModuleRootTable::lastCaptureShardReads`, `stwVisitViolations`, `resetDiagnostics`, `globalModuleRootTable()`, and its own copies of `forceCycles` / `ASSERT_CYCLES_DID_REAL_WORK` (duplicate them from `GlobalInterningTests.cpp`; do not make one test file depend on another's link order).

- [ ] **Step 1: Claim — a module root keeps its contents alive**

```cpp
// ModuleRootGCTests.cpp — P3: the module list is a GC ROOT, not merely memory
// that is never freed.
//
// The distinction is the whole point of the phase.  A perennial cell is not
// swept, but it is also not SCANNED, so the references it holds do not keep
// their targets alive.  A symbol gets away with perennial allocation alone; a
// module object's contents are ordinary collectable objects and do not.

#include <gtest/gtest.h>
#include "../headers/protoCore.h"
#include "../headers/proto_internal.h"

#include <cstdio>
#include <string>

using namespace proto;

namespace {

// --- forceCycles / CycleReport / ASSERT_CYCLES_DID_REAL_WORK: copy verbatim
// --- from test/GlobalInterningTests.cpp, including its two comment paragraphs
// --- about safepoint() and reclamation consistency.  Duplicated on purpose.

// A stand-in for a loaded module: an object whose attribute "moduleVariable"
// holds a freshly allocated, verifiable structure.  This is the shape that
// matters — a module anchors its contents through its variables.
const ProtoObject* buildModuleLike(ProtoContext* c, long tag) {
    ProtoContext::CriticalSection cs(c);
    const ProtoObject* contents =
        c->newList()->appendLast(c, c->fromInteger(tag))
                    ->appendLast(c, c->fromInteger(~tag))->asObject(c);
    const ProtoString* key = ProtoString::createSymbol(c, "moduleVariable");
    return c->newObject(false)->setAttribute(c, key, contents);
}

bool moduleIntact(ProtoContext* c, const ProtoObject* module, long tag) {
    const ProtoString* key = ProtoString::createSymbol(c, "moduleVariable");
    const ProtoObject* contents = module->getAttribute(c, key);
    if (!contents || contents == PROTO_NONE) return false;
    const ProtoList* l = contents->asList(c);
    return l && l->getSize(c) == 2 &&
           l->getAt(c, 0)->asLong(c) == tag &&
           l->getAt(c, 1)->asLong(c) == ~tag;
}

}  // namespace

// The module is registered as a module root and then dropped from every other
// reference.  Several cycles later its variable, and the structure behind it,
// are intact.
//
// MUTATION THAT MUST TURN THIS RED (1): delete the
// globalModuleRootTable().forEachCaptured(...) call from GC Phase 4 in
// core/ProtoSpace.cpp.  The table is still captured, still never freed — and the
// module's CONTENTS are collected, because the mark never entered through it.
// This is the "never freed is not a root" proof.
//
// MUTATION THAT MUST TURN THIS RED (2): keep Phase 4 as it is, but build the
// module with a NULL ProtoContext so its own cell is perennial, and do not
// register it.  The module cell survives; its contents do not.  The same proof,
// from the other side — and the one the maintainer asked for by name.
TEST(ModuleRootGC, ModuleContentsSurviveWhenOnlyTheRootHoldsThem) {
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    const ProtoObject* module = nullptr;
    {
        ProtoContext loader(&space, &live, nullptr, nullptr, nullptr, nullptr);
        module = buildModuleLike(&loader, 7);
        ASSERT_NE(module, nullptr);
        space.addModuleRoot(module);
        loader.safepoint();     // submit the loader's young chain
    }   // the loading context is gone; only the module root holds it

    const CycleReport rep = forceCycles(space, &live, 3);
    ASSERT_CYCLES_DID_REAL_WORK(rep, 3);

    EXPECT_TRUE(moduleIntact(&live, module, 7))
        << "the module root did not keep the module's contents alive";
}
```

- [ ] **Step 2: Claim — the pause cost is constant in the number of modules (D6)**

```cpp
// The stop-the-world capture reads SHARD_COUNT counters and nothing else, for
// any number of modules.
//
// MUTATION THAT MUST TURN THIS RED: restore
//   { std::lock_guard<std::mutex> l(space->moduleRootsMutex);
//     for (const ProtoObject* m : space->moduleRoots) addRootObj(m); }
// in Phase 2 in place of captureForGC().  lastCaptureShardReads() then stays 0
// while the pause does O(modules) work.
TEST(ModuleRootGC, CaptureUnderStopTheWorldIsConstant) {
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);

    ModuleRootTable::resetDiagnostics();
    CycleReport rep = forceCycles(space, &live, 2);
    ASSERT_CYCLES_DID_REAL_WORK(rep, 2);
    const unsigned long readsWithNoModules = ModuleRootTable::lastCaptureShardReads();
    EXPECT_EQ(readsWithNoModules,
              static_cast<unsigned long>(ModuleRootTable::SHARD_COUNT));

    for (long i = 0; i < 2000; ++i) space.addModuleRoot(buildModuleLike(&live, i));
    ASSERT_GE(ProtoSpace::moduleRootCount(), 2000ul);

    ModuleRootTable::resetDiagnostics();
    rep = forceCycles(space, &live, 2);
    ASSERT_CYCLES_DID_REAL_WORK(rep, 2);
    EXPECT_EQ(ModuleRootTable::lastCaptureShardReads(), readsWithNoModules)
        << "the pause's module work grew with the number of modules";
}

// And the per-entry walk never happens inside the pause.
//
// MUTATION THAT MUST TURN THIS RED: move the forEachCaptured call from Phase 4
// up into Phase 2, before stwFlag.store(false).  stwVisitViolations() goes
// positive.
TEST(ModuleRootGC, TheWalkNeverRunsInsideThePause) {
    ProtoSpace space;
    ProtoContext live(&space, space.rootContext, nullptr, nullptr, nullptr, nullptr);
    for (long i = 0; i < 2000; ++i) space.addModuleRoot(buildModuleLike(&live, i));

    ModuleRootTable::resetDiagnostics();
    const CycleReport rep = forceCycles(space, &live, 3);
    ASSERT_CYCLES_DID_REAL_WORK(rep, 3);
    EXPECT_EQ(ModuleRootTable::stwVisitViolations(), 0ul)
        << "the module walk ran while the world was stopped";
}
```

- [ ] **Step 3: Claim — one space's collector does not trace another's module (D1)**

```cpp
// The table is global; the TRACING is per owner.  A module owned by space B is
// not pushed onto space A's worklist, so A never traverses B's heap on account
// of this table.
//
// MUTATION THAT MUST TURN THIS RED: drop the `if (c->entries[i].owner == space)`
// guard in ModuleRootTable::forEachCaptured.  Space A then visits space B's
// module and the counter below moves.
TEST(ModuleRootGC, ACollectorTracesOnlyItsOwnSpacesModules) {
    ProtoSpace a, b;
    ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);

    b.addModuleRoot(buildModuleLike(&cb, 11));

    // The walk visits only entries the last captureForGC() covered, so capture
    // first; before that both walks legitimately see nothing.
    globalModuleRootTable().captureForGC();

    long visitedForA = 0, visitedForB = 0;
    auto count = [](void* user, const ProtoObject*) { ++*static_cast<long*>(user); };
    globalModuleRootTable().forEachCaptured(&a, &visitedForA, count);
    globalModuleRootTable().forEachCaptured(&b, &visitedForB, count);

    EXPECT_GE(visitedForB, 1) << "space b did not see its own module";
    EXPECT_EQ(visitedForA, 0) << "space a traced a module owned by space b";
}
```

- [ ] **Step 4: Run each mutation and record it**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake --build $P/build_p3 -j4
$P/build_p3/test/proto_tests --gtest_filter='ModuleRootGC.*:ModuleRegistration.*' 2>&1 | tee $S/claim-moduleroots.txt
```
**Done when** every case passes, and for each of the **six** mutations named in Tasks 6 and 9 a throwaway edit is applied, the named test is observed **red**, the edit is reverted, and the red run is appended to `$S/claim-moduleroots-mutations.txt` with the mutation's name. A claim whose mutation was not observed red is not implemented.

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add test/ModuleRootGCTests.cpp
git commit  # "test(gc): module roots are real roots, and the pause stays O(1) (P3)"
```

---

### Task 10: The tuple interner stays per-space, and a test keeps it that way

**Files:** modify `test/GlobalInterningTests.cpp`, `headers/proto_internal.h`.

**Interfaces:** consumes the tuple-building idiom of `test/test_tuple.cpp` and `ProtoString::createSymbol`. Produces one named test and one doc block. **No code change to `TupleInterner`.**

- [ ] **Step 1: Record the decision in `TupleInterner`'s doc block**

Extend the existing block at `headers/proto_internal.h:1080-1096`:

```cpp
    // P3 (2026-09-24): tuple interning stays PER SPACE, deliberately.  Intern
    // globally what is keyed by CONTENT; keep per-space what is keyed by
    // ADDRESS.  A symbol's key is its bytes, which are space-independent, so a
    // global symbol table produces cross-space identity.  THIS table's key is
    // the SLOT POINTERS (hashSlots / find), and addresses are not
    // space-independent, so a global tuple table would produce no cross-space
    // identity for the ordinary case at all.
    //
    // The one case where it WOULD alias is the hazard, and it is new since P3:
    // symbols are now the same pointers in every space, so a tuple of symbols
    // built in two spaces has identical slots.  A global table would return one
    // node, whose cells live in whichever space created it first — one collector
    // tracing another's heap, and elements kept alive by a foreign collector,
    // for no benefit.  See the P3 plan, D5, for what going global would
    // additionally require (an owner per entry, owner-filtered marking, and a
    // purgeSpace at teardown).
    //
    // test/GlobalInterningTests.cpp :: GlobalInterning.TupleInternerStaysPerSpace
    // fails if this table is made a singleton.
```

- [ ] **Step 2: The guard test**

```cpp
// P3 D5: the tuple interner is PER SPACE.  Two spaces building a tuple of the
// same two globally-interned symbols get two distinct tuple nodes, each in its
// own heap, each internally consistent.
//
// MUTATION THAT MUST TURN THIS RED: replace `context->space->tupleInterner` in
// ProtoTupleImplementation's interning path (core/ProtoTuple.cpp:219) with a
// process-global singleton.  The two addresses then coincide, which is exactly
// the cross-space coupling D5 declines.
TEST(GlobalInterning, TupleInternerStaysPerSpace) {
    ProtoSpace a, b;
    ProtoContext ca(&a, a.rootContext, nullptr, nullptr, nullptr, nullptr);
    ProtoContext cb(&b, b.rootContext, nullptr, nullptr, nullptr, nullptr);

    // Two names past INLINE_STRING_MAX_BYTES, so they are real interned cells
    // and — after P3 — the same pointers in both spaces.
    const ProtoString* n1a = ProtoString::createSymbol(&ca, "TupleElementOne");
    const ProtoString* n2a = ProtoString::createSymbol(&ca, "TupleElementTwo");
    const ProtoString* n1b = ProtoString::createSymbol(&cb, "TupleElementOne");
    const ProtoString* n2b = ProtoString::createSymbol(&cb, "TupleElementTwo");
    ASSERT_EQ(n1a, n1b);
    ASSERT_EQ(n2a, n2b);   // the premise: identical slot pointers

    // INTERFACE CHECK: use test/test_tuple.cpp's OWN two-element builder here,
    // not an invented one.
    const ProtoTuple* ta = /* build {n1a, n2a} in ca */;
    const ProtoTuple* tb = /* build {n1b, n2b} in cb */;
    const ProtoTuple* ta2 = /* build {n1a, n2a} in ca again */;
    ASSERT_NE(ta, nullptr);
    ASSERT_NE(tb, nullptr);

    EXPECT_NE(ta, tb)  << "the tuple interner has been made global; see P3 D5";
    EXPECT_EQ(ta, ta2) << "per-space tuple interning stopped working";
}
```
**The second assertion is load-bearing:** a mutation that broke per-space interning entirely would otherwise pass the first.

- [ ] **Step 3: Run and mutate**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake --build $P/build_p3 -j4
$P/build_p3/test/proto_tests --gtest_filter='GlobalInterning.TupleInternerStaysPerSpace' 2>&1 | tee -a $S/claim-tuples.txt
```
Done when it passes and the singleton mutation turns the first assertion red, with the red run recorded.

- [ ] **Step 4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add headers/proto_internal.h test/GlobalInterningTests.cpp
git commit  # "test(tuples): the tuple interner stays per-space (P3 D5)"
```

---

### Task 11: Concurrency, sanitizers and the measured pause

**Files:** modify `test/GlobalInterningTests.cpp`. Output: `$S/tsan-after.txt`, `$S/asan-after.txt`, `$S/pause-after.txt`.

**Interfaces:** consumes `ProtoSpace::newThread` (registered protoCore threads that park at stop-the-world), `PROTOCORE_GC_PROFILE`.

- [ ] **Step 1: Two spaces, four threads, overlapping spellings**

```cpp
// The global table is now contended across spaces.  Four registered protoCore
// threads — two per space — intern overlapping spellings concurrently; every
// thread must agree on the canonical pointer for every name.
//
// Threads are spawned with ProtoSpace::newThread, NOT raw std::thread: an
// unregistered thread is not counted in runningThreads, never parks at a
// stop-the-world, and would make this test weaker rather than stronger (P2 D10).
//
// MUTATION THAT MUST TURN THIS RED: drop the double-checked re-check inside the
// shard lock in SymbolTable::intern (core/SymbolTable.cpp:168-174), so two
// threads that normalise the same spelling concurrently both insert.  Two
// canonical pointers for one name.
TEST(GlobalInterning, ConcurrentInterningAcrossSpacesAgreesOnOnePointer) {
    // 200 spellings, all past INLINE_STRING_MAX_BYTES so every one is a real
    // interned cell, interned by four threads across two spaces.
    //
    // Body: build the 200 spellings up front (prefixed with this test's name so
    // the case is order-independent, P3 D8); each thread's main function interns
    // all 200 in a rotated order and writes the pointers into its own array;
    // the test then asserts all four arrays are equal element-wise, and equal to
    // a fifth set interned on the main thread.
    //
    // INTERFACE CHECK: copy test/SwarmTests.cpp's ProtoSpace::newThread + join
    // idiom exactly; do not invent a thread-main signature.
}
```
Done when the test passes with 200 spellings and four threads, and the re-check mutation turns it red.

- [ ] **Step 2: ThreadSanitizer, compared to the Task 1 baseline**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake --build $P/build_p3_tsan -j4
$P/build_p3_tsan/test/proto_tests \
  --gtest_filter='Swarm*:GCStress*:ConcurrentMark*:SymbolIntern*:GlobalInterning*:ModuleRootGC*:ModuleRegistration*' \
  2>&1 | tee $S/tsan-after.txt
diff <(grep -A3 "WARNING: ThreadSanitizer" $S/tsan-baseline.txt | grep -oP '#0 \S+' | sort -u) \
     <(grep -A3 "WARNING: ThreadSanitizer" $S/tsan-after.txt    | grep -oP '#0 \S+' | sort -u)
```
Done when the set of distinct stack tops in `tsan-after.txt` contains no entry in `SymbolTable`, `ModuleRootTable`, `ProtoSpace::addModuleRoot` or `ProtoSpace::registerModule` that is not in the baseline. protoCore has pre-existing benign races; the requirement is **no new** race on the paths this phase touches.

- [ ] **Step 3: AddressSanitizer over the whole suite**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
cmake --build $P/build_p3_asan -j4
ctest --test-dir $P/build_p3_asan --output-on-failure 2>&1 | tail -30 | tee $S/asan-after.txt
```
Done when `asan-after.txt` reports no `ERROR: AddressSanitizer`. This is the run that would have caught a retained `delete symbolTable`.

- [ ] **Step 4: Measure the pause, and self-report it**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
PROTOCORE_GC_PROFILE=1 $P/build_p3/test/proto_tests \
  --gtest_filter='GCStress*:ModuleRootGC*' 2>&1 | tee $S/pause-after.txt
grep -i "phase2\|pause" $S/pause-after.txt | tail -20
perf stat -r 3 $P/build_p3/test/proto_tests \
  --gtest_filter='ModuleRootGC.CaptureUnderStopTheWorldIsConstant' 2>&1 | tail -20
```
Done when `pause-after.txt` exists, the Phase-2 figure is recorded next to `pause-before.txt`'s, and the step's report states the module count the measurement ran at, the observed Phase-2 microseconds before and after, and the `perf stat -r 3` cycle counts. **The measurement must print what work it did**; a run that produced no Phase-2 line is a failed measurement, not a pass. The tests of Task 9 Step 2 remain the gate — this step is evidence for the documentation, not a threshold.

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add test/GlobalInterningTests.cpp
git commit  # "test(interning): concurrent cross-space interning agrees on one pointer (P3)"
```

---

### Task 12: Every runtime — the census, the three inverted assertions, and the clean rebuilds

**Files:** `protoST/tests/unit/test_cross_runtime_provider.cpp`, `protoST/src/modules/STModuleProvider.cpp`; `protoScala/tests/unit/protost_interop.cpp`, `protoScala/tests/conformance/25-interop/stand-in-provider-long-keyword.scala`, `protoScala/docs/INTEROP.md`, `protoScala/docs/DESIGN.md`. Output: one verification file per runtime.

**The census, taken 2026-09-24** across the five runtimes, excluding build directories and test262 data. `ProtoString::createSymbol`'s only two overloads, verbatim from `headers/protoCore.h:920-924`:

```cpp
        /** Creates or retrieves an interned symbol for the given UTF-8 string. Symbols with the same
         *  content share a unique pointer identity. Strong symbols (created here) are never GC-collected. */
        static const ProtoString* createSymbol(ProtoContext* context, const char* zeroTerminatedUtf8);
        static const ProtoString* createSymbol(ProtoContext* context, const std::string& s);
```

Both take `ProtoContext*` as a **mandatory** first parameter; there is no context-free overload and **no call site in any repository passes `nullptr`**. The context is used only as `ctx->space->symbolTable` plus as a read context for rope traversal (`core/ProtoString.cpp:1356`, `:1380`).

| Repository | `createSymbol` call lines | in `src/` | in tests/bench |
|---|---|---|---|
| protoPython | **876** | 875 | 1 |
| protoST | **112** | 87 | 26 (25 unit) |
| protoJS | **93** | 91 | 2 |
| protoScala | **67** | 40 | 27 |
| protoClojure | **64** | 58 | 6 |
| **five-runtime total** | **1212** | 1151 | 62 |
| protoCore (reference) | 81 | 10 outside `test/` + `performance/` | 71 |

Top files: protoPython `src/library/OsModule.cpp` 151, `ReModule.cpp` 106, `SysModule.cpp` 100, `IOModule.cpp` 76, `MathModule.cpp` 67, `ThreadModule.cpp` 62; protoST `src/runtime/STRuntime.cpp` 32, `ExecutionEngine.cpp` 21, `src/primitives/object_prims.cpp` 17; protoJS `src/runtime/ProtoInterpreter.cpp` 24, `src/ObjectPrototype.cpp` 17, `src/modules/http/HTTPModule.cpp` 15; protoScala `src/runtime/Runtime.cpp` 9, `src/runtime/Primitives.cpp` 7, `src/repl/Session.cpp` 7; protoClojure `src/repl/Repl.cpp` 20, `src/main.cpp` 17, `src/runtime/Primitives.cpp` 16.

Kinds of site, and what each means for this phase:

| Kind | Examples | Effect of global interning |
|---|---|---|
| (a) bootstrap / well-known-symbol caches, initialised once | `protoScala/src/runtime/Runtime.cpp:55`; `protoST/src/runtime/Bootstrap.cpp:171`; `protoJS/src/JSSymbols.cpp:25` (the `DEFINE_SYMBOL` macro, 60+ names); `protoCore/core/ProtoSpace.cpp:1253` | **No change needed.** They become correct across spaces instead of correct-by-luck. |
| (b) per-call hot-path interning of attribute names — the dominant kind | `protoPython/src/library/OsModule.cpp:58` (the identical expression repeats 151 times in that file); `protoScala/src/runtime/ExecutionEngine.cpp:664` (per field access); `protoST/src/runtime/STRuntime.cpp:121` (per message send) | **No change needed.** Lookup cost is unchanged; only the scope of canonicality widens. |
| (c) interning user-supplied strings at parse / compile / link time | `protoClojure/src/reader/Reader.cpp:90` (every symbol token read from source; its comment at `:86` says "interned for the lifetime of the ProtoSpace" and must be corrected to "of the process"); `protoST/src/runtime/BytecodeModule.cpp:87`; `protoScala/src/compiler/BytecodeModule.cpp:271`; `protoJS/src/runtime/ProtoInterpreter.cpp:430` | **No change needed, but note the unbounded-input shape:** these intern attacker- or author-controlled text into a table that now never resets. It was already perennial per space; it is now perennial per process. Worth a follow-up bound, not a P3 change. |
| (d) test code | `protoCore/test/SymbolInternTests.cpp:58`; `protoST/tests/unit/test_cross_runtime_provider.cpp:89`; `protoScala/tests/unit/protost_interop.cpp:159` | **Three files need changes** — Step 2 and Task 3 Step 5. |
| (e) `static` / `thread_local` / `call_once` caches holding the returned pointer | `protoJS/src/JSSymbols.cpp:25`, `DatePrototype.cpp:243`, `EventLoopBindings.cpp:23`, `ProtoCoreNativeBindings.cpp:167`, `ArrayElementsStorage.h:145`; `protoST/src/primitives/exception_prims.cpp:83,89,95,101`; `protoPython/src/library/PythonEnvironment.cpp:93` (`g_internPool`) | **No change needed; every one becomes sound.** Each was a latent bug whenever a process created more than one space. This is where a regression would surface first. |

**Nothing else in the family needs to change.** `internString` exists only in protoCore and is dead (`core/ProtoSpace.cpp:468`). `newSymbol`/`asSymbol` exist nowhere. protoJS's six `getSymbol` hits are `DynamicLibraryLoader::getSymbol` (dlsym) and `JSSymbols::getSymbolSpecies`, not interning.

- [ ] **Step 1: Re-run the census and reconcile**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
for r in protoPython protoJS protoST protoClojure protoScala protoCore; do
  printf '=== %s ===\n' "$r"
  rg --no-heading -c 'ProtoString::createSymbol\s*\(' /home/gamarino/Documentos/proyectos/$r \
     -g '*.cpp' -g '*.h' -g '*.hpp' \
     -g '!build/' -g '!build_*/' -g '!build-*/' -g '!cmake-build-*/' -g '!test262/' -g '!deps/' \
     2>/dev/null | sort -t: -k2 -rn
done | tee $S/census.txt
```
Done when the per-repo counts are within a few of the table above and every material difference is explained in the step's report. The counts move as the runtimes do; a large drift means a sibling agent has been working and Task 1's gate should be re-checked.

- [ ] **Step 2: Invert the three assertions that assert the old behaviour (D13)**

**Ask the maintainer before touching protoST or protoScala.** These three are the coverage that found the bug; **inverting is required, deleting is forbidden.**

1. `protoST/tests/unit/test_cross_runtime_provider.cpp:93-97` — `REQUIRE(counterKey != counterKeyInST);` becomes `REQUIRE(counterKey == counterKeyInST);`, and the comment *"The same name interned in protoST's space is a DIFFERENT pointer — that is the whole reason the namespace has to be rebuilt"* becomes: P3 made it one pointer, and the facade is **still** required because it re-parents to the caller's `objectPrototype` and **prototypes remain per-space** (D9).
2. `protoScala/tests/unit/protost_interop.cpp:145-163` — the negative assertion at `:163` still holds; the reasoning comment at `:150-152` becomes false and is rewritten to the same effect.
3. `protoScala/tests/conformance/25-interop/stand-in-provider-long-keyword.scala` — its entire purpose is the >6-byte-name-crosses-space trap, so after P3 it would **start passing for the wrong reason**, silently retiring its coverage. Re-point it at what still fails after P3 — a prototype-chain mismatch across spaces — or retire it explicitly with a note naming P3 as the reason. **Do not leave it to pass by accident.**

Also, per D9, correct the comment at `protoST/src/modules/STModuleProvider.cpp:107-116`, and rewrite the "Never cache interned symbols in function-local statics; symbols are per space" rule in `protoScala/docs/INTEROP.md:143,175-176` and `docs/DESIGN.md:907` — that rule is **reversed** by this phase, and five doc sets would otherwise contradict the kernel. Correct `protoClojure/src/reader/Reader.cpp:86`'s "for the lifetime of the ProtoSpace" to "of the process".

Done when each of the three is changed as described, each runtime still builds, and the step's report quotes the new assertion and the new comment for each.

- [ ] **Step 3: protoCore, from clean**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
rm -rf $P/build_p3_final && cmake -S $P -B $P/build_p3_final -DCMAKE_BUILD_TYPE=Release
cmake --build $P/build_p3_final -j4
ctest --test-dir $P/build_p3_final --output-on-failure 2>&1 | tail -30 | tee $S/protocore-after.txt
$P/build_p3_final/test/proto_tests --gtest_repeat=3 \
  --gtest_filter='GlobalInterning.*:SymbolIntern.*:ModuleIdentity.*:ModuleRootGC.*:ModuleRegistration.*' \
  2>&1 | tail -10 | tee -a $S/protocore-after.txt
```
Done when `protocore-after.txt` reports 438 plus the new cases, all passing, **and** the `--gtest_repeat=3` run passes — the order-independence check D8 requires.

- [ ] **Step 4: protoPython, protoST, protoClojure, protoScala, from clean**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
for r in protoPython protoST protoClojure protoScala; do
  R=/home/gamarino/Documentos/proyectos/$r
  rm -rf $R/build_release
  cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release && cmake --build $R/build_release -j4
  ctest --test-dir $R/build_release --output-on-failure 2>&1 | tail -30 \
    | tee $S/$(echo $r | tr 'A-Z' 'a-z')-after.txt
done
```
Done when protoPython reports **582/583** with only the pre-existing `protopy_import_site`; protoST **854/854**; protoClojure **391/391**; protoScala the number recorded in Task 1 Step 3. Use `build_release/` for protoPython, never `build-release/`, which is corrupted. protoClojure registers its threads and installs a shutdown hook so workers are joined before its `ProtoSpace` dies — confirm from the log that no run ended in a teardown abort. `Session::space_` is protoScala's first member so it is destroyed last; with D4 the space no longer frees the symbol table, so a `Session` destroyed before another is constructed is now safe rather than accidentally safe.

- [ ] **Step 5: protoJS — sequential, and ask first**

**Ask the maintainer before running this step.** protoJS builds with `-j>=2` hang DEV12, and a test262 sweep hangs it too.

```bash
R=/home/gamarino/Documentos/proyectos/protoJS; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p3-interning
rm -rf $R/build_release && cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build $R/build_release          # NO -j
ctest --test-dir $R/build_release --output-on-failure 2>&1 | tail -30 | tee $S/protojs-after.txt
# Read protoJS/CLAUDE.md for the exact runner invocation; do not guess it.
TEST262_CONCURRENCY=1 <runner> --paths 'built-ins/Object' 'built-ins/Reflect' 'built-ins/Proxy' \
  2>&1 | tail -30 | tee -a $S/protojs-after.txt
```
Done when `protojs-after.txt` reports ctest **34/34** and test262 `built-ins/{Object,Reflect,Proxy}` at **3619 passed** with no newly failing test. **protoJS is the runtime whose result matters most**: it interns attribute keys on hot paths, it holds five `static`/`thread_local` symbol caches whose soundness this phase changes, and it had a whole audit about uninterned keys.

- [ ] **Step 6: Report the matrix**

Done when the step's report is a table of six rows (protoCore, protoPython, protoJS, protoST, protoClojure, protoScala) each with the baseline, the observed number, and `MATCH` or the named failing tests. **Any `MATCH` claimed without a file in `$S/` to back it is not a result.**

---

### Task 13: Documentation

**Files:** modify `protoCore/docs/GarbageCollector.md`, `protoCore/docs/MODULE_DISCOVERY.md`.

- [ ] **Step 1: Phase 2's root list and "Not scanned"**

In `docs/GarbageCollector.md`, "Phase 2 — Root collection + mutable-shard snapshot", add between the current item 4 (tuple interner) and 5 (dirty segments):

```markdown
5. Records the **module-root snapshot**: `ModuleRootTable::captureForGC` stores
   each of its 8 shards' published entry count (O(shards)).  The module list is
   process-global and is a root — a module's contents are ordinary collectable
   objects, so an unfreed list would not keep them alive — but the entries are
   pushed in Phase 4 mark, NOT here.  Each entry records the `ProtoSpace` whose
   heap holds the module, and the Phase-4 walk visits only the collecting
   space's own entries: the table is global, the tracing is not.

   Until protoCore 2.2.0 this was a `for (mod : space->moduleRoots)
   addRootObj(mod)` loop **inside the pause**, holding `moduleRootsMutex`,
   O(modules) — and it was missing from the cost table below.  It was also only
   half of retention: `SharedModuleCache` was already process-global and was
   never scanned, so a module survived only because some space had pushed it
   into its own `moduleRoots`, and a cross-runtime import that called a provider
   directly reached neither mechanism.
```

Extend "Not scanned during Phase 2" so the `SymbolTable` bullet says the table is now **process-global**, one instance for the lifetime of the process, reached through `globalSymbolTable()`, and that `~ProtoSpace` does not free it.

- [ ] **Step 2: Phase 4's root list**

In "Phase 4 — Mark (concurrent with mutators)", before the tuple-interner paragraph:

```markdown
- **Module roots recorded by Phase 2** are pushed onto the worklist here,
  filtered to this space's own entries.  The walk is sound for the same four
  reasons the tuple interner's is: entries live in append-only chunks that never
  move; an entry's words are written before its count is published with release
  ordering; a module added after the capture is a young cell of the loading
  context and protected by it; and appending only ever extends past the captured
  count.
```

- [ ] **Step 3: The per-component cost table, and the claim under it**

Add the row:

```markdown
| **Module roots** | **< 1 μs** | constant | **O(8) published-count reads; entries walked in mark, not STW** |
```

The paragraph after the table asserts "every term in the table is either constant or scales with thread/stack quantities that the application controls". Add one sentence: that this **became** true in 2.2.0, and that before it the module-root loop was O(modules) inside the pause and absent from the table.

- [ ] **Step 4: `docs/MODULE_DISCOVERY.md`**

Add a section stating: the module list is process-global and a GC root; a module's identity is **provider GUID + logical path + version**, with the empty version reserved, permanently, for a module that declares none, so introducing a manifest later cannot re-alias a module loaded today; `SharedModuleCache` holds the identity and `ModuleRootTable` holds the reachability; the cache probe now happens **per resolution-chain entry**, so a module already loaded from a later entry no longer shadows an earlier one that can serve it; `ProtoSpace::addModuleRoot` and `ProtoSpace::registerModule` are how an embedder participates; and, historically, that `SharedModuleCache` alone was never a root.

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add docs/GarbageCollector.md docs/MODULE_DISCOVERY.md
git commit  # "docs(gc): global interning, module identity and the module root table (P3)"
```
Done when `grep -c "ModuleRootTable" docs/GarbageCollector.md` is at least 3, `grep -c "globalSymbolTable" docs/GarbageCollector.md` is at least 1, and `grep -c "provider" docs/MODULE_DISCOVERY.md` shows the identity section.

---

### Task 14: Version, changelog, and the platform spec

**Files:** modify `protoCore/CMakeLists.txt`, `protoCore/CHANGELOG.md`, `protoScala/docs/DECISIONS-LOG.md`; create `protoScala/docs/platform/GLOBAL-INTERNING-SPEC.md`.

- [ ] **Step 1: Version**

`protoCore/CMakeLists.txt` line 8: `VERSION 2.1.0` → `VERSION 2.2.0`. Leave `set(PROTOCORE_ABI_SOVERSION 2)` at line 67 **unchanged** (D7).

```bash
grep -n "VERSION 2.2.0\|PROTOCORE_ABI_SOVERSION" /home/gamarino/Documentos/proyectos/protoCore/CMakeLists.txt
```
Done when the first prints `2.2.0` and the second still prints `2`.

- [ ] **Step 2: Changelog**

Insert `## [2.2.0] - 2026-09-XX` **above** whatever `[Unreleased]` then contains — do not fold or move the existing Phase I packaging entries, because whether they become 2.1.0 or 2.1.1 is still an open maintainer question. Cover:

- **Added:** `globalSymbolTable()` / `globalSymbolCount()`; `ModuleIdentity`; `ModuleRootTable`; `ProtoSpace::addModuleRoot`, `moduleRootCount`, `registerModule`, `findModule`.
- **Changed:** interning is process-global and `ProtoSpace::symbolTable` is a borrowed pointer; `~ProtoSpace` no longer frees it; `SharedModuleCache` is keyed by provider + path + version, not by path alone; the cache probe moved inside the resolution-chain loop, so chain order is now respected; the O(modules) stop-the-world module loop is gone; `ProtoSpace::moduleRoots`/`moduleRootsMutex` retired but retained for layout; the perennial-path comment in `allocCell` corrected.
- **Fixed:** the same attribute name had a different address in each space unless it fitted in the pointer word, so a 5-byte name matched by accident while a 7-byte name missed with no error; a module loaded through a provider called directly was rooted only inside the providing runtime; `provider:st/counter_lib` and a local `counter_lib` were one module.
- **Unchanged, on purpose:** tuple interning stays per-space, with the test that keeps it so.
- State that the layout is byte-for-byte identical (Task 8 Step 6) and that a clean rebuild of every embedder is **nonetheless mandatory**, because a stale binary links and is silently wrong about symbol identity.

- [ ] **Step 3: The platform spec**

Create `protoScala/docs/platform/GLOBAL-INTERNING-SPEC.md` in `PMQ-SPEC.md`'s shape:

- `## 1. Problem` — per-space interning and the `INLINE_STRING_MAX_BYTES` accident, with the Track Y measurement; `SharedModuleCache` process-global and not a root; retention delivered per importing space; the cross-runtime import that reaches neither mechanism; path-alone identity aliasing silently.
- `## 2. Semantics` — one canonical pointer per spelling per process; a symbol is perennial; the module list is global, perennial and a root; a module's identity is provider + path + version with the empty version reserved for "declares none"; **the forward-compatibility rule** (an unversioned module's key is frozen, and a future resolver must reject an empty declared version); a module anchors its contents through its variables; and what is **not** made portable across spaces (prototypes, `PROTO_NONE`, the mutables tree, callbacks).
- `## 3. GC constraints` — four numbered constraints in PMQ-SPEC's style. Constraint 1: *no new stop-the-world work proportional to a collection's size; O(1) per pause.* Constraint 4: the perennial-versus-root distinction, spelled out per structure.
- `## 4. Tagged-pointer budget` — **none consumed**; no new pointer tag, no new `CellType`, so P2's figure of 35 free tags stands.
- `## 5. Tests` — every named test of Tasks 1, 3, 5, 6, 9, 10 and 11, each with the mutation that must turn it red; plus **the rule that every GC test goes through `forceCycles` (which calls `safepoint()`) and asserts a reclamation consistent with the garbage it created, never `reclaimed > 0`** — with the reason: forcing a cycle is not the same as submitting the young generation, and a helper that skipped `safepoint()` reclaimed 4-7 cells out of 205,120 while every assertion passed; plus the TSan and ASan runs and the protoPython GC-pressure run.
- `## 6. Rollout` — minor version bump; protoPython's root migration **before** the collector change; clean rebuild and test of every embedder against the baseline table; the three inverted assertions; then the follow-ups (the Track Y facade, deleting `moduleRoots` in a future major, bounding kind-(c) unbounded-input interning, protoPython's now-redundant `g_internPool` tier, protoJS's `ProxyBuiltin.cpp` uninterned cached keys, and `protoScala/src/runtime/ActorPrimitives.cpp.bak`).
- `## 7. Decisions (P3)` — D1-D13 as a table with a "Taken by" column: **D11 is `maintainer, 2026-09-24`**; the rest are *agent recommendation, pending maintainer review* until ruled. Plus the "Recorded, not asked" list from Task 0.

- [ ] **Step 4: `protoScala/docs/DECISIONS-LOG.md`**

Append, dated 2026-09-24:
- `| 2026-09-24 | Interning becomes process-global; the module list becomes a GC root | maintainer | GLOBAL-INTERNING-SPEC |`
- `| 2026-09-24 | A module's identity in the global list is provider + path + version, not path alone | maintainer | GLOBAL-INTERNING-SPEC §2, plan D11 |`
- one row per remaining decision with "Taken by" = *agent recommendation, pending maintainer review*.

Leave the "Still open" section intact.

- [ ] **Step 5: Commit, in two repositories**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore
git add CMakeLists.txt CHANGELOG.md
git commit  # "chore(release): protoCore 2.2.0 — global interning, module roots, module identity"

cd /home/gamarino/Documentos/proyectos/protoScala
git add docs/platform/GLOBAL-INTERNING-SPEC.md docs/DECISIONS-LOG.md docs/INTEROP.md docs/DESIGN.md
git commit  # "docs(platform): GLOBAL-INTERNING-SPEC and the P3 decisions"
```
Done when every touched repository is clean and none was pushed.

---

### Task 15: Hand-off

- [ ] **Step 1: Write the hand-off**

Done when the report states, each in one line: every branch and its commits, in all four touched repositories; the six-row verification matrix from Task 12 Step 6 with the file backing each row; the version (2.2.0) and that `SOVERSION` stayed 2, with the empty layout `diff` as the evidence; **every mutation observed red, by test name** (six for the module half, four for the symbol half, one for tuples, one for concurrency); the `forceCycles` calibration figures, including the ratio the unsubmitted case produces; the before/after Phase-2 pause figures with the module count they were measured at; which of D1-D13 the maintainer ruled and which remain recommendations; the behaviour change in resolution-chain order and whether any runtime depended on the old shadowing; and, separately and explicitly, **what a maintainer must rebuild from clean before trusting any binary** — all five runtimes, protoJS with no `-j` and sequential test262.

- [ ] **Step 2: Do not merge**

The branches stay unmerged until the maintainer has ruled on Task 0 and read the hand-off. `ProtoMPSCQueue` sat unmerged for a day over exactly the pause-cost question this plan answers in D6; that was the right outcome, not a delay.

---

## Self-review against the ruling (done while writing; gaps fixed inline)

- **Both ruled changes appear in named tasks.** Global interning: Task 2 (mechanism), Task 3 (claims). The module list as a root: Task 4 (structure), Task 5 (identity), Task 6 (registration), Task 7 (the gating migration), Task 8 (collector), Task 9 (claims).
- **The maintainer's second ruling — identity is provider + path + version — is recorded as ruled, in D11, with the representation, the empty-version rule and the forward-compatibility argument**, implemented in Task 5, and tested by `ModuleIdentity.TheUnversionedKeyIsFrozen`. The distribution model that motivates it is recorded in one sentence and deliberately not planned.
- **Every affected runtime appears in a named task.** protoPython has a task of its own (Task 7, the gate) plus Task 12 Step 4; protoST, protoClojure and protoScala are in Task 12 Steps 2 and 4, protoScala also in Task 6 Step 4; protoJS in Task 12 Step 5 with its own build discipline.
- **The pause-cost constraint is addressed explicitly, not assumed.** D6 tabulates the pause before and after, names the mechanism, and Task 9 Step 2 gates it with two deterministic tests and two mutations. The finding that the *existing* module loop is O(modules) inside the pause and missing from the documented cost table is recorded and fixed in Task 13 Step 3. **D12 names the one thing that blocks it** — protoPython's use of `moduleRoots` as a removable root set — and Task 8 is explicitly sequenced after Task 7 because of it.
- **The GC-test-rigour finding is a global constraint, not a footnote.** Every GC test goes through one `forceCycles` that calls `safepoint()` and returns `{cycles, reclaimed, created}`, asserts through `ASSERT_CYCLES_DID_REAL_WORK` against the garbage it created, self-reports its three numbers, and has its threshold calibrated in Task 3 Step 1 against the measured unsubmitted-case ratio. No test in this plan asserts `reclaimed > 0`.
- **Every GC claim has a mutation and a named test.** Perennial symbols → `SymbolsSurviveManyCollectionCycles`. Teardown → `SymbolSurvivesTheSpaceThatInternedIt` (ASan). Allocation-free cross-space reads → `CrossSpaceLookupAllocatesNothing`. Shared literals → `CachedLiteralsAreSharedAcrossSpaces`. No per-space re-interning → `ASecondSpaceInternsNothingItsPredecessorAlreadyDid`, `ResidencyDoesNotGrowWithSpaceCount`. Root-ness → `ModuleContentsSurviveWhenOnlyTheRootHoldsThem`, with **two** mutations, the second making a module cell perennial-but-unrooted — the direct test of "never freed ≠ is a root". Registration → `ARegisteredModuleIsRootedInTheRegisteringSpace`, `TheSameIdentityYieldsTheSameModule`. Pause cost → `CaptureUnderStopTheWorldIsConstant`, `TheWalkNeverRunsInsideThePause`. Owner filtering → `ACollectorTracesOnlyItsOwnSpacesModules`. Identity → four `ModuleIdentity.*` cases plus the chain-order case. Tuples → `TupleInternerStaysPerSpace`. Concurrency → `ConcurrentInterningAcrossSpacesAgreesOnOnePointer`.
- **The motivating cross-space test is present and ordered correctly.** Task 1 Step 4 is written and committed **before** any change, and its done-when requires the 7-byte cases red and the 5-byte case green on the unmodified tree. If they are not, the task stops.
- **The three tests that assert the opposite of this phase are named and inverted, not deleted** (D13, Task 12 Step 2), including the conformance test that would otherwise start passing for the wrong reason and silently retire its own coverage.
- **Types and signatures are spelled identically where consumed.** `SymbolTable& globalSymbolTable()` / `unsigned long globalSymbolCount()`: declared Task 2 Step 2, defined Step 3, used Task 3 Steps 4-5. `ModuleIdentity` with `unversioned`, `getProviderGUID`, `getLogicalPath`, `getVersion`, `asKey`, `operator==`: declared Task 5 Step 1, defined Step 2, used in Task 5 Steps 3-5, Task 6 Steps 2-4. `ModuleRootTable` with `SHARD_COUNT`, `CHUNK_SIZE`, `Entry{module, owner}`, `add(const ProtoObject*, const ProtoSpace*)`, `captureForGC()`, `forEachCaptured(const ProtoSpace*, void*, void(*)(void*, const ProtoObject*))`, `size()`, `lastCaptureShardReads()`, `stwVisitViolations()`, `resetDiagnostics()`: declared Task 4 Step 1, defined Step 2, called in Task 8 Steps 3-4 and Task 9 Steps 2-3 with the same argument order. `ProtoSpace::addModuleRoot`, `moduleRootCount`, `registerModule`, `findModule`: declared Task 6 Step 1, defined Step 2, used in Tasks 6 Step 3, 8 Step 5 and 9 Steps 1-3. `sharedModuleCacheGet/Insert(const ModuleIdentity&, …)`: changed Task 5 Step 3, used Task 5 Step 4 and Task 6 Step 2.
- **`ProtoRootSet`'s real method names are used.** `add` / `remove` / `resolve` / `size` / `forEachRoot` (`headers/protoCore.h:1882-1911`). Task 7 says so explicitly, because the P2 plan's test snippets call `addRoot`/`removeRoot`, which **do not exist** — copying that spelling would not compile.
- **Four interface checks are flagged rather than guessed.** Task 8 Step 4 must use the neighbouring `addRootObj`'s own `ProtoObject*`→`Cell*` conversion, not an invented `Cell::fromObject`. Task 10 Step 2 must use `test/test_tuple.cpp`'s own two-element tuple builder. Task 11 Step 1 must copy `test/SwarmTests.cpp`'s `newThread` idiom. Task 5 Step 5's chain-order case must follow `test/test_module_discovery.cpp`'s existing fixture. Task 7 Step 5 must use protoPython's own memory-limit env-var spelling from its `CLAUDE.md`.
- **No placeholders.** No "TBD", no "add appropriate error handling", no "similar to Task N". The four deliberately unwritten bodies — Task 6 Step 3's first test, Task 10 Step 2's tuple construction, Task 11 Step 1's thread loop, and Task 5 Step 5's chain-order case — each name the existing file to copy the idiom from and state exactly what the body must assert. Each is an interface check, not a deferred decision.
