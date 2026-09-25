# Platform spec: global interning and the module list as a GC root (protoCore)

> **Status:** implemented (protoCore 2.2.0, branch `feature/global-interning-p3`,
> 2026-09-25). Maintainer ruling of 2026-09-24: *"hacer la internación global y la
> lista de módulos como raíz"*, and, separately, *a module's identity in that list
> is provider + path + version*. This is roadmap Phase P3. It is a kernel change
> with five runtimes downstream of it, so every embedder is rebuilt from clean and
> compared against its stated baseline (§6).

## 1. Problem

Two silent failures, both measured in the tree rather than inferred.

### 1.1 Interning was half-global, and the half that failed was silent

An attribute key **is the address of an interned symbol**. `createSymbol` reached
its table through `ctx->space->symbolTable`, and every runtime in the family owns
its own `ProtoSpace`:

| Runtime | Where its `ProtoSpace` lives |
|---|---|
| protoScala | `src/repl/Session.h` — `proto::ProtoSpace space_;` (first member, destroyed last) |
| protoST | `src/runtime/STRuntime.cpp` — `STRuntime::Impl::space` |
| protoPython | `src/library/PythonEnvironment.cpp` — `static proto::ProtoSpace s_processSpace`, singleton enforced |
| protoJS | `src/JSContext.h` |
| protoClojure | `src/main.cpp` and `src/repl/Repl.cpp` |

Two spaces therefore disagreed about the key for the same name — **except** that
protoCore embeds a short ASCII string in the pointer word itself
(`INLINE_STRING_MAX_BYTES == 6`) and `createSymbol` short-circuits to an inline
string for those, which is bit-identical in every space. So the guarantee was
**half-global with silent partial success**, which is the worst available state.

protoST's own source recorded the trap verbatim, measured 2026-09-24 during
Track Y:

> …a name the caller interns in its own space is a DIFFERENT pointer from the one
> protoST stored the binding under — unless the name is short enough for protoCore
> to embed it in the pointer word, in which case the two agree by accident. That
> accident is the trap: `value` (5 bytes) would resolve and `Counter` (7 bytes)
> would silently miss.

A 7-byte name missed with **no error at all**: `getAttribute` returned
`PROTO_NONE`, which is also a legitimate value.

**Measured before the change** (`protoCore/test/GlobalInterningTests.cpp`, commit
`874d82dc`, three cases on the unmodified tree):

```
[  OK    ] GlobalInterning.ShortNameMatchedAcrossSpacesEvenBeforeP3
[ FAILED ] GlobalInterning.LongNameIsOnePointerAcrossSpaces
              sa = 0x653ced648016   sb = 0x653ced648216
[ FAILED ] GlobalInterning.AttributeWrittenInOneSpaceIsReadableFromAnother
              got = 0x141 (PROTO_NONE) vs PROTO_NONE
```

The 5-byte case passed and the 7-byte case failed, on the same tree, in the same
process. That is the bug, pinned.

### 1.2 The module list was not the root the ruling describes

- **`SharedModuleCache` was process-global and was *not* a GC root.**
  `core/ModuleCache.cpp` held a function-local-static `std::map<std::string, const
  ProtoObject*>` that outlived every space and held raw module pointers no
  collector ever read.
- **Retention came from somewhere else, and it was per importing space.**
  `getImportModuleImpl` pushed the module into the **calling** space's
  `moduleRoots`, and GC Phase 2 iterated that vector under stop-the-world. So "a
  global list" held, but "and therefore perennial" was delivered once per
  importing space.
- **A prefixed cross-runtime import reached neither mechanism.** protoScala's
  `Session::loadForeign` called `provider->tryLoad(logicalPath, &ctx)` directly,
  so nothing entered `SharedModuleCache` and nothing entered any `moduleRoots`.
  Retention rested on two per-runtime roots inside protoST. Destroying the
  `STRuntime` while an importer still held its values dropped the only anchor;
  the tests survived only because construction order happened to make the runtime
  destroyed last, and nothing enforced that.
- **The identity was wrong and aliased silently.** The cache was keyed by **path
  alone**, provider prefix stripped, so `import st.counter_lib` and a local
  `counter_lib.scala` collapsed into **one** module with the first load winning
  for both — the same wrong-answer-no-error class as the 6-versus-7-byte bug.

## 2. Semantics

### 2.1 Names

- **One canonical pointer per spelling per PROCESS.** `ProtoString::createSymbol`
  returns the same address for the same bytes in every `ProtoSpace` of the
  process. Reached through `globalSymbolTable()`, one leaked function-local
  static; `ProtoSpace::symbolTable` is a **borrowed** pointer to it and
  `~ProtoSpace` does **not** free it.
- **A symbol is perennial.** Its cells are allocated with a null `ProtoContext`,
  which is an ordinary `posix_memalign` enrolled in no thread freelist and no
  context young chain. No cycle can reclaim one. `addCell2Context` chains it to no
  young generation because its chaining is guarded by `if (this)`.
- **Residency grows with distinct spellings, not with space count.** Measured: 4 KB
  attributable to interning across five spaces × 2000 names, against 10,004 KB
  with a per-space table.
- **Static and `thread_local` caches of a `createSymbol` result are now sound.**
  They were latent bugs whenever a process created more than one space:
  `protoJS/src/JSSymbols.cpp` (the `DEFINE_SYMBOL` macro, 60+ names),
  `DatePrototype.cpp`, `EventLoopBindings.cpp`, `ProtoCoreNativeBindings.cpp`,
  `ArrayElementsStorage.h` (an `unordered_map` keyed on raw symbol pointers),
  `protoST/src/primitives/exception_prims.cpp` (four `call_once` caches capturing
  the first `ctx`).

### 2.2 Modules

- **The module list is process-global, perennial and a GC ROOT.**
  `ModuleRootTable` (`core/ModuleRoots.cpp`): 8 shards, append-only chunks that
  never move, a per-shard `published` count. Entries are never removed, because a
  loaded module never unloads. A module anchors its contents through its
  variables.
- **Each entry carries the `const ProtoSpace* owner`** whose heap holds the
  module, and the mark walk visits only the collecting space's own entries. The
  **table** is global; the **tracing** is not. A collector never traverses another
  space's heap on account of this table.
- **A module's identity is provider GUID + logical path + version**, rendered as
  one canonical string with `\x1F` (ASCII unit separator) between the components.
  `\x1F` cannot occur in a GUID, in a POSIX or Windows path, or in any version
  syntax — unlike `/`, which is in every path, and `:`, which is in the
  `provider:` spec and in Windows drive letters. The provider component is the
  **GUID**, never the alias: an alias is a user-facing nickname that can be
  re-pointed at a different provider.
- **The forward-compatibility rule.** A module that declares no version has the
  **empty** version, and that is a permanent, first-class identity value meaning
  *"this module declares no version"*. It is **not** a wildcard and **not** a
  synonym for any declared version. There is no module manifest yet; fixing the
  key's *shape* now is what guarantees that introducing one later cannot re-alias
  any module that exists today, because an unversioned key is byte-identical
  before and after. **A future resolver MUST reject an empty declared version**,
  because `""` is reserved. `"0.0.0"`, `"unversioned"` and `"latest"` were all
  rejected as the reserved value, because each is a string a future manifest could
  legitimately declare.
- **`SharedModuleCache` holds the identity; `ModuleRootTable` holds the
  reachability.** They are two structures with two jobs.
- **Resolution-chain order is now respected.** Because the provider is not known
  until a chain entry is selected, the cache probe moved **inside** the chain
  loop, one probe per entry. A module already loaded from a **later** chain entry
  therefore no longer shadows an **earlier** entry that can serve the same path.
  That is a behaviour change and it is tested explicitly.
- **An embedder participates through** `ProtoSpace::addModuleRoot`,
  `ProtoSpace::registerModule`, `ProtoSpace::findModule` and
  `ProtoSpace::moduleRootCount`.

### 2.3 What is NOT made portable across spaces

Global interning fixes attribute **keys**. It does not make objects portable.
**Prototypes remain per space** — `objectPrototype` and its thirty siblings are
per-space cells, and a space must root its own. `PROTO_NONE`, the mutables tree
and every per-space callback also remain per space. This is why protoST's
`buildCallerFacade` survives P3: its key rebuild is now redundant, but its
re-parenting to `callerCtx->space->objectPrototype` is not. Deleting it would
trade a silent attribute miss for a silent prototype mismatch.

### 2.4 Tuple interning stays PER SPACE — a deviation from a literal ruling

The ruling said "all interning should be global". Tuple interning is deliberately
**not** made global, and this is recorded as an explicit deviation with its safety
argument, taken by the implementing agent and **pending maintainer review**.

**The rule:** intern globally what is keyed by **content**; keep per-space what is
keyed by **address**.

- A symbol's key is its **bytes**, which are space-independent, so a global table
  produces cross-space identity — which is the purpose the ruling serves.
- `TupleInterner`'s key is the **slot pointers**: `hashSlots` mixes the element
  addresses and `find` confirms with `memcmp` over the slot array. Addresses are
  *not* space-independent, so a global tuple table would produce **no cross-space
  identity at all** for the ordinary case. It would not serve the ruling's
  purpose.
- **The one case where it *would* alias is a hazard, and it is new since P3.**
  Symbols are now the same pointers in every space, so a tuple of symbols built in
  space A and in space B has identical slots. A global table would return **one**
  node, whose cells live in whichever space created it first: one collector
  tracing another's heap, and elements kept alive by a foreign collector. New
  cross-space coupling for zero benefit.
- **It is not a use-after-free today only by accident.** `~ProtoSpace` never frees
  the `posix_memalign` blocks that back its cells, so a global tuple table holding
  entries into a dead space would read leaked-but-mapped memory rather than
  crashing. A design that is safe because of a leak is not a design.
- Making it global anyway would need three new mechanisms (an `owner` per entry,
  owner-filtered marking, and a `purgeSpace` called from `~ProtoSpace`) to obtain
  an identity the key cannot express.

`GlobalInterning.TupleInternerStaysPerSpace` exists to stop a future reader from
"completing" the change, and the decision is recorded in `TupleInterner`'s own doc
block.

## 3. GC constraints (binding)

**Constraint 1 — no new stop-the-world work proportional to a collection's size.**
The collector stops user threads only to take stack roots, the mutables-tree root
and global-structure roots. Every global root set must cost **O(1) per pause**,
not O(modules) and not O(symbols). This is the rule that governed `ProtoMap`
(P1 D5) and `ProtoMPSCQueue` (P2 D1).

- `SymbolTable`: **0**, before and after. Symbols are perennial, so the collector
  never reads the table. Global-ness changes the table's *residency*, not whether
  it is scanned.
- Module list: **O(8)** — `globalModuleRootTable().captureForGC()` reads each
  shard's `published` counter and dereferences no entry. The captured entries are
  pushed onto the mark worklist in **Phase 4**, after `stwFlag.store(false)`.
- `TupleInterner`: O(64) counter reads, unchanged.

**This phase makes the pause strictly cheaper.** It *removes* existing linear
pause work: `for (mod : space->moduleRoots) addRootObj(mod)` ran inside the pause,
holding `moduleRootsMutex`, and was O(modules) — and it was **missing from
`docs/GarbageCollector.md`'s own per-component cost table**, so that document's
claim that "every term is either constant or scales with thread/stack quantities
the application controls" was not true before 2.2.0. It is now.

**Constraint 2 — the pause-cost gate is deterministic, not timed.** A wall-clock
pause assertion on this hardware is noise. Two counter tests gate the property;
the instrumented measurement is evidence for the documentation, not the threshold.

**Constraint 3 — the concurrent walk must be sound.** Four properties, the same
four that make the tuple interner's walk sound: entries live in append-only chunks
that never move; an entry's `module` and `owner` words are written *before*
`published` is stored with release ordering, so a walker that sees the count sees
the entry; a module appended after the capture is not visited this cycle but is a
young cell of the loading context and protected by it until the next cycle's
capture; and appending only ever extends past the captured count.

**Constraint 4 — "never freed" is NOT "is a GC root".** A perennial cell is not
swept, but it is also not *scanned*: the references it holds do not keep their
targets alive. Per structure:

| Structure | Perennial? | A root? | Why |
|---|---|---|---|
| `SymbolTable` entries | yes | **no, and needs none** | a symbol's references point only at its own perennial nodes |
| `ModuleRootTable` entries | yes | **yes, necessarily** | a module's contents are ordinary collectable objects in a space's heap |
| `TupleInterner` entries | yes | yes (per space) | a tuple references heap objects |

The comment in `ProtoContext::allocCell` that described the perennial path as
*"Absolute fall back (rare or error)"* was wrong and invited a future reader to
delete a branch two callers depend on by contract. It is corrected, and it now
states the perennial-versus-root distinction in place.

**Constraint 5 — a reclamation assertion must be consistent with the garbage
created.** Every GC test in this phase goes through one `forceCycles` helper that
calls `ProtoContext::safepoint()` and returns `{cycles, reclaimed, created}`, and
asserts through `ASSERT_CYCLES_DID_REAL_WORK` against `created`, **never**
against `reclaimed > 0`. The helper self-reports its three numbers on every run.
Forcing a cycle is not the same as submitting the young generation: Track Y
measured a helper that skipped `safepoint()` reclaiming 4–7 cells out of 205,120
while every assertion passed. Calibration for this phase's helper, measured:

| Variant | reclaimed / created | Verdict |
|---|---|---|
| as written | 280,000 / 425,000 = **0.66** | passes with 6.6× margin |
| safepoint() removed, child context kept | 280,000 / 425,000 = 0.66 | **unchanged** — the child context's destructor already submits the chain, so this mutation alone proves nothing. Recorded rather than papered over. |
| the real Track Y shape: allocate into the long-lived parent context, no safepoint | **0** / 425,000 = 0.0 | fails decisively; protoCore reports `last cycle reclaimed 0` and exits |

Threshold: `reclaimed > created / 10`. A `reclaimed > 0` assertion could not have
failed in Track Y's case; this one cannot pass in it.

## 4. Tagged-pointer budget

**None consumed.** No new pointer tag, no new `CellType`, no new `ProtoSpace`
field. P2's figure of 35 free tags stands. The `ProtoSpace` layout is
byte-for-byte identical — proved by an `offsetof` diff against the base commit
(§6.1).

## 5. Tests

Every claim below names the **mutation** that must turn the **named test** red. A
claim whose mutation was not observed red is not implemented. Red runs are
recorded in `.agent_scratch/p3/claim-mutations-symbols.txt` and
`claim-mutations-modules.txt`.

### 5.1 The motivating fixture (committed before any change)

| Test | Before P3 | After |
|---|---|---|
| `GlobalInterning.ShortNameMatchedAcrossSpacesEvenBeforeP3` | PASS (the accident) | PASS |
| `GlobalInterning.LongNameIsOnePointerAcrossSpaces` | **FAIL** | PASS |
| `GlobalInterning.AttributeWrittenInOneSpaceIsReadableFromAnother` | **FAIL** (`PROTO_NONE`) | PASS |

### 5.2 Names

| Test | Mutation that turns it red | Observed |
|---|---|---|
| `GlobalInterning.SymbolsSurviveManyCollectionCycles` | pass the caller's context, not `nullptr`, to `fromUTF8Bytes` in `createSymbol` and `normalizeForSymbol` | red: `readBack` returns `""` |
| `GlobalInterning.SymbolSurvivesTheSpaceThatInternedIt` | restore `delete symbolTable` in `~ProtoSpace` (ASan) | red: `heap-use-after-free` in `SymbolTable::lookupUTF8` |
| `GlobalInterning.CrossSpaceLookupAllocatesNothing` | make `lookupUTF8` build an intermediate protoCore string in the reading context | red: 2002 cells against 2 |
| `GlobalInterning.CachedLiteralsAreSharedAcrossSpaces` | revert the constructor to `new SymbolTable()` | red |
| `GlobalInterning.ASecondSpaceInternsNothingItsPredecessorAlreadyDid` | same | red |
| `GlobalInterning.ConcurrentInterningAcrossSpacesAgreesOnOnePointer` | drop the double-checked re-check inside `SymbolTable::intern`'s shard lock | red |
| `SymbolIntern.ResidencyDoesNotGrowWithSpaceCount` | same as the constructor mutation | red: 10,004 KB attributable, against 4 KB |

**Note on the second-space test.** It first passed under the constructor mutation,
vacuously: with per-space tables, `globalSymbolCount()` is unchanged because the
per-space tables stop populating the global one at all. A per-spelling pointer
equality assertion was added, and it is now red under that mutation. Recorded,
because a claim whose mutation stays green proves nothing.

**Order-independence (D8).** With one table, "is this spelling fresh?" is a
property of the process, not of a space, so it depends on test execution order and
on `--gtest_repeat`. Every spelling in `SymbolInternTests.cpp` now carries the test
invocation's own suffix, and `--gtest_repeat=3` over
`GlobalInterning.*:SymbolIntern.*` passes. The three resident-memory assertions
skip under AddressSanitizer, where an RSS bound measures the sanitizer rather than
the code; two of them pre-date P3 and were already red in an ASan build for that
reason.

### 5.3 Modules

| Test | Mutation that turns it red | Observed |
|---|---|---|
| `ModuleRootGC.ModuleContentsSurviveWhenOnlyTheRootHoldsThem` | (1) delete the `forEachCaptured` call from GC Phase 4 | red |
| | (2) build the module with a null context so its cell is perennial, and do not register it | red — the module cell survives, its contents do not |
| `ModuleRootGC.CaptureUnderStopTheWorldIsConstant` | restore the per-module loop in Phase 2 | red — `lastCaptureShardReads()` stays 0 |
| `ModuleRootGC.TheWalkNeverRunsInsideThePause` | move `forEachCaptured` from Phase 4 into Phase 2 | red — `stwVisitViolations()` goes positive |
| `ModuleRootGC.ACollectorTracesOnlyItsOwnSpacesModules` | drop the `owner == space` guard in `forEachCaptured` | red |
| `ModuleRegistration.ARegisteredModuleIsRootedInTheRegisteringSpace` | make `registerModule` insert into the cache without `addModuleRoot` | red |
| `ModuleRegistration.TheSameIdentityYieldsTheSameModule` | — | green |
| `ModuleDiscoveryTest.AnEarlierChainEntryIsNotShadowedByAnEarlierLoad` | probe the cache above the chain loop, keyed by the bare path | red — `fromEarly == fromLate` |
| `ModuleIdentity.ProviderIsPartOfTheIdentity` / `VersionIsPartOfTheIdentity` / `TheUnversionedKeyIsFrozen` / `TheSeparatorCannotBeForged` / `ComponentsRoundTrip` | — | green |

### 5.4 Tuples

`GlobalInterning.TupleInternerStaysPerSpace` asserts that the tuple of two
now-globally-interned symbols has a **different** `ProtoTuple` address in two
spaces, **and** that per-space interning still holds inside one space. The second
assertion is load-bearing: a mutation that broke per-space interning entirely
would otherwise pass the first. Mutation: make `TupleInterner` a process-global
singleton.

### 5.5 Sanitizers and the migration

- **ASan over protoCore's whole suite** — the run that would have caught a
  retained `delete symbolTable`.
- **TSan** on `Swarm*:GCStress*:ConcurrentMark*:SymbolIntern*:GlobalInterning*:
  ModuleRootGC*:ModuleRegistration*`, compared against a baseline taken on the
  unmodified tree. The requirement is **no new** race on the paths this phase
  touches; protoCore has pre-existing benign races. On this machine ThreadSanitizer
  aborts with `unexpected memory mapping` under the kernel's default ASLR entropy
  and must be run under `setarch -R`; raising `vm.mmap_rnd_bits` would need root
  and was not done.
- **protoPython's root migration is verified by ASan and a GC-pressure run**, not
  by its suite passing — its suite cannot reach the failure, because losing a GC
  root does not fail a test, it corrupts memory later.

## 6. Rollout

protoCore `2.1.0` → **`2.2.0`**, and `PROTOCORE_ABI_SOVERSION` **2 → 3**.

### 6.1 Why the soname moves even though the ABI is additive

The `ProtoSpace` layout is byte-for-byte identical — no field added, removed or
reordered; `symbolTable` keeps its type and slot; `moduleRoots` and
`moduleRootsMutex` are retained; the public additions are one class
(`ModuleIdentity`) and four non-virtual methods, which add no vtable and no
layout. An `offsetof` diff against the base commit is empty.

A minor version bump alone would have matched the `SameMajorVersion` package
config. It was rejected because of what makes this phase different from P1 and
P2: **a stale embedder binary here links successfully and merely returns wrong
answers.** There is no load-time error, no crash and no diagnostic — just a
`getAttribute` that returns `PROTO_NONE`, which is also a legitimate value. A
soname bump converts that into a load-time error. It costs one line in each of
the five runtimes' `CMakeLists.txt` today, and a great deal later. The plan
recommended keeping SOVERSION at 2; this spec records the decision to override
that recommendation, with the plan's own sentence as the deciding reason.

### 6.2 Order

1. **protoPython's root migration lands FIRST**, before the collector change.
   protoPython used `space->moduleRoots` as a general-purpose, removable,
   slot-replacing embedder root set. `ModuleRootTable` is append-only and is not a
   substitute. Deleting the stop-the-world loop before migrating protoPython would
   silently drop protoPython's GC roots — a failure its passing tests cannot
   reach.
2. Then the collector change: Phase 2 captures, Phase 4 walks, and the O(modules)
   loop is deleted.
3. Then every embedder is rebuilt **from clean** and compared against its
   baseline. "From clean" means a fresh build directory. protoJS builds with no
   `-j` at all and runs test262 sequentially; protoST's `ctest` runs with stdin at
   EOF.
4. Each runtime's fix is pushed as it lands green. **protoCore is pushed LAST**,
   only after all five runtimes have been rebuilt from clean against it and
   verified green: publishing a kernel ABI change before its consumers are known
   to work would publish a break.

### 6.3 Baselines

| Project | Baseline |
|---|---|
| protoCore | 438/438 ctest |
| protoPython | 582/583 — the lone `protopy_import_site` failure is pre-existing (a `.pth` in a sibling `venv/`) |
| protoJS | ctest 34/34, plus test262 `built-ins/{Object,Reflect,Proxy}` at 3619 passed with no newly failing test |
| protoST | 854/854 |
| protoClojure | 391/391 |
| protoScala | 1248/1248, plus one pre-existing failure under `PROTOCORE_HEAP_LIMIT_CELLS=20000` (`Mailbox.EightProducers…`) |

### 6.4 The three tests that asserted the opposite

They are the coverage that found the bug; **inverting is required, deleting is
forbidden**.

1. `protoST/tests/unit/test_cross_runtime_provider.cpp` —
   `REQUIRE(counterKey != counterKeyInST)` becomes `REQUIRE(counterKey ==
   counterKeyInST)`, and the comment now says that P3 made it one pointer and that
   the facade is still required for **prototypes**.
2. `protoScala/tests/unit/protost_interop.cpp` — the negative assertion still
   holds; the reasoning comment that said "symbols are interned per space" is
   false and is rewritten.
3. `protoScala/tests/conformance/25-interop/stand-in-provider-long-keyword.scala`
   — **the plan was wrong about this one, and reality is followed.** Its purpose is
   not the cross-space trap: it guards the `createSymbol`-versus-`fromUTF8String`
   convention for a keyword-parameter key *within one space*
   (`tests/unit/probe_provider.cpp`, `keywordNamed`). P3 does not change what it
   tests and does not make it vacuous. It was therefore neither re-pointed nor
   retired; instead its guard was **re-verified still live** by mutation, and its
   comment now states exactly what it guards after P3.

### 6.5 Follow-ups, deliberately not in this phase

- protoPython's `g_internPool` / `thread_local` two-tier intern layer in front of
  protoCore's is now redundant, including its guard for an "ephemeral
  (unit-test scope)" space. Collapsing it is out of scope; the guard should be
  revisited.
- `protoJS/src/ProxyBuiltin.cpp` builds function-local `static` caches from
  `ctx->fromUTF8String` + `asString` rather than `createSymbol` — an uninterned
  key in a cached slot. Not fixed by this phase.
- Kind-(c) interning of unbounded input (`protoClojure/src/reader/Reader.cpp`,
  each runtime's bytecode modules) interns author- or attacker-controlled text
  into a table that never resets. It was already perennial per space; it is now
  perennial per process. Worth a bound, not a P3 change.
- `ProtoSpace::moduleRoots` / `moduleRootsMutex` are retired in comment and held
  empty, retained only so the layout does not change. They can be deleted in a
  future major.
- `protoScala/src/runtime/ActorPrimitives.cpp.bak` holds six `createSymbol` calls
  and is excluded from the census only by its suffix. It should be deleted by
  whoever owns that repository.
- protoST's `buildCallerFacade` key rebuild is now redundant as a key rebuild and
  could be simplified once the prototype half is solved; the facade itself must
  stay.

## 7. Decisions (P3)

D11 is **ruled by the maintainer**. Every other row was taken by the implementing
agent and is **pending maintainer review**.

| Id | Decision | Taken by |
|---|---|---|
| D1 | Supplement, not replace: the global module table is the only new global root; everything else stays per-space. Each entry carries `const ProtoSpace* owner` and `forEachCaptured(space, …)` visits only that space's entries. A single global root set replacing per-space roots was rejected — it would kill the per-space prototype roots and make every collector traverse every other space's live graph. | agent, pending review |
| D2 | `globalSymbolTable()` is one leaked function-local static, and `ProtoSpace::symbolTable` points at it. All eleven existing `ctx->space->symbolTable` call sites are untouched, the layout is unchanged, and the mid-construction sentinel that six null checks in `ProtoObject.cpp` rely on still fires exactly when it used to. | agent, pending review |
| D3 | Locking unchanged. The discipline, now stated in the source: a `SymbolTable` shard mutex is a **strict leaf lock**, never ordered against `ProtoSpace::globalMutex`, and no Cell is allocated while it is held. With one table serving several spaces, breaking that would deadlock two collectors instead of one. The read path is allocation-free by construction, and a test pins it. | agent, pending review |
| D4 | `~ProtoSpace` stops freeing the symbol table, and only the symbol table. `delete tupleInterner`, `freeStringInternMap`, the context deletes, the root-set sweep and the `DirtySegment` drain all stay. This fixes a leak rather than creating one: `~SymbolTable` frees only the `Bucket` nodes, so each destroyed space already leaked its whole symbol set. | agent, pending review |
| D5 | **Tuple interning stays per-space** — a deviation from the literal ruling "all interning should be global", with the safety argument in §2.4. The ruling's purpose was cross-space identity for **names**; a tuple's key is slot *addresses*, so a global tuple table would deliver no cross-space identity in the ordinary case, and the one aliasing case would make one collector trace another's heap. | agent, **explicit deviation**, pending review |
| D6 | Pause cost stays O(1): symbols cost nothing, modules use the `TupleInterner` capture/walk split. Gated by two deterministic counter tests, not by a wall-clock assertion. | agent, pending review |
| D7 | protoCore `2.1.0` → `2.2.0` **and `PROTOCORE_ABI_SOVERSION` 2 → 3**, overriding the plan's recommendation to keep 2. See §6.1: a stale embedder here links and merely returns wrong answers, and only a soname bump turns that into a load-time error. | agent, **overrides the plan**, pending review |
| D8 | Symbols interned before a second space is constructed are found by it; `literalData` and its siblings become shared, and that is the fix working. Every test spelling is made unique per invocation, because freshness is now a property of the process. | agent, pending review |
| D9 | protoST's `buildCallerFacade` is **not** simplified: its key rebuild is redundant now, but it also re-parents to the caller's `objectPrototype`, and prototypes stay per space. Only its comment is corrected. | agent, pending review |
| D10 | Four additive `ProtoSpace` methods plus `ModuleIdentity` are public, so an embedder that resolves before loading can participate and `Session::loadForeign` can close the cross-runtime gap. | agent, pending review |
| D11 | **A module's identity in the global list is provider + path + version**, not path alone. Representation, the reserved empty version and the forward-compatibility rule in §2.2. | **maintainer, 2026-09-24** |
| D12 | protoPython is migrated to a `ProtoRootSet` **in this phase**, and the collector's stop-the-world module loop is retired only after that lands. Deferring to a P3b was rejected: the pause rule is binding and the kernel already violated it. | agent, pending review |
| D13 | The cross-runtime registration gap is closed with `registerModule`, and the three tests that assert the old behaviour are inverted, not deleted. The third turned out to test something else; see §6.4. | agent, pending review |
| D14 | `getImportModuleImpl`'s two wrapper-construction branches are unified **with** the `addParent(objectPrototype)` the cache-hit branch had and the fresh-load branch lacked. Before P3 the same call returned a wrapper with a different prototype chain depending on whether the module happened to be cached already; the merged path removes that nondeterminism. | agent, pending review |
| D15 | **`ModuleRootTable::purgeSpace`, called from `~ProtoSpace`, and `Entry::owner` made atomic.** NOT IN THE PLAN — found by a test during execution. The table is append-only, so an entry outlives the space it names, and the allocator can hand a LATER `ProtoSpace` the same address; that space's collector would then match the dead space's entries by owner and trace cells in a heap with no owner. It does not crash today only because `~ProtoSpace` never frees its cell blocks — the same "safe because of a leak" shape §2.4 rejects for tuples. This is exactly mechanism (iii) that D5 listed as a cost of going global for tuples, and the module case turned out to need it. `purgeSpace` tombstones in place (the chunks must not move, or the concurrent walk breaks), runs after the space's GC thread is joined, and is O(entries) at teardown, never at a pause. `owner` is atomic because a tombstone is written while another space's mark may be reading that word. `countOwnedBy` was added alongside, because `forEachCaptured` dereferences its `space` (for the `stwFlag` diagnostic) and the teardown test must ask about an address whose space is gone. Pinned by `ModuleRootGC.ADeadSpacesEntriesAreRetiredAtTeardown`; M11 turns it red with 64 entries still naming a destroyed space. | agent, pending review |
| D16 | **The three resident-memory assertions in `SymbolInternTests.cpp` skip under AddressSanitizer.** An RSS bound measures the sanitizer, not the code: the same 200k `createSymbol` calls that commit ~0 KB in Release commit 26 MB under ASan. Two of the three pre-date P3 and were already red in an ASan build for that reason, so this makes protoCore's ASan suite meaningful rather than papering over a P3 defect. The Release build still asserts them, and the residency claim's mutation is verified there. | agent, pending review |
| D17 | **Three P3 tests were strengthened after their mutation failed to turn them red**, and the fact is recorded rather than quietly fixed. (a) `SymbolsSurviveManyCollectionCycles` interned its symbol in the long-lived context, so the cells survived through that context's young chain; it now interns in a short-lived child context. (b) `ASecondSpaceInternsNothingItsPredecessorAlreadyDid` asserted only on `globalSymbolCount()`, which is unchanged under a per-space table because the per-space tables stop populating the global one at all; it now also asserts per-spelling pointer equality. (c) `ConcurrentInterningAcrossSpacesAgreesOnOnePointer` let its mutation through in 1 of 3 runs with one round; it now runs five. A mutation that is only sometimes caught is only sometimes a test. | agent, pending review |

### Recorded, not asked (facts the maintainer should see)

- **This phase removes linear pause work that already existed, and the documented
  pause profile had been understating the pause.** The module-root loop ran inside
  the stop-the-world window, held `moduleRootsMutex` there, was O(modules), and
  was absent from `docs/GarbageCollector.md`'s per-component cost table — so that
  document's claim that every term is constant or scales with quantities the
  application controls was **not true** before 2.2.0.
- **The cache-hit branch already rooted another space's module in this space.**
  Cross-space marking is pre-existing, not introduced here, and D1 preserves it.
- **`ProtoSpace::globalMutex` is `static`**, so two co-resident spaces already
  serialise their stop-the-world windows and their `getFreeCells` OS fallbacks on
  one process-wide recursive mutex. A family that now genuinely expects several
  spaces per process should know.
- **`ProtoSpace` teardown does not free its cell heap.** That leak is what keeps
  several cross-space hazards from being crashes today, including the one §2.4
  declines. A future teardown that *does* free the heap will need a `purgeSpace`
  for any global structure holding a cell of a dying space.
- **Global interning does not make objects portable across spaces.** It fixes
  attribute keys. §2.3.
- **protoPython's destructor was leaking GC roots.** Its ~150 hand-written
  `std::remove` calls on `moduleRoots` did not cover the thread-roots dict, the
  unbound-local sentinel, the interned symbols or any builtins version, so every
  destroyed `PythonEnvironment` left roots behind in a space that outlives it. One
  `destroyRootSet` takes all of them.
- **ThreadSanitizer does not run on this machine without `setarch -R`.** It aborts
  with `FATAL: ThreadSanitizer: unexpected memory mapping` under the kernel's
  default ASLR entropy. Disabling ASLR for the run is the workaround used;
  `vm.mmap_rnd_bits` would need root.
