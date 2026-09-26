# Phase P4 — The embedder conformance suite (protoCore) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **This plan is written, not executed.** A sibling agent is writing `docs/plans/2026-09-24-phase-p3-global-interning.md` in this same tree as this file is created. Nothing in protoCore or in any runtime may be touched until the maintainer says the family is quiet **and P3 has landed**. Task 1 Step 1 is the gate.

**Goal:** Implement the maintainer's instruction of 2026-09-25 — *"After the current work lands, add an audit phase for all embedders that checks protoCore's rules."* The audit is delivered as a **shared executable conformance suite that lives in protoCore and that every embedder runs as part of its own `ctest`**, plus a static checker every embedder runs against its own source, plus an explicitly-labelled human review checklist for the rules that cannot honestly be mechanised. The phase ends when all five runtimes run the suite, their first-run results are compared against the predictions recorded in this plan, and every one of the suite's own cases has been shown to fail under a named mutation.

---

## Motivation — why a suite and not a review

**Every deep bug found in this project over 2026-09-23/24 was a documented protoCore rule that an embedder simply did not follow, and not one of them failed loudly.**

The clearest case is protoST's **S15**. protoST's interpreter called `ProtoContext::safepoint()` **nowhere**. `safepoint()` is the only place in protoCore that hands a context's young chain to `dirtySegments` (`core/ProtoContext.cpp:363-389`), and GC Phase 2 records a young chain only as a *root handle* — "the cells of a context's young chain are not candidates of the cycle and are never marked for it" (`docs/GarbageCollector.md`, Phase 2, closing note). A chain that is never submitted is therefore live **by definition**: no cycle can ever consider it. protoST ran collection cycles for its entire history, reclaimed exactly **0** cells, grew to **2,748,398** cells, and **passed 833 tests** doing it.

The same session produced four more of the same shape:

| Bug | The rule that was omitted | How it announced itself |
|---|---|---|
| protoST S15 | the young generation must be submitted | it did not — **848** tests green (corrected 2026-09-25: this row quoted 833, which was the suite size at the earlier S13 fix; when S15 was measured the suite was 848/848, which is the figure the `MailboxCursor::adopt` row below already used. Source: protoST `2d5bc6f`, and protoCore `docs/FIELD-NOTES.md` case 1) |
| protoClojure idle actor worker on `queueCv_` | a registered thread that blocks does so inside `UnmanagedScope` | a hang, with no message, only under load |
| protoClojure `ActorMessage` payloads | no `ProtoObject*` across an allocation in a bare C++ local | intermittent wrong values |
| protoScala `Mailbox::push` | same rule — a CAS snapshot held across `appendLast` | nothing, until GC pressure |
| protoST `MailboxCursor::adopt` | same rule — `unique_ptr::reset` builds the replacement **before** destroying the old one, releasing a live pin | **unreachable until S15 was fixed**; 848 passing tests could not reach it |

That last row is the argument for this phase in one line. **The third bug of the class was invisible because the first bug of the class was still present.** A code review conducted on 2026-09-23 would have found neither, because the reviewer would have been reading a program in which the collector never ran.

**When the kernel's principles simplify this much, omitting one does not announce itself.** protoCore's value proposition is that the embedder does not manage memory, does not take a global lock and does not write barriers. The price is a handful of participation obligations, and the failure mode of skipping one is almost never a crash at the point of the mistake — it is unbounded RSS, a hang under contention, or a use-after-free weeks later.

### The verdict: a suite, with an honest split

**An audit finds today's violations; a suite prevents tomorrow's** — and, decisively, **a suite finds the violations a review cannot see**, because three of the five bugs above were only reachable with the collector actually reclaiming. A one-off review is rejected as the primary deliverable.

But a suite that claims to mechanise a judgement call is worse than one that admits the split, so this plan classifies every rule into one of three mechanisms and **says per rule which it is**:

- **S — static check.** A script over the embedder's source, with a per-site allowlist that carries a written justification. New unjustified sites are a hard failure; the raw count is not a gate.
- **R — runtime case.** A case in the installed conformance library, driven through an embedder-supplied adaptor, asserting a kernel-side invariant. Hard failure.
- **J — judgement.** A checklist item a human answers in a recorded review. There is no pass/fail; there is a signed answer with a citation. **Labelling a rule J is a result, not a gap.**

Two rules are also reclassified *upward* relative to the maintainer's framing, and one downward. Both changes are argued in Task 0 §D5 and §D6.

---

## The rule table — the spine of the phase

| # | Rule | Mechanism | Why omitting it is silent | Case / check id | Hard failure? |
|---|---|---|---|---|---|
| 1 | **The young generation must be submitted.** A context's young chain reaches the collector only through `ProtoContext::safepoint()` (`core/ProtoContext.cpp:363-389`) or the context's destruction. | **R** | An unsubmitted chain is live by construction. The heap grows; nothing errors; `gcCycleCount` still advances, so instrumentation that counts *cycles* looks healthy. | `gc.young_submitted` | yes |
| 2 | **Every registered protoCore thread must park.** A thread that blocks does so inside `ProtoContext::UnmanagedScope` (`headers/protoCore.h:1730-1744`). | **R** (+ S as a hint) | STW Phase 1 waits for `parkedThreads >= runningThreads` forever. No collection completes, so the symptom is a hang under load, not a failure. | `stw.quorum_completes` | yes |
| 3 | **No `ProtoObject*` may be held across an allocation only in a C++ local.** | **J**, with **R** stress + **S** for three named shapes | The window is one GC cycle wide. Without pressure it never opens; with pressure it is a use-after-free that ASan sees and a green suite does not. | `gc.host_stress` (R), `local_across_alloc` (S), checklist C3 (J) | R: yes · S: yes · J: recorded |
| 4 | **Symbols come from `ProtoString::createSymbol`, never `fromUTF8String`.** | **S**, split by destination, **+ R** (see §D11) | The severity depends on where the key goes, which §D11 establishes: `setAttribute` auto-interns and `getAttribute` falls back to content, so both are merely slow — but **`getOwnAttributeDirect` does neither and silently misses**, and it is the Phase-6 fast path protoPython and protoJS both use. On top of that, ≤ 6 ASCII bytes are inline and match by accident (`INLINE_STRING_MAX_BYTES`, `headers/proto_internal.h:300`), so short names work and long ones do not. | `symbol_key_source` (S), `symbol.trust_symbols_clean` (R) | error only for the fast path |
| 5 | **`ProtoTuple` is never used for transient data** — every tuple node is interned and perennial. | **R** (reclassified — see §D5) | Perennial cells are never swept, so the leak is invisible to every reclamation metric. It looks like a working program with a large heap. | `gc.transient_reclaimed` | yes |
| 6 | **`getAttribute` returns `PROTO_NONE`, not `nullptr`** — but the convention is **per function**. | **S**, per-function table (see §D6) | Comparing against the wrong sentinel dereferences `321UL`. Phase 3 found the segfault. | `attr_sentinel` | yes |
| 7 | **External memory obeys its contract** (`docs/MemoryModel.md` §5, `docs/GarbageCollector.md` §7). | **S** (finalizer present, finalizer body) + **R** (perennial wrapper) + **J** (accounting) | A missing finalizer leaks silently; a blocking finalizer stalls the whole space's sweep; external bytes are invisible to `heapSize` and to the GC trigger. | `external_finalizer` (S), `external.perennial_never_finalized` (R), checklist C7 (J) | S: yes · R: yes · J: recorded |
| 8 | **No heap-ceiling wait may be reachable only by the thread that can free.** | **R**, isolated process | The only exit is `std::abort()` in `ProtoSpace::waitForHeapHeadroom` (`core/ProtoSpace.cpp:1548-1554`). Before that, a silent hang. | `heap.ceiling_progress` | yes |
| 9a | **Post-P3: interning is global, so no code may assume a per-space symbol address.** | **S** | A cached raw symbol pointer across space lifetimes is correct until it is not. | `global_symbol_assumption` | yes |
| 9b | **Post-P3: the module list is a GC root.** | **R** | "Never freed" is not "is a root" — the list survives and its contents are collected under it. | `module.root_survives_cycle` | yes |
| 9c | **Post-P3: a module's identity is provider + path + version.** | **S** + **R** | Keying by path alone aliases two different modules into one, first load winning. Wrong answer, no error. | `module_key_shape` (S), `module.alias_rejected` (R) | yes |
| 10 | **A GC or concurrency test that cannot fail is worse than no test.** | **R**, applied to the suite itself | protoST's suite looked green for its whole history. | `selfcheck.*` matrix | yes |
| 11 | **NEW — every OS thread that holds a `ProtoObject*` must be a protoCore-registered thread**, or everything it holds must be pinned in a `ProtoRootSet`. | **R** | See §D7. GC Phase 2 scans roots by walking `space->threads` (`core/ProtoSpace.cpp:381-411`). A thread absent from that list is **never root-scanned**: its `automaticLocals`, `returnValue`, `pendingRoot` and young chain are invisible to the marker, so its live objects are swept under it. This is *worse* than rule 2 — rule 2 hangs, this corrupts. | `thread.registered` | yes — with three conforming shapes, §D12 |
| 12 | **NEW — a `ProtoContext::CriticalSection` must not be held across a blocking wait, an `UnmanagedScope`, or a `safepoint()` that is expected to submit.** | **S** | `parkForStopTheWorld` skips parking while `criticalSectionDepth > 0`, and `safepoint()` skips young-chain submission at depth > 0 (`core/ProtoContext.cpp:376`). So a critical section held across a wait reproduces rule 2's hang and rule 1's non-submission through a **different** mechanism, in code that looks like it is doing the right thing. Four runtimes use `CriticalSection`. | `critsec_across_block` | yes |

Rules 11 and 12 are additions this plan argues for; they are not in the maintainer's list. Rule 11 comes from a bug class already met (`protoClojure`: *join workers before `ProtoSpace` dies*) plus the Phase-2 reading above. Rule 12 is a risk, not yet a met bug, and is included because it is cheap, static and defeats the two most expensive rules by a back door.

**Architecture:** four artefacts, of which three are new machinery in protoCore and one is a document.

- **`libprotoCoreConformance.a`** — a **framework-free** static library built inside protoCore, installed with the package and exported as `protoCore::conformance`. It contains every **R** case. It is framework-free because protoCore's GoogleTest is a `FetchContent` download with `INSTALL_GTEST OFF` (`test/CMakeLists.txt:7-16`) and is therefore not available to a consumer of the installed package; and it is built **inside** protoCore because several cases need `headers/proto_internal.h`, which is not installed (`CMakeLists.txt:70-75` installs `headers/protoCore.h` alone). Its public header `headers/protoCoreConformance.h` uses only public types.
- **`proto::conformance::Host`** — the adaptor. This is how a rule that needs the embedder's own idioms is expressed **without protoCore knowing the embedder**: protoCore never names protoST or protoJS; it declares the *capabilities a case needs* ("make me allocate `n` cells of garbage and tell me how many you allocated", "run this closure on each kind of thread you create", "intern this text the way your runtime interns an attribute key") and asserts kernel-side invariants over the result. An unimplemented capability returns `NotApplicable`, not `Pass`.
- **`scripts/conformance/check_static.py`** — the **S** checker, shipped by protoCore, run by each embedder over its own tree against a per-embedder allowlist file. It is a **ratchet**: existing sites are listed with a written justification, and any new or unjustified site fails.
- **`docs/EMBEDDER-CONFORMANCE.md`** — the rule table above as normative text, plus the **J** checklist whose answers are recorded per embedder in that embedder's own `docs/CONFORMANCE.md`.

**Tech Stack:** C++20, CMake ≥ 3.16, GoogleTest 1.14 (embedder side only, via each embedder's existing test target), Python 3.11 (`check_static.py`, stdlib only — no new dependency), AddressSanitizer, ThreadSanitizer, `clang-query` from the system LLVM (optional; the three shapes of rule 3 degrade to regex when it is absent).

**Spec:** this plan is the spec of record until Task 16 writes `protoScala/docs/platform/CONFORMANCE-SPEC.md` in the shape of `PROTOMAP-SPEC.md` and `PMQ-SPEC.md` (problem, semantics, GC constraints, tag budget — none consumed here — tests, rollout, decisions). Normative sources this plan cites and must not contradict: `protoCore/docs/MemoryModel.md` (especially §5, the external-memory boundary), `protoCore/docs/GarbageCollector.md` (§7 finalizer contract, Phase 1–4), `protoCore/docs/TESTING.md`, and `protoScala/docs/plans/2026-09-24-phase-p3-global-interning.md` (rule 9; owned by a sibling agent — **read it, never edit it**).

---

## Global Constraints

- **Workspace safety.** Create, modify or delete nothing outside `/home/gamarino/Documentos/proyectos`. Scratch output goes to `/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance/`. `perf stat` only, never `perf record`. No `rm -rf` outside that scratch directory and the build directories. No `cmake --install` to a system prefix; when an installed-package path must be exercised, install into `$SCRATCH/prefix` with `-DCMAKE_INSTALL_PREFIX`.
- **Do not touch the sibling's file.** `protoScala/docs/plans/2026-09-24-phase-p3-global-interning.md` is owned by another agent. Read it; do not edit it. This plan's own file is `protoScala/docs/plans/2026-09-25-phase-p4-embedder-conformance.md`.
- **P3 first.** Rule 9 is defined by P3's rulings (global interning, the module root table, `ModuleIdentity` = provider + path + version, P3 §D11). Tasks 2–14 do not depend on P3; **Task 15 does**, and Task 1 Step 1 records whether P3 has landed. If it has not, Task 15's cases are written and registered as `Skipped` with the reason `"requires protoCore >= 2.2.0 (P3)"`, checked by `PROTOCORE_VERSION_MINOR`.
- **Do not start while the family is building.** Task 1 Step 1 stops unless every repository is clean and the maintainer has confirmed the family is quiet. A protoCore change landing under a sibling's build produces the ABI-mismatch class of crash (memory: ABI mismatch crashes stale binaries) with no useful diagnostic.
- **This phase is additive to protoCore's ABI.** A new static library, a new installed header and a new script. `PROTOCORE_ABI_SOVERSION` stays `2`; the version moves `2.2.0` → `2.3.0` (Task 16). No existing class gains a member or a virtual. **Do not change an existing object model to get a new semantic** (memory: extend protoCore by new type) — if a case needs an observation protoCore does not expose, the answer is a new read-only accessor, never a changed field.
- **Every GC claim carries a test that fails if the premise is false**, and — the specific lesson of this phase — **every case in this suite carries a named mutation that must turn it red** (rule 10, Task 9). A case without an executed mutation is not implemented.
- **Every reclamation assertion must be proportional to the garbage the case created.** `reclaimed > 0` is forbidden in this suite. Track Y found a forced cycle reclaiming **4–7 cells instead of 205,120** while a `> 0` assertion passed; protoCore's own `GCSurvivorRechainTests.cpp:145` still asserts only `EXPECT_GT(freeAfter, freeBefore)`, which one cell satisfies. Every GC case in this suite asserts `reclaimed >= kReclaimFraction * allocatedByTheWorkload` with the workload **self-reporting** what it allocated (memory: benchmarks must self-report). Task 3 §Step 2 fixes `kReclaimFraction` and says why.
- **Every case self-reports.** A case's `CaseResult::detail` always carries the numbers it measured, even on `Pass`. A run whose details are all zeros is a broken harness that looks like a clean suite — which is the whole subject of this phase (memory: silent bench failures fooled the harness).
- **`ctest` is run with stdin at EOF, everywhere.** protoST's **S18**: a debugger test blocks forever on an open stdin. Every `ctest` invocation in this plan appends `< /dev/null`, and Task 2 Step 7 makes the suite runner do the same for the embedders it drives. A conformance runner that hangs is indistinguishable from a conformance failure that hangs, which would make rules 2, 8 and 11 undebuggable.
- **Every embedder is rebuilt from clean** and its suite compared to this baseline:

  | Project | Baseline | Build discipline |
  |---|---|---|
  | protoCore | **438/438** ctest | `-j4` maximum |
  | protoPython | **582/583** — the lone `protopy_import_site` failure is **pre-existing** (a `.pth` in a sibling `venv/`) and is not attributed to this work | `build_release/`, never `build-release/` (memory: build dirs) |
  | protoJS | **ctest 34/34**, plus test262 `built-ins/{Object,Reflect,Proxy}` at **3619 passed** with no newly failing test | **no `-j` at all**; test262 **sequential** (`TEST262_CONCURRENCY=1`); **ask the maintainer before any full sweep** (memory: protoJS build/test parallelism hang DEV12) |
  | protoST | **854/854** | `-j4` maximum |
  | protoClojure | **391/391** | `-j4` maximum |
  | protoScala | **1248/1248** | `-j4` maximum |

  "From clean" means a fresh build directory, not an incremental rebuild.
- **A conformance failure is not a licence to change the runtime in this phase.** P4 delivers the suite and the **recorded first-run baseline**. Fixing what it finds is P5, one commit per finding, except where a finding is a one-line omission whose fix is covered by an existing case (Task 11 §Step 5 states the exception precisely). The reason is the ordering lesson of protoST's `MailboxCursor::adopt`: a fix landed in the same commit as the test that could not yet reach it proves nothing.
- **Purity over performance.** Reject any variant that fragments protoCore's conceptual model for a measured win (memory: purity > performance). The model this phase must keep intact: *protoCore states its participation obligations once, and the same statement is what every embedder executes.* A per-embedder special case in the library is the failure mode to avoid — that is what the `Host` adaptor exists to prevent.
- **Git:** work on branch `feature/embedder-conformance-p4` in `protoCore`, and `feature/p4-conformance` in each runtime. Never push. Stage explicitly by path (`git add <file> <file>`), never `git add -A` or `git add .`. Commit with the configured identity (name **"Gustavo Marino"**; never "Gustavo Adrian Marino"; never override `user.email`). End every commit message with:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```
- All code comments, documentation and commit messages in **professional English**, regardless of the language of the request.

---

## Task 0: Maintainer decisions

Twelve decisions, each with options and a recommendation. **These are recommendations, not agent decisions.** The change is process-wide across six repositories, so the executor asks before Task 2. The questions the maintainer explicitly asked for — where the suite lives, how an embedder runs it, how an embedder-specific idiom is expressed, which rules are static/runtime/judgement, hard failure versus reported finding, how protoJS participates, and what each runtime is expected to fail today — are §D1, §D2, §D3, §D4/§D5/§D6, §D8, §D9 and §Task 1 Step 6 respectively. §D11 and §D12 were added while writing the plan, because reading the kernel changed two of the maintainer's rules — §D11 splits rule 4 by destination and hands it a runtime case the kernel already ships, and §D12 shows that rule 11 has three conforming shapes rather than one.

### D1 — Where the suite lives and how an embedder runs it — **Recommendation: (c) a framework-free static library built inside protoCore, installed and exported as `protoCore::conformance`, which each embedder wraps in three lines of its own test framework**

**(a) A protoCore-installed GoogleTest library that registers its own `TEST()`s.** The embedder links it and `gtest_discover_tests` finds the cases.
- **Impossible as the package stands.** protoCore fetches GoogleTest with `FetchContent` and sets `INSTALL_GTEST OFF CACHE BOOL "" FORCE` (`test/CMakeLists.txt:13`), so no GTest is installed with the package. An installed library carrying GTest symbols would not link for a consumer, and making it link means installing GTest from protoCore's package — which the comment at `test/CMakeLists.txt:5` explicitly rules out ("test-only dependency; do not install with the library package").
- It would also impose GoogleTest on every embedder. protoST uses **Catch2** (`protoST/tests/unit`), not GoogleTest. Rejected.

**(b) A header of test cases each embedder compiles.** protoCore ships `protoCoreConformance.h` containing the case bodies; each embedder compiles it into its own test target.
- Simple, no new build artefact, and it automatically adopts the embedder's own framework.
- **But several cases need `headers/proto_internal.h`**, which is **not installed**: `CMakeLists.txt:70-75` sets `PUBLIC_HEADER "headers/protoCore.h"` and that single file is all `install(TARGETS ...)` ships (`CMakeLists.txt:226`). The cases that need it are the ones that must read `INLINE_STRING_MAX_BYTES` (rule 4's inline-string boundary, `headers/proto_internal.h:300`) and the ones that use `toImpl<ProtoThreadImplementation>` to read a thread's unmanaged depth — the mechanic protoCore's own `UnmanagedRegionTests.cpp:45-50` uses.
- A header-only suite therefore has to either duplicate internal constants (which is how a constant drifts silently — exactly this phase's subject) or drop its most valuable cases. Rejected as the primary mechanism, **kept as a thin adjunct**: a one-line `protoCoreConformanceGTest.h` and `...Catch2.h` that expand the library's case list into the embedder's framework (Task 2 Step 6).

**(c) A framework-free static library built inside protoCore. — RECOMMENDED**
- Built in protoCore's own tree, so it sees `proto_internal.h` and every internal constant **by inclusion, never by copy**. Installed as `libprotoCoreConformance.a`, added to the existing `protoCoreTargets` export set, reached as `protoCore::conformance`.
- Its public header uses only public types, so the embedder's compiler never needs the internal header.
- It reports results as data (`std::vector<CaseResult>`), so the embedder's framework does the asserting. Three lines in GoogleTest, three in Catch2, three in a bare `main`.
- **Cost:** one new target, one new installed header, one new export entry. And the `install()` block is guarded by `if(CMAKE_PROJECT_NAME STREQUAL "protoCore")` (`CMakeLists.txt:217`) because protoPython consumes protoCore by `add_subdirectory`, so the target must work **both** as an installed package and as an in-tree target. Task 2 Step 5 handles both and Task 13 Step 2 proves the `add_subdirectory` path.

**(d) A script that greps every repository from one place.** One `audit.sh` in protoCore, run over all five trees.
- This is the **static half** and it is genuinely the right mechanism for rules 4, 6, 7-static, 9a, 9c-static and 12 — so it is adopted *as well*, as `scripts/conformance/check_static.py` (Task 8). It cannot express rules 1, 2, 3, 5, 8, 9b or 11, because those are behaviours, not text. Adopted as a complement, rejected as a substitute.

### D2 — How a rule that needs the embedder's own idioms is expressed without protoCore knowing the embedder — **Recommendation: a capability adaptor, where protoCore names capabilities and never names runtimes, and an unimplemented capability yields `NotApplicable`, never `Pass`**

The suite must assert things like *"when your runtime creates and discards N units of garbage, protoCore reclaims them"*. protoCore cannot know what a "unit of garbage" is in Clojure or in Smalltalk. Three ways to bridge that:

**(a) protoCore knows the embedders.** A `#ifdef PROTOST` per case.
- Rejected outright: it fragments the conceptual model the suite exists to state once, and it means every new runtime edits protoCore. It is the exact failure this phase is meant to prevent, committed inside the tool built to prevent it.

**(b) The embedder writes its own cases against a documented rule list.** protoCore ships only `docs/EMBEDDER-CONFORMANCE.md`.
- This is what already happens, and it is why five runtimes disagree about safepoints. protoST instruments every loop kind and ships a `PROTOST_NO_GC_SAFEPOINT=1` escape hatch; protoClojure has none. Rejected.

**(c) A capability adaptor. — RECOMMENDED** protoCore declares an abstract `Host` whose methods are *capabilities*, not runtime concepts:

```cpp
namespace proto::conformance {

/// What a case needs from the runtime under test.  protoCore names
/// capabilities; it never names a runtime.  Every method has a default
/// implementation that reports the capability as unavailable, so an embedder
/// implements only what it can honestly supply and the rest is reported
/// NotApplicable — never Pass.
class Host {
public:
    virtual ~Host() = default;

    /// The context the runtime's main thread evaluates in.  REQUIRED.
    virtual ProtoContext* mainContext() = 0;

    /// Make the runtime allocate at least `requestedCells` cells of data that
    /// is unreachable when this call returns, using the runtime's OWN
    /// evaluator — not protoCore calls.  Returns the number of cells the
    /// runtime actually allocated, measured by the runtime itself, or 0 if
    /// the capability is unavailable.  The number is the case's denominator,
    /// so a Host that cannot measure it must return 0 rather than guess.
    virtual unsigned long makeGarbage(unsigned long requestedCells) { (void)requestedCells; return 0; }

    /// Same, but the garbage must be built out of the runtime's own
    /// *sequence* type — the type a user gets from a literal vector, list or
    /// array.  This is rule 5's probe and it deliberately does not say
    /// ProtoTuple: the case measures reclamation, so it is right whatever the
    /// runtime chose.
    virtual unsigned long makeSequenceGarbage(unsigned long requestedCells) { (void)requestedCells; return 0; }

    /// Intern `text` the way this runtime interns an ATTRIBUTE KEY, and
    /// return the pointer it would use as the key.  Rule 4's probe.
    virtual const ProtoObject* internAttributeKey(const char* text) { (void)text; return nullptr; }

    /// Describes one kind of OS thread the runtime creates.
    struct ThreadKind { const char* name; bool blocksWhenIdle; };

    /// Enumerate the runtime's thread kinds and, for each, start one, run
    /// `body` on it, and join it.  `body` is called ON that thread with the
    /// context that thread uses.  Rules 2, 11 and 12's probe.
    using ThreadBody = void (*)(void* user, const ThreadKind&, ProtoContext*);
    virtual bool forEachThreadKind(ThreadBody body, void* user) { (void)body; (void)user; return false; }

    /// Run the runtime's own producer/consumer or actor workload, `units` of
    /// work, and return true when it completed.  Rule 8's probe: the suite
    /// sets a hard heap ceiling first, so "completed" is the assertion.
    virtual bool runProducerConsumer(unsigned long units) { (void)units; return false; }

    /// Total bytes of memory the runtime allocated outside protoCore and is
    /// accounting for itself (MemoryModel.md section 5, "Count it").
    /// Returns (unsigned long)-1 when the runtime keeps no such total.
    virtual unsigned long externalBytesAccounted() { return (unsigned long)-1; }

    /// Load the same logical path from two different providers, returning
    /// whether the runtime kept them distinct.  Rule 9c's probe.
    virtual int loadSamePathTwoProviders() { return -1; }  // -1 = unavailable
};

}  // namespace proto::conformance
```

The wins, stated plainly:

- **protoCore never names a runtime.** Grep for `protoST|protoJS|protoPython|protoClojure|protoScala` in `conformance/` must return zero, and Task 9 Step 5 makes that a test.
- **A runtime cannot accidentally pass.** Every default returns "unavailable", and the runner maps unavailable to `NotApplicable` with the capability's name in `detail`. A suite that reports `NotApplicable` for `makeGarbage` has told the maintainer exactly why its GC cases mean nothing — which is the thing protoST's 833 green tests never said.
- **`makeGarbage` returning the count is the fix for the Track-Y vacuity.** The denominator comes from the runtime, is printed on pass and on fail, and a Host that returns 0 turns every proportional assertion into a `NotApplicable` instead of a vacuous pass.

**Open question for the maintainer:** should `mainContext()` be the only required method, or should `makeGarbage` also be required, so that a Host that cannot make garbage fails to compile rather than reporting `NotApplicable`? The recommendation is **required** for `makeGarbage` too — a runtime that cannot allocate garbage on demand cannot be audited at all, and finding that out at compile time is better than at run time. That makes two pure virtuals.

### D3 — Hard failure or reported finding, per rule — **Recommendation: hard for behaviour, ratchet for text, recorded for judgement — exactly as the rule table's last column states**

Three regimes, and the reason each rule sits where it does:

- **Hard failure (all R cases).** A behavioural case either observes the invariant or it does not; there is no "arguably fine". These are rules 1, 2, 3-stress, 5, 7-perennial, 8, 9b, 9c-runtime, 10, 11. A runtime whose `gc.young_submitted` case fails is not reclaiming, and there is no reading of that which is acceptable.
- **Ratchet (all S checks).** A static check cannot distinguish a legitimate use from a violation in every case — `fromUTF8String` is correct for a string that is not a key, and `getOwnAttributeDirect` *must* be compared against `nullptr` (`headers/protoCore.h:260-264`). So each embedder carries `conformance-allow.txt`: one line per known site, each with a **written justification**, and the check fails on any site not in the file and on any file entry whose site no longer exists. **The count is never the gate**; the delta is. This is what makes the check land without a week of triage and still catch tomorrow's site.
- **Recorded finding (all J items).** The checklist produces a dated, signed answer in the embedder's `docs/CONFORMANCE.md` with a citation. An unanswered item is a hard failure of the *review*, not of the suite; the runner reports `NeedsReview` and exits non-zero only under `--strict-review`, which Task 17 turns on for the family once the first round of answers exists.

**One deliberate exception.** `external_finalizer` (rule 7's "a finalizer is supplied") is **S with a hard failure**, not a ratchet, for a *new* call site — but every existing site goes into the allowlist with its justification, because a null finalizer is legitimate when the embedder frees the memory itself. protoST has three such sites (`DapServer.cpp:257`, `STRuntime.cpp:671`, `ExecutionEngine.cpp:2452-2453`), all passing `nullptr`, and whether each is correct is a **J** question (checklist C7), not something a script can rule on. The script's job is to make sure nobody adds a fourth without answering the question.

### D4 — Which rules are static, which runtime, which judgement — **Recommendation: as the rule table states; the two reclassifications are §D5 and §D6 and the honest J list is three items**

The full assignment is the rule table above. What matters for the maintainer's review is the **J list**, because that is where this plan declines to mechanise:

1. **C3 — rule 3 in general.** *"No `ProtoObject*` is held across an allocation only in a C++ local."* Deciding this in general requires knowing, for an arbitrary call, whether it can allocate, and whether an arbitrary local is still live afterwards. That is escape analysis over the whole call graph, and a checker that claimed to do it would produce a false-negative rate nobody could estimate — which is worse than a checklist, because it would be *believed*. What **is** mechanised: the `gc.host_stress` case under ASan with a forced cycle per allocation batch (which is what actually caught all three instances), and `clang-query` matchers for the **three named shapes** the session met (Task 5 Step 3). The general rule stays J.
2. **C5 — which structure rule 5 should be measured on.** The measurement is mechanical (§D5); choosing *which* of the runtime's types is "the sequence a user gets from a literal" is a reading of the runtime. The Host answers it by implementing `makeSequenceGarbage`, and the checklist records which type was chosen and why.
3. **C7 — whether external memory is correctly accounted and correctly released.** `externalBytesAccounted()` mechanises *"does an accounting exist at all"*. Whether the number is **right** — whether a region reached through two wrappers is double-counted, whether a null finalizer is correct because the embedder frees it elsewhere — is unknowable to protoCore by construction. `docs/MemoryModel.md` §5 "Why going further is impossible" is the normative statement of exactly this, and the checklist quotes it: *"External size is whatever the embedder declares... A collection policy driven by that number is a policy driven by a figure that can drift arbitrarily from reality, and the kernel has no way to detect the drift."* A conformance suite is in the same position as the collector here, and says so.

### D5 — Rule 5 (`ProtoTuple` is never transient) is reclassified from judgement to a runtime measurement — **Recommendation: measure reclamation, do not grep for the type**

The maintainer's framing was *"needs judgement, not a grep"*, and the second half is right: a grep for `newTuple` finds sites and cannot say whether a site is transient. But the conclusion does not follow, because **the rule's consequence is measurable even though its cause is not.**

Every interned tuple node is perennial: `TupleInterner` entries are never removed (`headers/proto_internal.h:1088-1095`) and the collector records only a published count for them under STW, dereferencing nothing (`core/ProtoTuple.cpp:121-125`). So a runtime that builds its user-visible sequences out of `ProtoTuple` has a heap that **grows monotonically with the number of sequences ever constructed** and never shrinks. That is exactly what `makeSequenceGarbage` measures: build and discard N of the runtime's sequences, drive cycles, and assert reclamation proportional to what the runtime says it allocated.

Why this is strictly better than the grep:

- It is **correct for a runtime that reached the same trap through a different type.** protoCore has other interned or perennial structures, and a future one would be caught with no change to the check. A grep for `ProtoTuple` would not.
- It **passes a runtime that uses `ProtoTuple` legitimately** — for a genuinely perennial structure — instead of raising a finding a human then has to dismiss.
- It is the check that **would have caught protoClojure's vectors**, which is the bug the rule comes from. Track C moved vectors to a `ProtoSparseList` box holding one `ProtoList` (`protoClojure/src/.../VectorOps.h:1-40`, commit `90d3d86`) precisely because *"protoCore interns every tuple node and never frees one"* (`VectorOps.h:24-27`). A reclamation measurement is a direct test of that sentence.

A **grep census is still produced** — as information in the report, never as a gate — because knowing which runtime touches `ProtoTuple` at all is useful context for reading a failure.

### D6 — Rule 6 is not "never compare to `nullptr`"; it is a per-function table — **Recommendation: ship the table, check against it, and fix the doc that made the confusion possible**

A naive check ("flag `== nullptr` near an attribute call") would flag **correct** code. The conventions genuinely differ, verified in `core/ProtoObject.cpp`:

| Function | Not found | Invalid input | Citation |
|---|---|---|---|
| `getAttribute` | `PROTO_NONE` | `nullptr` | `core/ProtoObject.cpp:773-781` — *"nullptr is reserved for invalid input, not 'missing'"* |
| `getOwnAttributeDirect` | **`nullptr`** | `nullptr` | `headers/protoCore.h:260-264`, `core/ProtoObject.cpp:1776` |
| `hasAttribute` / `hasOwnAttribute` | `PROTO_FALSE` | — (a boolean object, never a C++ `bool`) | `core/ProtoObject.cpp:2134`, `:2428`, `:2465-2466` |

So `protoClojure/src/.../Primitives.cpp:130-132` comparing a `getOwnAttributeDirect` result against `nullptr` is **right**, and a checker that flagged it would train everyone to ignore it. The check is therefore a table lookup: the sentinel compared against must match the function called, and a comparison against the *other* sentinel is the failure.

Two further consequences worth the maintainer's attention:

- **`getOwnAttributeDirect` cannot distinguish "absent" from "not an object cell".** That is a genuine expressive gap in protoCore, it is why this class of confusion exists at all, and the probe the memory notes recommend (`hasOwnAttribute` / `hasAttribute` before the read) is the documented way round it. Task 16 Step 2 adds the table to `docs/EMBEDDER-CONFORMANCE.md`; it does **not** change the function, because changing a return convention is an ABI-visible semantic change and this phase is additive.
- **`PROTO_NONE` is `(const ProtoObject*)321UL`** (`headers/protoCore.h:63-65`), so a mistaken dereference is a wild read at a fixed low address — reliably a segfault rather than silent corruption. Rule 6 is the *least* silent rule in the set, which is why it is the cheapest to check and the lowest-value to have checked. It is included because it costs one regex.

### D7 — Rule 11 (thread registration) is an addition, and it is the most dangerous rule in the set — **Recommendation: adopt it, and make `thread.registered` the first case a new embedder runs**

Not in the maintainer's list, and it should be, because it is rule 2's strictly worse twin.

`ProtoContext`'s constructor does **not** register a thread with the space. There is no `registerThread` in protoCore at all — confirmed by grep over `headers/*.h` and `core/*.cpp`. `runningThreads` is incremented only in the `ProtoThreadImplementation` constructor (`core/Thread.cpp:27`), which runs only for a thread created through `ProtoSpace::newThread` (`headers/protoCore.h:2011-2016`). A `ProtoContext` built on a raw `std::thread` gets `thread == nullptr` unless it happens to be running on `space->mainThreadId` (`core/ProtoContext.cpp:66-76`).

Now read GC Phase 2. Root collection walks **`space->threads`** — the sparse list of registered `ProtoThread`s — and for each one calls `scanContexts` on that thread's context chain, taking `automaticLocals`, `returnValue`, `pendingRoot` and the young-chain head (`core/ProtoSpace.cpp:381-411`, and the `scanContexts` lambda at `:340-378`).

**A thread that is not in `space->threads` is never root-scanned.** Its locals are invisible to the marker. Every object it holds and has not pinned in a `ProtoRootSet` is swept while live. Compare the two failure modes:

- Rule 2 (registered thread blocks outside `UnmanagedScope`): the collector **never finishes**. Nothing is corrupted; the process hangs. Bad, and loud once you attach a debugger.
- Rule 11 (unregistered thread holds objects): the collector **finishes and is wrong**. Use-after-free, at a distance, under load.

And the shape is already in the tree — `protoCore/test/ConcurrentMarkSafetyTests.cpp:77` constructs `ProtoContext threadCtx{&space};` inside a raw `std::thread`. In that test it is deliberate and the objects are pinned; as a pattern for an embedder to copy it is a trap, and Task 16 Step 3 adds a warning comment there.

The case is cheap and exact: `forEachThreadKind` runs a body on each of the runtime's thread kinds, and the body asserts `ctx->thread != nullptr` and that `ProtoContext::getCurrentThread(ctx)` is non-null (`headers/protoCore.h:1380`). One boolean per thread kind, no heuristics. The adjacent already-met bug is protoClojure's *join workers before `ProtoSpace` dies* (memory), which is the same family: a thread protoCore does not fully know about, outliving or racing the space.

### D8 — How protoJS participates — **Recommendation: a separate, small ctest target that never triggers a test262 sweep, built with no `-j`, and a maintainer gate before any sweep**

protoJS cannot be treated like the others: builds with `-j >= 2` hang DEV12 and test262 runs hang it too (memory: protoJS build/test parallelism). Options:

**(a) Fold the conformance cases into protoJS's existing test target.** Any conformance run then rebuilds and reruns everything.
- Couples a 30-second check to the family's most fragile build. Rejected.

**(b) A separate `protojs_conformance` executable, registered as a handful of ctest tests, built as its own target. — RECOMMENDED**
- `cmake --build build --target protojs_conformance` with **no `-j`** builds only the conformance translation unit plus its link, so the check is reachable without touching the 34 ctest tests or test262 at all.
- Run with `ctest --test-dir build -R '^conformance\.' --output-on-failure < /dev/null`, sequentially (no `-j`).
- **test262 is not part of conformance and is never invoked by it.** The baseline `built-ins/{Object,Reflect,Proxy}` at 3619 passing is a *regression* gate for protoJS's own phases, not a conformance signal, and the runner must not be able to start it. Task 14 Step 4 asserts that `ctest -R '^conformance\.' -N` lists only conformance tests.
- **The maintainer is asked before any full sweep**, and Task 14 Step 5 is written as a stop.

**(c) Skip protoJS.** Rejected: protoJS is the runtime with the most history in rule 4's class (the uninterned-key bugs, `1ff62b95`, the string audit `8305218d`), so it is the one whose ratchet matters most.

### D9 — What the first run is expected to find, and whether a prediction that misses is a defect of the plan — **Recommendation: yes, treat a miss as a finding about the plan, and record both columns**

The predictions are in Task 1 Step 6 and they are load-bearing: **the point of predicting is that a surprise is visible as a surprise.** A suite whose first run produces a list of failures nobody expected teaches nothing about the runtimes and everything about how little was known. So Task 1 Step 6 writes the prediction table to disk **before** any case is written, Task 17 Step 2 diffs the observed results against it, and every divergence gets one line of explanation in `protoScala/docs/DECISIONS-LOG.md`. A divergence is not a failure of the phase; an *unexplained* divergence is.

### D10 — Version, SOVERSION and the release story — **Recommendation: `2.2.0` → `2.3.0`, `PROTOCORE_ABI_SOVERSION` stays `2`**

The phase adds a static library, an installed header and a script. It adds no member to an existing class, no virtual, and changes no return convention (§D6 deliberately documents rather than fixes). `SameMajorVersion` compatibility (`CMakeLists.txt:256-261`) means every current consumer keeps working, and an embedder that wants the suite asks `find_package(protoCore 2.3 REQUIRED CONFIG)`.

**Alternative:** ship the conformance library as a separate CMake package (`protoCoreConformance`), versioned independently.
- Cleaner separation, and it keeps a test-only artefact out of the runtime package.
- **But** the cases must track protoCore's internals exactly — that is the whole reason for §D1(c) — so an independently versioned package would immediately need a tight `protoCore` version pin, which is the coupling without the convenience. **Not recommended**, though the maintainer may prefer it if packaging policy forbids shipping test artefacts in the runtime package; in that case the target simply moves to its own export set and nothing else in this plan changes.

### D11 — Rule 4's severity is not uniform: an uninterned key is a *performance* fault through `getAttribute` and a *silent correctness* fault through `getOwnAttributeDirect` — **Recommendation: split the check by destination, and adopt the `PROTOCORE_TRUST_SYMBOLS` experiment as rule 4's runtime case**

This decision exists because the plan could not predict protoJS's rule-4 result without reading how protoCore actually handles a non-symbol key, and what it found changes the rule.

**Three different behaviours, all in `core/ProtoObject.cpp`:**

1. **`setAttribute` auto-interns.** A `POINTER_TAG_STRING` name is interned *strongly* before use (`:1004-1015`). The comment there records the real bug that forced it: a weak intern let the `SymbolTable` bucket keep a freed pointer, so *"the next `createSymbol()` with the same content would return a fresh pointer that no longer matches the SparseList key — a missed lookup that surfaces as `Array.isArray === undefined` after a few hundred async operations have triggered a GC."*
2. **`getAttribute` falls back to content.** A `POINTER_TAG_STRING` name is resolved through `symbolTable->lookupByContent` (`:785-799`), returning `PROTO_NONE` only when the content was never interned at all. So a write and a read that both use an uninterned string **do agree** — at the cost of a content lookup on every read.
3. **`getOwnAttributeDirect` does neither.** It has no `POINTER_TAG_STRING` branch and no content fallback (`:1776-1860`): it goes straight to the `AttributeCache` and the AVL probe, both keyed on the **raw `name` pointer**.

**Therefore an uninterned key works through the general path and silently misses through the fast path** — and `getOwnAttributeDirect` is exactly the Phase-6 `LOAD_ATTR` fast path that protoPython and protoJS both added (memory: Phase 6 LOAD_ATTR fast path, `getOwnAttributeDirect`-first). That is the mechanism behind protoJS's repeated bugs in this class, stated precisely for the first time.

So the check splits, and the severities follow the mechanism rather than the function name:

| Destination of the uninterned key | Consequence | Severity |
|---|---|---|
| `setAttribute`, `setAttributeIfEqual` | a strong intern per call — correct, costs a `SymbolTable::intern` | `warn` |
| `getAttribute`, `getOwnAttribute`, `hasAttribute`, `hasOwnAttribute` | a `lookupByContent` per call — correct, costs a content hash over the rope | `warn` |
| **`getOwnAttributeDirect`** | **silent miss: `nullptr` is returned and is also this function's "absent" value** | **`error`** |

**And rule 4 gains a runtime case, using a mechanism the kernel already ships and nobody uses.** `getAttribute`'s defensive branch is guarded by an env var (`:789-795`):

> *"EXPERIMENT (2026-06-16): when `PROTOCORE_TRUST_SYMBOLS=1` is set, treat a STRING-tagged name as definitely-absent — the embedder contract is 'always pass a SYMBOL'. This lets us measure how much the defensive `lookupByContent` costs and reveal embedder sites that violate the contract (they will read back `PROTO_NONE` and tests will fail)."*

That is rule 4's conformance case, written two months ago and never run across the family. The case is: **run the embedder's own existing suite with `PROTOCORE_TRUST_SYMBOLS=1` and require the same pass count.** It is exact, it has no false positives, and it needs no new kernel code — Task 11 Step 6 adds it as `symbol.trust_symbols_clean`, a per-embedder ctest entry that re-runs that embedder's suite under the flag.

**The question for the maintainer** is only the policy: is `symbol.trust_symbols_clean` a **hard failure** or a **reported finding** for the first run? The recommendation is **reported finding in P4, hard failure from P5**, because protoJS has 248 key-shaped `fromUTF8String` sites and turning the flag into a gate on day one would fail protoJS's whole suite for a reason unrelated to this phase. Recording the number is the deliverable; fixing it is protoJS's next phase.

### D12 — Rule 11 must accept more than one conforming shape — **Recommendation: three answers per thread kind, not two, because protoJS is correct in a way the naive check would fail**

The naive form of rule 11 — *"every thread the runtime creates is registered with `ProtoSpace::newThread`"* — would report protoJS as non-conforming, and protoJS is right.

protoJS has **three** thread shapes and all three are sound:

- **Registered**, through `space->newThread`: `src/Deferred.cpp:421`, `src/ProtoCoreNativeBindings.cpp:171`, `src/ProtoCoreModule.cpp:649`. Root-scanned, in the quorum.
- **Unregistered and holding no `ProtoObject*`**: the CPU/IO pool and socket-accept threads never touch protoCore, handing work back through `EventLoop::enqueueCallback`. Nothing to scan, so nothing to lose.
- **Unregistered and owning its own `ProtoSpace`**: `WorkerThreadsModule` workers each construct a separate space (`src/JSContext.h:234`), so they are a *main* thread of their own space rather than a stray thread of someone else's.

The rule is therefore about **holding**, not about creating, and the case's verdict per thread kind is one of three:

```cpp
enum class ThreadVerdict {
    Registered,      // ctx->thread != nullptr and reachable from space->threads
    HoldsNothing,    // the kind asserts it touches no ProtoObject* -- conforming
    OwnSpace,        // the kind is the main thread of its own ProtoSpace -- conforming
};
```
`Host::ThreadKind` therefore carries a declared `verdict` field, and the case's job is to **verify the declaration rather than take it**: for `HoldsNothing` it asserts `ctx == nullptr || ctx->allocatedCellsCount == 0` after the body runs; for `OwnSpace` it asserts the thread's `space` differs from `mainContext()->space`; for `Registered` it asserts `ctx->thread != nullptr`. A kind that declares `HoldsNothing` and then allocates is a **Fail**, and that is the finding worth having — a declaration the code contradicts.

**Alternative:** require registration unconditionally, and treat protoJS's pools as a finding to fix.
- Simpler check, and arguably a simpler system.
- **But it is wrong**: registering a thread that holds nothing adds it to the STW quorum for no benefit, which makes every pause wait for a thread that has nothing to contribute. It would trade a non-problem for a latency cost. **Not recommended.**

### Recorded, not asked (facts the maintainer should see)

- **protoClojure has no safepoint at all today.** It had one; commit `1bcc433` added it and `dde94c8` reverted it the same day, with the stated reason that it *"only served the allocation-budget GC trigger being reverted in protoCore"*. That trigger is **not** reverted in protoCore today: `PROTOCORE_GC_REINCLUDE_SURVIVORS` is `ON` by default (`protoCore/CMakeLists.txt:125`) and `ProtoContext::safepoint` is the sole young-chain submission point under it (`core/ProtoContext.cpp:363-389`). **protoClojure is therefore in protoST's pre-S15 state right now, with 391 tests green.** This is not a new bug discovered by this plan; it is a stale revert whose premise expired, and it is the single most valuable thing the first run will confirm.
- **protoCore's own `GCSurvivorRechainTests.cpp:145` contains the vacuous assertion shape** this phase forbids: `EXPECT_GT(freeAfter, freeBefore)` after allocating exactly one cell. It is not wrong for what T2 tests, but it is the pattern to avoid, and Task 9 Step 6 adds a comment there pointing at the conformance library's proportional helper so the next GC test written in protoCore does not copy it.
- **No runtime in the family calls `ProtoSpace::setHeapLimits`.** Grep over all five `src/` and `test/` trees returns nothing. With `maxHeapSize == 0` the ceiling is disabled and `waitForHeapHeadroom` returns immediately (`core/ProtoSpace.cpp:1495`), so **rule 8 is currently unreachable in every runtime as configured** — which means the P2 finding is latent, not fixed, and the conformance case must configure its own ceiling to reach it (Task 6).
- **No runtime installs an `outOfMemoryCallback`** (`headers/protoCore.h:1995`). It is the one recovery hook before `std::abort()` (`core/ProtoSpace.cpp:1536-1543`). Whether a runtime should install one is a design question for P5, not this phase, but the static check reports its absence so the question is on the record.
- **protoST's `Bootstrap.sym.*` WKS-cache slips are not rule-4 violations.** Six sites in `object_prims.cpp` (`:229`, `:296`, `:336`, `:451`, `:585`, `:693`) call `createSymbol(ctx, "__class_name__")` directly instead of using `sym.className`. They use the **correct** function, so the key is correct and nothing is silent; the cost is a `SymbolTable::intern` lookup on a hot path. That is a performance and consistency finding, reported by the static checker at severity `info`, and it is **not** a conformance failure. Saying otherwise would be the kind of overclaim that gets a suite ignored.

---

## File Structure

New in **protoCore**:

| Path | Kind | Purpose |
|---|---|---|
| `conformance/CMakeLists.txt` | new | builds `protoCoreConformance`, installs it, adds it to `protoCoreTargets` |
| `headers/protoCoreConformance.h` | new, installed | `proto::conformance::Host`, `CaseResult`, `Selector`, `runAll`, `runOne`, `caseIds` |
| `headers/protoCoreConformanceGTest.h` | new, installed | 3-line GoogleTest adapter (`PROTOCORE_CONFORMANCE_GTEST(HostType)`) |
| `headers/protoCoreConformanceCatch2.h` | new, installed | the same for Catch2, for protoST |
| `conformance/Runner.cpp` | new | `runAll`/`runOne`, result formatting, `PROTOCORE_CONFORMANCE_*` env handling |
| `conformance/CycleDriver.cpp` / `.h` | new (internal) | the proportional-reclamation helper: drive cycles to convergence, measure, and refuse a vacuous assertion |
| `conformance/CaseGC.cpp` | new | `gc.young_submitted`, `gc.transient_reclaimed`, `gc.host_stress` |
| `conformance/CaseThreads.cpp` | new | `stw.quorum_completes`, `thread.registered` |
| `conformance/CaseHeap.cpp` | new | `heap.ceiling_progress` (isolated-process case) |
| `conformance/CaseExternal.cpp` | new | `external.perennial_never_finalized` |
| `conformance/CaseModules.cpp` | new | `module.root_survives_cycle`, `module.alias_rejected` (P3-gated) |
| `conformance/main.cpp` | new | `protocore-conformance-isolate`, the one-case-per-process runner rule 8 needs |
| `scripts/conformance/check_static.py` | new | every **S** check, the allowlist ratchet, JSON and text output |
| `scripts/conformance/mutate.py` | new | applies a named mutation and asserts the named case goes red (rule 10) |
| `test/ConformanceSelfCheckTests.cpp` | new | the `NonConformingHost` matrix — every case must fail for a host built to break it |
| `docs/EMBEDDER-CONFORMANCE.md` | new | the rule table as normative text, the per-function sentinel table, the **J** checklist |

Modified in **protoCore**: `CMakeLists.txt` (add `add_subdirectory(conformance)`, version `2.3.0`), `docs/GarbageCollector.md` (§7 cross-reference), `docs/MemoryModel.md` (§5 cross-reference), `docs/TESTING.md` (a "Conformance" section), `CHANGELOG.md`, `test/ConcurrentMarkSafetyTests.cpp` (a warning comment at `:77`, no behaviour change).

New in **each runtime** (`<R>` ∈ protoScala, protoST, protoClojure, protoPython, protoJS):

| Path | Purpose |
|---|---|
| `<R>/tests/ConformanceHost.h` / `.cpp` (path per repo convention) | the runtime's `Host` implementation |
| `<R>/tests/ConformanceTests.cpp` | the 3-line framework adapter |
| `<R>/conformance-allow.txt` | the **S** ratchet: one justified line per known site |
| `<R>/docs/CONFORMANCE.md` | the **J** answers, dated and signed, plus the recorded first-run result |

**Interfaces (whole phase):**
- Consumes: `ProtoContext::safepoint`, `::UnmanagedScope`, `::allocatedCellsCount`, `::fromExternalPointer`, `ProtoSpace::triggerGC`, `::setHeapLimits`, `::waitForHeapHeadroom`, `::createRootSet`/`destroyRootSet`, `ProtoRootSet::add`/`remove`/`resolve`/`size`, `ProtoSpace::newThread`, `heapSize`, `freeCellsCount`, `gcCycleCount`, `reclaimedLastCycle`, `liveCellsLastCycle`, `getGCCycleCount()`, `ProtoString::createSymbol`, and — P3 only — `ProtoSpace::registerModule`/`findModule`/`addModuleRoot` and `ModuleIdentity`.
- Produces: `libprotoCoreConformance.a`, the imported target `protoCore::conformance`, four installed headers, two scripts, one normative document, and five per-runtime adaptors.
- **Adds no member, no virtual and no return-convention change to any existing protoCore class.**

---

### Task 1: The gate, the baselines, and the predictions — written before a single case exists

**Files:** none modified in any repository. Produces only files under `/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance/`.

**Interfaces:** consumes each runtime's existing build and `ctest`. Produces `baseline-<runtime>.txt` for all six, `predictions.md`, and a go/no-go note.

This task exists because **the phase's deliverable is a trustworthy baseline, and a baseline taken after the cases are written is worth nothing.** It also exists because five of the six numbers in the Global Constraints table are second-hand and one of them is known to disagree with its own source (Step 3).

- [ ] **Step 1: The gate — is the family quiet, and has P3 landed?**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance; mkdir -p $S
for R in protoCore protoScala protoST protoClojure protoPython protoJS; do
  D=/home/gamarino/Documentos/proyectos/$R
  printf '%-14s %-28s %s\n' "$R" "$(git -C $D rev-parse --abbrev-ref HEAD)" \
    "$(test -z "$(git -C $D status --porcelain)" && echo CLEAN || echo DIRTY)"
done | tee $S/gate.txt
grep -m1 '^    VERSION' /home/gamarino/Documentos/proyectos/protoCore/CMakeLists.txt | tee -a $S/gate.txt
```
**Done when** `gate.txt` shows every repository `CLEAN`, the maintainer has confirmed in writing that no sibling agent is building, and the protoCore version line is recorded. If that version is `2.1.0`, **P3 has not landed**: continue, but Task 15 is written and registered as `Skipped`, and Task 17 Step 4 records that rule 9 is unverified. If it is `2.2.0` or later, Task 15 runs. **Do not proceed on a DIRTY tree** — an ABI-mismatched stale binary links happily and lies (memory: ABI mismatch crashes stale binaries).

- [ ] **Step 2: protoCore's own baseline, from clean**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
git -C $P switch -c feature/embedder-conformance-p4
rm -rf $P/build_p4 && cmake -S $P -B $P/build_p4 -DCMAKE_BUILD_TYPE=Release
cmake --build $P/build_p4 --target protoCore proto_tests -j4
ctest --test-dir $P/build_p4 -j4 --output-on-failure < /dev/null 2>&1 | tail -20 | tee $S/baseline-protoCore.txt
```
**Done when** `baseline-protoCore.txt` reports **438/438**. Note the `< /dev/null`: it is applied to every `ctest` in this plan, not only protoST's, because the runner built in Task 2 Step 7 must behave identically everywhere and a habit that has an exception is not a habit (S18).

- [ ] **Step 3: Every runtime's baseline, recording the number actually observed — not the number expected**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
run() {  # run <name> <dir> <builddir> <extra-cmake-build-args...>
  R=/home/gamarino/Documentos/proyectos/$1; B=$R/$3; shift 3
  rm -rf $B && cmake -S $R -B $B -DCMAKE_BUILD_TYPE=Release && cmake --build $B "$@"
  ctest --test-dir $B --output-on-failure < /dev/null 2>&1 | tail -25
}
run protoScala   . build_release -j4  2>&1 | tee $S/baseline-protoScala.txt
run protoST      . build         -j4  2>&1 | tee $S/baseline-protoST.txt
run protoClojure . build_release -j4  2>&1 | tee $S/baseline-protoClojure.txt
run protoPython  . build_release -j4  2>&1 | tee $S/baseline-protoPython.txt
# protoJS: NO -j, ever, and no test262 here.
R=/home/gamarino/Documentos/proyectos/protoJS
rm -rf $R/build_p4 && cmake -S $R -B $R/build_p4 -DCMAKE_BUILD_TYPE=Release && cmake --build $R/build_p4
ctest --test-dir $R/build_p4 --output-on-failure < /dev/null 2>&1 | tail -25 | tee $S/baseline-protoJS.txt
```
**Done when** all five files exist and each records its number. Expected: protoST **854/854**, protoClojure **391/391**, protoPython **582/583** with `protopy_import_site` the only failure, protoJS **34/34**.

**protoScala needs its discrepancy resolved here, not later.** The Global Constraints table says 1248/1248, but `protoScala/docs/STATUS.md:19-33` records **1247 total** from `ctest -N` — 370 unit + 841 conformance + 24 CLI + 12 benchmark smoke — and calls `umd/protost-interop` "the 1248th case", built only when protoST is found beside the tree (`STATUS.md:33-36`). `STATUS.md:1172,1178` further records a Track F low-heap sweep at **1247/1248 with one pre-existing failure**, `Mailbox.EightProducersLoseNothingAndDuplicateNothing`. So: record `ctest -N` and the pass count separately, state whether `umd/protost-interop` was built, and if the Mailbox test fails, attribute it to Track F and **not** to this phase. A baseline whose total depends on whether a sibling repository is present must say so, or every later comparison is noise.

- [ ] **Step 4: Confirm the S18 hazard before it costs fourteen minutes**

```bash
R=/home/gamarino/Documentos/proyectos/protoST
sed -n '1,40p' $R/tests/unit/test_debugger.cpp
grep -rn 'setInputStream' $R/tests/unit/test_debugger.cpp $R/src/debugger/DebuggerRuntime.cpp
grep -n -i 'S18\|stdin\|/dev/null' $R/docs/STATUS.md | head
timeout 60 ctest --test-dir $R/build -R debugger --output-on-failure < /dev/null; echo "exit=$?"
```
**Done when** the step's report states: that the first case in `tests/unit/test_debugger.cpp` omits `setInputStream` where its three siblings supply it, so it falls through to the real `std::cin` (`src/debugger/DebuggerRuntime.cpp:75`); that `docs/STATUS.md` documents the resulting ~14-minute hang and the `< /dev/null` workaround; and that the `timeout 60` run above **exits 0** with stdin closed. **This is a hazard the suite runner must handle, not a bug this phase fixes** — fixing `test_debugger.cpp` is protoST's to do, and Task 12 Step 5 files it as a finding. What P4 owns is that no conformance invocation anywhere can hang on it.

- [ ] **Step 5: Census every rule's static surface, so the ratchet starts from measured reality**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
for R in protoScala protoST protoClojure protoPython protoJS; do
  D=/home/gamarino/Documentos/proyectos/$R
  echo "=== $R ==="
  for pat in 'createSymbol' 'fromUTF8String' 'safepoint' 'UnmanagedScope' \
             'ProtoTuple\|newTuple' 'fromExternalPointer\|newExternalBuffer' \
             'CriticalSection' 'setHeapLimits' 'outOfMemoryCallback' 'createRootSet'; do
    printf '  %-40s %s\n' "$pat" "$(grep -rn --include=*.cpp --include=*.h -e "$pat" $D/src 2>/dev/null | wc -l)"
  done
done | tee $S/census.txt
```
**Done when** `census.txt` exists and its numbers are compared against the values this plan predicts in Step 6. The two that must be zero everywhere and are expected to be zero everywhere are `setHeapLimits` and `outOfMemoryCallback`; if either is non-zero, Task 6's prediction changes and Task 17 Step 2 records why.

- [ ] **Step 6: Write the predictions to disk — before any case exists**

Write the table below verbatim to `$S/predictions.md`, then `git -C /home/gamarino/Documentos/proyectos/protoCore add` nothing (it is scratch, deliberately: it must not be editable after the fact by the same commit that reports results). Task 17 Step 2 diffs the observed results against this file.

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
sed -n '/^#### The prediction table/,/^#### End of predictions/p' \
  /home/gamarino/Documentos/proyectos/protoScala/docs/plans/2026-09-25-phase-p4-embedder-conformance.md \
  > $S/predictions.md
sha256sum $S/predictions.md | tee $S/predictions.sha256
```

#### The prediction table

Legend: **P** pass · **F** fail (predicted, with the reason) · **N** not applicable, reported as such and never as a pass · **?** genuinely unknown — the row this plan expects to *learn* something from · **i** pass with informational findings · **J** judgement item, recorded not scored. Confidence is **high** where a source line was read for this plan, **med** where it follows from a documented commit or memory note, **low** where it is inference.

| Rule | protoCore (SelfHost) | protoScala | protoST | protoClojure | protoPython | protoJS |
|---|---|---|---|---|---|---|
| 1 `gc.young_submitted` | **P** high | **P** high — `ActorScheduler.cpp:488`, `ExecutionEngine.cpp:1094` | **P** high — 5 sites incl. `ExecutionEngine.cpp:430,441,1797` | **F** high — **no safepoint anywhere**; added in `1bcc433`, reverted in `dde94c8` on a premise that has expired | **P** high — `ExecutionEngine.cpp:3981`, every 64 opcodes | **P** high — `ProtoInterpreter.cpp:6712`, every 1024 dispatches |
| 2 `stw.quorum_completes` | **P** high | **P** high — every wait wrapped (`ActorScheduler.cpp:490,504`, `Futures.cpp:267`, `ActorPrimitives.cpp:462`) | **P** high — `STRuntime.cpp:871-874` wraps `workerSem.acquire()` | **F** high — idle wait fixed (`ActorScheduler.cpp:187-189`) but **three bare joins remain**: `ActorScheduler.cpp:76-79`, `Primitives.cpp:1598`, `:1925`, never re-wrapped after the `2bb3b8d` revert | **F** high — 17 `UnmanagedScope` uses, but `src/runtime/main.cpp:284,366,386` poll `runningThreads` with `usleep(50000)` for up to **5 s at shutdown** with no scope, stalling STW that whole time; plus `PythonEnvironment.cpp:23715` | **F** med — 10 correct sites, but `src/JSContext.cpp:200-201` joins `CPUThreadPool`/`IOThreadPool` unwrapped (teardown only) |
| 3 `gc.host_stress` (R) | **P** high | **P** med — `Mailbox.cpp:50-58` roots the snapshot before `appendLast` | **P** med — `TransientPin::reset` replaces the `unique_ptr` shape | **P** med — `ActorMessage` deleted in `a6b722d`; payloads are a `ProtoList` in a rooted `ProtoMPSCQueue` | **?** med — disciplined, with explicit GC-discipline comments (`ExecutionEngine.cpp:8812`), but ~30k lines unswept | **F** med — `src/WorkerThreadsModule.cpp:241-283` (`parseArray`/`parseObject`) holds `keyObj`/`v`/`els` across unbounded recursive `parseValue()` allocation, and protoJS uses `pendingRoot` **nowhere** |
| 3 `local_across_alloc` (S) | **P** | **i** med | **i** med | **i** med | **i** med | **i** high — shape B expected to fire on the JSON parser |
| 4 `symbol_key_source` | **P** | **P** high — **zero** `fromUTF8String` calls; the one hit is a warning comment at `BytecodeModule.cpp:288` | **i** high — ~9 `fromUTF8String` sites to allowlist, plus **6 info-level WKS slips** in `object_prims.cpp` (`:229,296,336,451,585,693`) which are *not* violations (§D0) | **P** high — 23 sites, all non-key (`Primitives.cpp:618,1858,2034`, `ExecutionEngine.cpp:93`, `Reader.cpp:80`) | **i** high — 875 `createSymbol` against only 8 `fromUTF8String`; the one key-shaped site is `src/compiler/CppGenerator.cpp:351`, which keys AOT kwargs by `fromUTF8String(kw)->getHash(ctx)` — hash-keyed, so a perf departure, not a mismatch | **F-or-i** high — **1367 `fromUTF8String` sites, 248 key-shaped**, against 97 `createSymbol`. **Which it is depends entirely on §D11**: residual `__`-prefixed internal keys at `ProtoInterpreter.cpp:448,802,1021,2034,2087,3852,5758,6993,7348,7715,7790` and the `"__pd_<Name>__"` family (`:4016,4039,4146,5089`). Helper `ensureInterned()` (`:353-433`) with a `thread_local` cache already exists |
| 5 `gc.transient_reclaimed` | **P** | **P** high — no `ProtoTuple` in `src/`, only 5 enforcing comments | **P** high — no `newTuple`; one `asTuple` read at `ValueFormat.cpp:70` | **P** high — Track C `90d3d86` moved vectors to a `ProtoSparseList` box over one `ProtoList` | **F** high — **the finding the maintainer's list did not predict.** `src/compiler/CppGenerator.cpp:356` builds a fresh positional-args `ProtoTuple` via `newTupleFromList` **on every AOT-compiled call site**; `OP_BUILD_TUPLE` and `OP_LIST_TO_TUPLE` allocate per execution; `*args` binding allocates one per call (`ExecutionEngine.cpp:604-609`). A Python tuple **is** a user-visible sequence, so `makeSequenceGarbage` hits this directly | **P** high — 7 hits, one construction site (`src/ProtoCoreModule.cpp:511-527`), a low-frequency user-facing factory |
| 6 `attr_sentinel` | **P** | **P** high — 53 `PROTO_NONE` comparisons, 0 bare-`nullptr` sites, plus a warning comment at `Values.cpp:157` | **P** high — 19 sites, all also checking `PROTO_NONE` | **P** high — the one site (`Primitives.cpp:130-132`) is `getOwnAttributeDirect`, where `nullptr` is **correct** | **F** high — `src/library/PythonEnvironment.cpp:11427`, `py_bytes_find`: `getAttribute(ctx, "__data__") == nullptr` is **dead code**, so `bytes.find()` does not short-circuit as intended | **i** high — correct everywhere, but the comment at `src/runtime/ProtoInterpreter.cpp:8438` states the convention **inverted** ("nullptr=absent, PROTO_NONE=undefined"); it works today only because a prior guard excludes invalid input |
| 7 `external_finalizer` (S) | **P** | **N** high — zero external wrappers | **J** high — **3 sites all passing `nullptr`** (`DapServer.cpp:257`, `STRuntime.cpp:671`, `ExecutionEngine.cpp:2452-2453`); whether each is correct is checklist C7, not a script's call | **N** high — zero external wrappers | **P** high — 13 `fromExternalPointer` sites, every finalizer a trivial `delete static_cast<T*>(ptr)` on a POD | **F** high — **the highest-value mechanical catch in the phase.** Four finalizers both **block** and **call protoCore**: `src/HTTPModule.cpp:120-136` (`thread.join()` + `rs->remove()`), `:731-741` (`rs->remove()`), `src/NetModule.cpp:213-232`, `src/WorkerThreadsModule.cpp:357-368`. Plus `src/GCBridge.cpp:27-32` calls QuickJS `JS_FreeValue` from the GC thread |
| 7 `external.perennial_never_finalized` (R) | **P** | **N** | **P** — characterisation only; asserts the documented trap | **N** | **P** | **P** |
| 8 `heap.ceiling_progress` | **P** | **?** **high-value** — the P2 finding was found *in protoScala's mailbox*; it has producers and a consumer and **never calls `setHeapLimits`**, so the case is the first thing ever to reach the wait | **?** high-value — worker pool + `workerSem` | **?** high-value — actor scheduler, 3 priority bands | **?** | **?** |
| 9a/9b/9c (P3) | **P** | **?** | **?** | **?** | **?** — the heaviest `moduleRoots` user (P3 §D12: 8 writers, 3 readers) | **?** |
| 10 `selfcheck.*` | **P** — this is where the matrix lives | n/a | n/a | n/a | n/a | n/a |
| 11 `thread.registered` | **P** | **?** **high-value** | **?** **high-value** | **?** **high-value** | **P** high — Python-visible threads go through `space->newThread` (`src/library/ThreadModule.cpp:424,698`); no raw spawning | **?** high — **two sound shapes, and the check must accept both** (§D12): GC-visible threads use `newThread` (`Deferred.cpp:421`, `ProtoCoreNativeBindings.cpp:171`, `ProtoCoreModule.cpp:649`), while pool and accept threads hold **no** `ProtoObject*` and hand off through `EventLoop::enqueueCallback`, and `WorkerThreadsModule` workers each own an independent `ProtoSpace` (`JSContext.h:234`) |
| 12 `critsec_across_block` | **P** | **P** med — no `CriticalSection` in `src/` | **?** low — `STModuleProvider.cpp` | **?** low — `Primitives.cpp` | **?** low — `PythonEnvironment.cpp` | **?** low — 3 files |

**The eight predictions this plan stakes itself on**, in descending order of confidence:

1. **protoClojure fails rule 1.** It has no safepoint at all and 391 green tests. If this passes, either the revert was reinstated or `PROTOCORE_GC_REINCLUDE_SURVIVORS` is off in its build — and either answer is worth knowing.
2. **protoClojure fails rule 2** at its three unwrapped joins.
3. **protoScala and protoST pass rules 1–6 cleanly.** They are the two runtimes that have already been through this, so if either fails a rule it already fixed, the suite is measuring the wrong thing and Task 9's self-check is what must be believed over the result.
4. **Rules 8 and 11 are unknown for all five, and that is the point.** Rule 8 is *unreachable today* in every runtime because none sets a heap ceiling, and rule 11 has never been checked by anyone. These two rows are where the phase earns its cost, and a clean sweep on both would be the most surprising outcome available.
5. **protoPython fails rule 5, and the maintainer's list did not predict it.** Its AOT call path and its `BUILD_TUPLE`/`LIST_TO_TUPLE`/`*args` opcodes build a fresh `ProtoTuple` per call or per execution, and every one of those nodes is interned and perennial. This is the strongest argument for §D5's reclassification: a **grep** census would have found `ProtoTuple` in protoPython and had no way to say that 519 hits contain both a legitimate perennial use (bootstrap MRO tuples, code-object metadata) and a per-call transient one. A **measurement** separates them without an opinion.
6. **protoJS fails rule 7 on four finalizers that both block and call protoCore.** `HTTPModule.cpp:120-136` calls `thread.join()` *and* `rs->remove()` from inside a finalizer; three more do the same. The contract forbids both absolutely — a finalizer *"never allocates cells, never publishes to a shared structure with compare-and-swap ... and never dereferences other `ProtoObject*`"* and *"must also not block: it runs on the single GC thread inside the sweep, so a wait there stalls collection for the whole space"* (`docs/GarbageCollector.md` §7). This is the most valuable purely-static catch in the phase, and it argues that rule 7's finalizer-body check should ship before anything else if the phase has to be cut short.
7. **protoPython fails rule 6 on a real dead-code bug** (`PythonEnvironment.cpp:11427`) and **protoJS carries an inverted comment** about the convention (`ProtoInterpreter.cpp:8438`). Rule 6 is the cheapest rule in the set and it still found a live defect in the largest runtime, which is the argument for keeping cheap checks even when their theoretical value is low.
8. **protoJS's rule-4 verdict is undecided until §D11 is answered**, and this is the one place where the plan cannot predict because it does not know the kernel's behaviour well enough. 248 key-shaped `fromUTF8String` sites is either the largest finding in the family or a performance note, and nothing in between.

**What would make this plan wrong, stated in advance:** if protoST or protoScala fails rule 1 or 2, the cases are over-strict. If protoClojure *passes* rule 1, the case is vacuous and Task 9's `selfcheck.no_safepoint_host` must be re-examined before anything else is believed. If every runtime passes rules 8 and 11, check that `forEachThreadKind` and `runProducerConsumer` are actually implemented and not silently returning `false` — a `NotApplicable` reported as a pass is the exact defect this phase exists to end.

#### End of predictions

**Done when** `$S/predictions.md` exists, its sha256 is recorded, and the maintainer has seen the five staked predictions. **No case may be written before this file exists.**

- [ ] **Step 7: Commit the branch point (no source change)**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P commit --allow-empty -m "chore(conformance): branch point for P4, baselines recorded

Baselines and predictions recorded under
.agent_scratch/p4-conformance/ before any conformance case exists, so the
first run's result is a comparison and not a discovery.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```
**Done when** `git -C $P log --oneline -1` shows the empty commit and `git -C $P status --porcelain` is empty.

---

### Task 2: `libprotoCoreConformance` — the framework-free library, the `Host` adaptor and the runner

**Files:** create `protoCore/headers/protoCoreConformance.h`, `protoCore/conformance/CMakeLists.txt`, `protoCore/conformance/Runner.cpp`, `protoCore/conformance/CycleDriver.h`, `protoCore/conformance/CycleDriver.cpp`, `protoCore/conformance/main.cpp`, `protoCore/headers/protoCoreConformanceGTest.h`, `protoCore/headers/protoCoreConformanceCatch2.h`. Modify `protoCore/CMakeLists.txt`.

**Interfaces:**
- Consumes: `ProtoSpace` (`heapSize`, `freeCellsCount`, `gcStarted`, `gcCV`, `globalMutex`, `reclaimedLastCycle`, `liveCellsLastCycle`, `getGCCycleCount()`, `triggerGC`, `setHeapLimits`), `ProtoContext` (`safepoint`, `allocatedCellsCount`, `thread`), `ProtoRootSet`.
- Produces: the target `protoCoreConformance` (static), the imported name `protoCore::conformance`, four installed headers, and the executable `protocore-conformance-isolate`.

- [ ] **Step 1: The public header — capabilities, results, and nothing that names a runtime**

Create `protoCore/headers/protoCoreConformance.h`:

```cpp
/*
 * protoCoreConformance.h — the embedder conformance suite (P4).
 *
 * protoCore states its participation obligations once, and this library is
 * that statement in executable form.  Every runtime built on protoCore runs
 * the same cases through an adaptor it implements itself.
 *
 * Two deliberate properties, both load-bearing:
 *
 *  1. FRAMEWORK-FREE.  Cases return results as data; the embedder's own test
 *     framework does the asserting.  protoCore's GoogleTest is a FetchContent
 *     download with INSTALL_GTEST OFF (test/CMakeLists.txt), so it is not
 *     available to a consumer of the installed package -- and protoST uses
 *     Catch2, not GoogleTest, so imposing one framework was never an option.
 *
 *  2. protoCore NEVER NAMES A RUNTIME.  Host declares capabilities, not
 *     runtimes.  A case that needs something a runtime cannot supply reports
 *     NotApplicable with the capability's name, NEVER Pass.  A silent pass is
 *     the defect this whole library exists to end.
 *
 * See docs/EMBEDDER-CONFORMANCE.md for the normative rule table.
 */
#ifndef PROTO_CORE_CONFORMANCE_H
#define PROTO_CORE_CONFORMANCE_H

#include "protoCore.h"

#include <string>
#include <vector>

namespace proto { namespace conformance {

/**
 * @brief What a case needs from the runtime under test.
 *
 * Every method except mainContext() and makeGarbage() has a default that
 * reports the capability as unavailable.  Implement what you can honestly
 * supply; the runner reports the rest as NotApplicable and says which
 * capability was missing.
 */
class Host
{
public:
    virtual ~Host() = default;

    /** The context the runtime's main thread evaluates in.  REQUIRED. */
    virtual ProtoContext* mainContext() = 0;

    /** A short, stable name for this runtime, used only in reports. */
    virtual const char* name() const = 0;

    /**
     * Make the runtime allocate at least `requestedCells` cells of data that
     * is UNREACHABLE when this call returns, using the runtime's own
     * evaluator rather than direct protoCore calls.  REQUIRED.
     *
     * @return the number of cells the runtime actually allocated, measured by
     *         the runtime itself.  This number is the denominator of every
     *         proportional reclamation assertion, so a Host that cannot
     *         measure it MUST return 0 -- which turns the assertion into
     *         NotApplicable instead of a vacuous pass.  Do not guess.
     */
    virtual unsigned long makeGarbage(unsigned long requestedCells) = 0;

    /**
     * Same, but built out of the runtime's own SEQUENCE type -- what a user
     * gets from a literal vector, list or array.  Rule 5's probe.  It
     * deliberately does not mention ProtoTuple: the case measures
     * reclamation, so it is correct whatever representation was chosen.
     */
    virtual unsigned long makeSequenceGarbage(unsigned long requestedCells)
    { (void) requestedCells; return 0; }

    /**
     * Intern `text` the way this runtime interns an ATTRIBUTE KEY, and return
     * the pointer it would actually use as that key.  Rule 4's runtime probe.
     */
    virtual const ProtoObject* internAttributeKey(const char* text)
    { (void) text; return nullptr; }

    /** One kind of OS thread the runtime creates. */
    struct ThreadKind
    {
        const char*  name;            ///< e.g. "actor-worker"
        bool         blocksWhenIdle;  ///< true when it waits rather than spins
    };

    /** Called ON the spawned thread, with the context that thread uses. */
    using ThreadBody = void (*)(void* user, const ThreadKind& kind, ProtoContext* ctx);

    /**
     * For each kind of OS thread this runtime creates: start one, run `body`
     * on it, and join it.  Rules 2, 11 and 12's probe.
     * @return false when the runtime cannot enumerate its thread kinds.
     */
    virtual bool forEachThreadKind(ThreadBody body, void* user)
    { (void) body; (void) user; return false; }

    /**
     * Run the runtime's own producer/consumer or actor workload for `units`
     * of work.  Rule 8's probe: the case sets a hard heap ceiling first, so
     * "it completed" is the whole assertion.
     * @return true when the workload completed.
     */
    virtual bool runProducerConsumer(unsigned long units)
    { (void) units; return false; }

    /**
     * Bytes the runtime allocated OUTSIDE protoCore and is accounting for
     * itself (MemoryModel.md section 5, "Count it").
     * @return (unsigned long) -1 when the runtime keeps no such total.
     */
    virtual unsigned long externalBytesAccounted() { return (unsigned long) -1; }

    /**
     * Load the same logical path from two different providers.  Rule 9c.
     * @return 1 when the runtime kept them distinct, 0 when it aliased them,
     *         -1 when the capability is unavailable.
     */
    virtual int loadSamePathTwoProviders() { return -1; }
};

enum class Status { Pass, Fail, NotApplicable, Skipped, NeedsReview };

struct CaseResult
{
    const char*  id;        ///< e.g. "gc.young_submitted"
    unsigned     rule;      ///< the rule number in EMBEDDER-CONFORMANCE.md
    Status       status;
    std::string  detail;    ///< ALWAYS carries the numbers measured, even on Pass
};

struct Selector
{
    /// Empty runs every case.  Otherwise, exact case ids.
    std::vector<std::string> ids;
    /// Cases whose failure mode is std::abort() are skipped in-process and
    /// must be run through protocore-conformance-isolate.  Defaults to true.
    bool skipAbortingCases = true;
};

/** Every case id this build knows, in a stable order. */
std::vector<const char*> caseIds();

/** The rule number a case belongs to, or 0 for an unknown id. */
unsigned ruleOf(const char* id);

/** True when this case's failure mode is a process abort (rule 8). */
bool isAbortingCase(const char* id);

/** Run one case.  Never throws; a case that would throw reports Fail. */
CaseResult runOne(Host& host, const char* id);

/** Run every case the selector admits. */
std::vector<CaseResult> runAll(Host& host, const Selector& sel = Selector{});

/** One line per result, for a log.  Always includes `detail`. */
std::string format(const std::vector<CaseResult>& results);

}}  // namespace proto::conformance

#endif  // PROTO_CORE_CONFORMANCE_H
```

**Done when** `g++ -std=c++20 -fsyntax-only -I protoCore/headers protoCore/headers/protoCoreConformance.h` succeeds and `grep -Ec 'protoST|protoJS|protoPython|protoClojure|protoScala' protoCore/headers/protoCoreConformance.h` prints `0`.

- [ ] **Step 2: `CycleDriver` — drive cycles to convergence, and make a vacuous assertion impossible to write**

Create `protoCore/conformance/CycleDriver.h` and `.cpp`. This is the single most important file in the phase: it is where `reclaimed > 0` is made unavailable.

```cpp
// CycleDriver.h -- forcing a cycle is not the same as submitting, and
// "reclaimed something" is not the same as "reclaimed the garbage".
//
// Track Y measured a forced cycle reclaiming 4-7 cells where the workload had
// created 205,120, and the test asserted `reclaimed > 0` and passed.
// protoCore's own GCSurvivorRechainTests.cpp:145 still asserts only
// EXPECT_GT(freeAfter, freeBefore), which a single cell satisfies.
//
// So this header offers NO "did it reclaim anything" call.  The only
// reclamation question it can answer takes the denominator as an argument.
#ifndef PROTO_CORE_CONFORMANCE_CYCLE_DRIVER_H
#define PROTO_CORE_CONFORMANCE_CYCLE_DRIVER_H

#include "../headers/protoCore.h"
#include <string>

namespace proto { namespace conformance {

/// The fraction of created garbage a conforming runtime must reclaim.
/// Chosen in Step 3 below; deliberately a named constant so a future
/// loosening is a visible diff and not a tweak inside an assertion.
inline constexpr double kReclaimFraction = 0.50;

struct CycleReport
{
    unsigned long heapBefore  = 0;   ///< space.heapSize before the workload
    unsigned long heapPeak    = 0;   ///< after the workload, before cycles
    unsigned long heapAfter   = 0;   ///< after cycles converged
    unsigned long freeBefore  = 0;
    unsigned long freeAfter   = 0;
    unsigned long reclaimed   = 0;   ///< sum of space.reclaimedLastCycle
    unsigned long liveLast    = 0;   ///< space.liveCellsLastCycle
    unsigned      cyclesRun   = 0;
    bool          converged   = false;  ///< false when the deadline hit first
};

/**
 * @brief Drive GC cycles on `space` until reclamation converges.
 *
 * Two interlocking concerns, both learned the hard way:
 *
 *  * triggerGC() is advisory -- it does nothing unless free cells are below
 *    20% (ProtoSpace::triggerGC, core/ProtoSpace.cpp).  A case that allocates
 *    a bounded amount must set gcStarted directly, which is what protoCore's
 *    own GC tests do (test/GCSurvivorRechainTests.cpp waitForGcCycles).
 *
 *  * STW cannot begin until parkedThreads >= runningThreads, and the calling
 *    thread is one of the running ones.  So the wait loop MUST call
 *    ctx->safepoint(), or the collector blocks forever waiting for us.  That
 *    is the same obligation rule 1 audits, which is worth noticing: the
 *    harness has to obey the rule it is measuring.
 */
CycleReport driveCycles(ProtoSpace& space, ProtoContext* ctx,
                        unsigned maxCycles, unsigned deadlineMs);

/**
 * @brief The ONLY reclamation verdict this library offers.
 *
 * @param createdCells what the workload reported allocating -- the
 *        denominator.  Pass 0 only when the Host could not measure it; the
 *        result then says so and the case reports NotApplicable.
 * @return an empty string when the report is consistent with `createdCells`,
 *         otherwise the failure message, which always includes both numbers.
 */
std::string checkProportionalReclaim(const CycleReport& r,
                                     unsigned long createdCells);

/// Human-readable, always emitted -- on pass as well as on fail.
std::string describe(const CycleReport& r, unsigned long createdCells);

}}  // namespace proto::conformance

#endif
```

The implementation of the verdict, in `CycleDriver.cpp` — the whole argument of rule 1 in twenty lines:

```cpp
std::string checkProportionalReclaim(const CycleReport& r, unsigned long createdCells)
{
    if (createdCells == 0) {
        return "denominator unavailable: the Host reported allocating 0 cells, "
               "so no proportional assertion is possible.  A Host that cannot "
               "measure its own allocation must return 0 (not a guess), and "
               "this case is NotApplicable rather than passed.";
    }
    if (!r.converged) {
        return "reclamation did not converge within the deadline: "
               + describe(r, createdCells);
    }
    const unsigned long required =
        (unsigned long) (kReclaimFraction * (double) createdCells);
    if (r.reclaimed < required) {
        return "reclaimed " + std::to_string(r.reclaimed) + " cells but the "
               "workload created " + std::to_string(createdCells) + "; at least "
             + std::to_string(required) + " was required.  A cycle that runs and "
               "reclaims a handful of cells is what an UNSUBMITTED young "
               "generation looks like: the chain is live by construction, so no "
               "cycle can ever consider it.  See rule 1 -- "
               "ProtoContext::safepoint() is the only submission point.  "
             + describe(r, createdCells);
    }
    return std::string();
}
```

**Done when** `grep -n 'reclaimed > 0\|reclaimed >= 1\|freeAfter > freeBefore' protoCore/conformance/*.cpp protoCore/conformance/*.h` returns nothing, and the header exposes no function that answers a reclamation question without a `createdCells` argument (verify by `grep -c 'createdCells' protoCore/conformance/CycleDriver.h` being ≥ 3).

- [ ] **Step 3: Fix `kReclaimFraction`, and write down why it is not 0.95**

Record this reasoning as a comment above the constant and in `docs/EMBEDDER-CONFORMANCE.md`:

- A conforming runtime does **not** reclaim 100% of the garbage a workload created, for three legitimate reasons that are all visible in `docs/GarbageCollector.md`: (a) the young chain submitted **after** Phase 2's `dirtySegments` exchange belongs to the *next* cycle (Phase 2, item 5), so the last batch always lags; (b) with `PROTOCORE_GC_REINCLUDE_SURVIVORS` and `survivorStagger > 1`, survivors re-enter the candidate set only every *n*-th cycle (Phase 2, item 6); (c) the driver's own allocations and the runtime's caches are live.
- A **non**-conforming runtime reclaims a rounding error: protoST reclaimed **0** of 2,748,398, and Track Y's helper reclaimed **4–7** of 205,120 — a ratio of 3 × 10⁻⁵.
- So the discriminating power of the threshold is enormous anywhere in `[0.05, 0.9]`, and `0.50` is chosen because it is (i) far above every observed failure, (ii) far below the lag any of (a)–(c) can cause for a workload of ≥ 50,000 cells, and (iii) a round number nobody will be tempted to tune per runtime. **Tuning it per runtime is forbidden**; if a runtime cannot reach 0.50 the finding is about the runtime.
- The case therefore always requests **at least 200,000 cells** of garbage, so that the fixed lag of (a) and (b) is a small fraction of the denominator.

**Done when** the constant, the three reasons and the 200,000-cell floor are all present in `CycleDriver.h` and the case files, and `grep -rn 'kReclaimFraction' protoCore/ | grep -v 'CycleDriver.h'` shows it is only *read*, never redefined.

- [ ] **Step 4: The runner — `NotApplicable` is never `Pass`**

In `Runner.cpp`, the dispatch table and the one rule that matters:

```cpp
std::vector<CaseResult> runAll(Host& host, const Selector& sel)
{
    std::vector<CaseResult> out;
    for (const char* id : caseIds()) {
        if (!sel.ids.empty() && !contains(sel.ids, id)) continue;
        if (sel.skipAbortingCases && isAbortingCase(id)) {
            out.push_back({id, ruleOf(id), Status::Skipped,
                "failure mode is std::abort(); run through "
                "protocore-conformance-isolate --case=" + std::string(id)});
            continue;
        }
        out.push_back(runOne(host, id));
    }
    return out;
}
```

and, in `format`, the line that makes a broken harness visible:

```cpp
    // Every result carries its numbers, on pass as well as on fail.  A run
    // whose details are all zeros is a broken harness that looks like a clean
    // suite -- which is the entire subject of this phase.
    for (const CaseResult& r : results) {
        s += statusName(r.status);
        s += "  rule ";  s += std::to_string(r.rule);
        s += "  ";       s += r.id;
        s += "  ";       s += r.detail;   // never empty, never omitted
        s += "\n";
    }
```

**Done when** `runOne` on an unknown id returns `Fail` (not `Skipped`), every `Status::NotApplicable` result carries the missing capability's method name in `detail`, and `format` emits `detail` unconditionally. Verified by Task 9 Step 3.

- [ ] **Step 5: CMake — one target that works both installed and as a subdirectory**

`protoCore/conformance/CMakeLists.txt`:

```cmake
# The conformance suite is built INSIDE protoCore so its cases can include
# headers/proto_internal.h, which is not installed (the package ships
# headers/protoCore.h alone).  Its public header uses only public types, so a
# consumer never needs the internal header.
add_library(protoCoreConformance STATIC
    Runner.cpp CycleDriver.cpp
    CaseGC.cpp CaseThreads.cpp CaseHeap.cpp CaseExternal.cpp CaseModules.cpp)

target_include_directories(protoCoreConformance
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../headers>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(protoCoreConformance PUBLIC protoCore)
set_target_properties(protoCoreConformance PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(PROTOCORE_GC_REINCLUDE_SURVIVORS)
    # The cases must know whether the submission path they audit exists in
    # this build; rule 1 is vacuous without it and says so rather than passing.
    target_compile_definitions(protoCoreConformance PRIVATE PROTOCORE_GC_REINCLUDE_SURVIVORS)
endif()

# One case per process, for the case whose failure mode is std::abort().
add_executable(protocore-conformance-isolate main.cpp)
target_link_libraries(protocore-conformance-isolate PRIVATE protoCoreConformance)

# Installed only when protoCore is the top-level project -- protoPython
# consumes protoCore by add_subdirectory and owns its own packaging
# (see the guard at the top-level CMakeLists.txt).
if(CMAKE_PROJECT_NAME STREQUAL "protoCore")
    install(TARGETS protoCoreConformance protocore-conformance-isolate
        EXPORT protoCoreTargets
        COMPONENT ${PROTOCORE_INSTALL_COMPONENT}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../headers/protoCoreConformance.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../headers/protoCoreConformanceGTest.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../headers/protoCoreConformanceCatch2.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT ${PROTOCORE_INSTALL_COMPONENT})
endif()
```

and in `protoCore/CMakeLists.txt`, immediately before the existing `if(CMAKE_PROJECT_NAME STREQUAL "protoCore")` install block:

```cmake
# P4: the embedder conformance suite.  Always built (it is how protoCore's own
# test suite runs the self-check matrix), installed only at top level.
add_subdirectory(conformance)
```

Add the alias so the in-tree and installed spellings match:

```cmake
add_library(protoCore::conformance ALIAS protoCoreConformance)
```

**Done when**
```bash
P=/home/gamarino/Documentos/proyectos/protoCore; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
cmake --build $P/build_p4 --target protoCoreConformance protocore-conformance-isolate -j4
cmake --install $P/build_p4 --prefix $S/prefix
test -f $S/prefix/lib/libprotoCoreConformance.a && test -f $S/prefix/include/protoCoreConformance.h && echo INSTALL-OK
grep -n 'protoCoreConformance' $S/prefix/lib/cmake/protoCore/protoCoreTargets.cmake | head
```
prints `INSTALL-OK` and the export file mentions the target. Note the `--prefix` into scratch: **never install to a system prefix** (Global Constraints).

- [ ] **Step 6: The three-line framework adapters**

`protoCore/headers/protoCoreConformanceGTest.h`:

```cpp
#ifndef PROTO_CORE_CONFORMANCE_GTEST_H
#define PROTO_CORE_CONFORMANCE_GTEST_H
#include "protoCoreConformance.h"
#include <gtest/gtest.h>

/**
 * Expands one GoogleTest case per conformance case, so a failure names the
 * rule it violated instead of hiding inside one aggregate test.  An
 * aggregate would also stop at the first failure, and the whole value of a
 * first run is the COMPLETE list.
 *
 * Usage, in the embedder's own test target:
 *     PROTOCORE_CONFORMANCE_GTEST(MyHost)
 * where MyHost is default-constructible and derives from
 * proto::conformance::Host.
 */
#define PROTOCORE_CONFORMANCE_GTEST(HostType)                                  \
    class HostType##Conformance                                                \
        : public ::testing::TestWithParam<const char*> {};                     \
    TEST_P(HostType##Conformance, ObeysRule) {                                 \
        HostType host;                                                         \
        const ::proto::conformance::CaseResult r =                             \
            ::proto::conformance::runOne(host, GetParam());                    \
        RecordProperty("detail", r.detail);                                    \
        SCOPED_TRACE(r.detail);                                                \
        EXPECT_NE(r.status, ::proto::conformance::Status::Fail) << r.detail;    \
        if (r.status == ::proto::conformance::Status::NotApplicable)           \
            GTEST_SKIP() << "capability unavailable: " << r.detail;            \
    }                                                                          \
    INSTANTIATE_TEST_SUITE_P(                                                  \
        conformance, HostType##Conformance,                                    \
        ::testing::ValuesIn(::proto::conformance::caseIds()),                  \
        [](const ::testing::TestParamInfo<const char*>& i) {                   \
            std::string n(i.param);                                            \
            for (char& c : n) if (!isalnum((unsigned char) c)) c = '_';        \
            return n;                                                          \
        });
#endif
```

`protoCoreConformanceCatch2.h` is the same shape with `TEST_CASE` + `GENERATE(from_range(...))` + `CHECK(...)` and a `SKIP(...)` on `NotApplicable`, for protoST.

**Done when** each header compiles standalone against its framework and the GoogleTest one produces one ctest entry per case id — verified in Task 11 Step 3 by `ctest -N -R conformance` listing exactly `caseIds().size()` entries.

- [ ] **Step 7: The runner script, with the S18 discipline built in**

Create `protoCore/scripts/conformance/run.sh`:

```bash
#!/usr/bin/env bash
# Run one embedder's conformance cases.  Two hazards are handled here, not by
# the caller, because a runner that hangs is indistinguishable from a
# conformance failure that hangs -- and rules 2, 8 and 11 are exactly the
# rules whose failure mode IS a hang.
#
#  * stdin is closed for every ctest invocation (protoST S18: a debugger test
#    reads the real std::cin and blocks for ~14 minutes).
#  * every invocation carries a hard timeout.
set -euo pipefail
BUILD_DIR=${1:?build dir}; JOBS=${2:-1}; TIMEOUT=${3:-300}
timeout --signal=INT "${TIMEOUT}" \
  ctest --test-dir "${BUILD_DIR}" -R '^conformance' -j"${JOBS}" \
        --output-on-failure --timeout 120 < /dev/null
```

**Done when** `bash -n protoCore/scripts/conformance/run.sh` passes, the script is executable, and `grep -c '< /dev/null' protoCore/scripts/conformance/run.sh` is `1`. Also assert the discipline is not bypassable: `grep -rn 'ctest' protoCore/scripts/conformance/ | grep -v '/dev/null'` must be empty.

- [ ] **Step 8: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add headers/protoCoreConformance.h headers/protoCoreConformanceGTest.h \
  headers/protoCoreConformanceCatch2.h conformance/ scripts/conformance/run.sh CMakeLists.txt
git -C $P commit  # "feat(conformance): framework-free Host adaptor, runner and cycle driver (P4)"
```
**Done when** the commit exists and `cmake --build $P/build_p4 --target protoCoreConformance -j4` succeeds from clean configure.

---

### Task 3: The GC cases — rules 1, 5 and 3's stress probe

**Files:** create `protoCore/conformance/CaseGC.cpp`.

**Interfaces:** consumes `Host::makeGarbage`, `Host::makeSequenceGarbage`, `Host::runProducerConsumer`, `CycleDriver`. Produces the cases `gc.young_submitted`, `gc.transient_reclaimed`, `gc.host_stress`.

- [ ] **Step 1: `gc.young_submitted` — rule 1**

```cpp
// Rule 1 -- the young generation must be submitted.
//
// A context's young chain reaches the collector only through
// ProtoContext::safepoint() (core/ProtoContext.cpp:363-389) or the context's
// destruction.  GC Phase 2 records a young chain as a ROOT HANDLE, not as a
// candidate: "the cells of a context's young chain are not candidates of the
// cycle and are never marked for it" (docs/GarbageCollector.md, Phase 2).  So
// an unsubmitted chain is live BY CONSTRUCTION -- no cycle can consider it.
//
// protoST ran cycles for its entire history, reclaimed 0 of 2,748,398 cells
// and passed 848 tests (corrected 2026-09-25 from 833, the suite size at the
// earlier S13 fix; source protoST 2d5bc6f).  This case would have said so.
static CaseResult caseYoungSubmitted(Host& host)
{
    const char* kId = "gc.young_submitted";
    ProtoContext* ctx = host.mainContext();
    if (!ctx || !ctx->space)
        return {kId, 1, Status::Fail, "Host::mainContext() returned no usable context"};
#ifndef PROTOCORE_GC_REINCLUDE_SURVIVORS
    // Say so rather than pass: without the flag there is no threshold
    // submission path to audit, and a green result would be a lie.
    return {kId, 1, Status::NotApplicable,
            "protoCore built without PROTOCORE_GC_REINCLUDE_SURVIVORS: "
            "ProtoContext::safepoint() has no submission path in this build, "
            "so rule 1 cannot be observed here"};
#else
    ProtoSpace& space = *ctx->space;
    const unsigned long kRequest = 200000;   // see Task 2 Step 3 for the floor

    const unsigned long before = space.heapSize;
    const unsigned long created = host.makeGarbage(kRequest);
    if (created == 0)
        return {kId, 1, Status::NotApplicable,
                "Host::makeGarbage() reported 0 cells: no denominator, so no "
                "proportional assertion is possible (see CycleDriver.h)"};

    CycleReport r = driveCycles(space, ctx, /*maxCycles=*/12, /*deadlineMs=*/20000);
    r.heapBefore = before;

    const std::string bad = checkProportionalReclaim(r, created);
    return {kId, 1, bad.empty() ? Status::Pass : Status::Fail,
            bad.empty() ? describe(r, created) : bad};
#endif
}
```
**Done when** the case compiles, and both of these hold: with protoCore's own `SelfHost` (Task 9) it returns `Pass` with a `detail` naming a reclaimed count ≥ 100,000; and with `NonConformingHost_NoSafepoint` it returns `Fail` with a message naming both numbers. The second half is Task 9 Step 2 and **this case is not complete until that mutation has been executed.**

- [ ] **Step 2: `gc.transient_reclaimed` — rule 5, measured rather than grepped**

Identical shape, calling `host.makeSequenceGarbage(200000)` instead. The comment carries the argument from §D5:

```cpp
// Rule 5 -- ProtoTuple is never used for transient data.
//
// Measured, not grepped.  Every TupleInterner entry is perennial: entries are
// never removed (headers/proto_internal.h:1088-1095) and the collector records
// only a published count for them under STW, dereferencing nothing
// (core/ProtoTuple.cpp:121-125).  So a runtime whose user-visible sequences are
// ProtoTuple has a heap that grows with every sequence ever built and never
// shrinks -- which is exactly what this measurement sees.
//
// This is strictly better than grepping for the type: it catches a runtime
// that reached the same trap through a different perennial structure, and it
// does NOT raise a finding against a runtime that uses ProtoTuple for
// something genuinely perennial.  protoClojure's vectors were the bug
// (Track C, commit 90d3d86, "protoCore interns every tuple node and never
// frees one"), and reclamation is a direct test of that sentence.
```
**Done when** the case returns `Pass` under `SelfHost` and `Fail` under `NonConformingHost_PerennialSequence`, a host whose `makeSequenceGarbage` builds `ProtoTuple`s (Task 9 Step 2).

- [ ] **Step 3: `gc.host_stress` — rule 3's runtime probe, the one that actually caught all three bugs**

```cpp
// Rule 3 (runtime half) -- no ProtoObject* held across an allocation only in a
// C++ local.  The general rule is NOT mechanisable (see EMBEDDER-CONFORMANCE.md
// checklist C3); what is mechanisable is the pressure that opens the window.
//
// All three instances met in this project -- protoClojure's ActorMessage
// payloads, protoScala's Mailbox::push CAS snapshot across appendLast, and
// protoST's MailboxCursor::adopt unique_ptr::reset -- were found by running the
// runtime's own queue and actor paths with the collector actually reclaiming.
// The third was UNREACHABLE until the first was fixed, which is why this case
// runs AFTER gc.young_submitted in caseIds() order and reports a dependency in
// its detail when rule 1 failed.
static CaseResult caseHostStress(Host& host)
{
    const char* kId = "gc.host_stress";
    ProtoContext* ctx = host.mainContext();
    ProtoSpace& space = *ctx->space;

    // A tight ceiling makes every batch trigger real reclamation, so the
    // window between a construction and its rooting is open on every
    // iteration instead of once in a thousand runs.
    const int savedSoft = space.softHeapLimit, savedHard = space.maxHeapSize;
    space.setHeapLimits(/*softCells=*/150000, /*hardCells=*/400000);

    unsigned long completed = 0;
    for (unsigned i = 0; i < 40; ++i) {
        if (!host.runProducerConsumer(2000)) break;
        ++completed;
        driveCycles(space, ctx, /*maxCycles=*/1, /*deadlineMs=*/2000);
    }
    space.setHeapLimits(savedSoft, savedHard);

    if (completed == 0)
        return {kId, 3, Status::NotApplicable,
                "Host::runProducerConsumer() unavailable -- rule 3's runtime "
                "probe needs the runtime's own queue or actor path"};
    return {kId, 3, completed == 40 ? Status::Pass : Status::Fail,
            "completed " + std::to_string(completed) + "/40 stress rounds under a "
            "400,000-cell ceiling with a forced cycle between rounds; "
            "run this case under AddressSanitizer -- a surviving use-after-free "
            "is silent without it"};
}
```
**Done when** the case is registered, and Task 11 Step 4 has run it under ASan against at least one runtime with `ASAN_OPTIONS=detect_leaks=0` and recorded the result. **A pass without an ASan run is not evidence** — that is the lesson of protoST's `MailboxCursor::adopt`, which 848 non-ASan tests could not reach.

- [ ] **Step 4: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add conformance/CaseGC.cpp && git -C $P commit  # "feat(conformance): rules 1, 5 and 3-stress (P4)"
```
**Done when** `cmake --build $P/build_p4 --target protoCoreConformance -j4` succeeds and `$P/build_p4/conformance/protocore-conformance-isolate --list | grep -c '^gc\.'` prints `3`.

---

### Task 4: The threading cases — rule 2 and the new rule 11

**Files:** create `protoCore/conformance/CaseThreads.cpp`.

**Interfaces:** consumes `Host::forEachThreadKind`, `ProtoSpace::newThread`, `ProtoContext::thread`, `ProtoContext::getCurrentThread`. Produces `stw.quorum_completes`, `thread.registered`.

- [ ] **Step 1: `thread.registered` — rule 11, and why it runs first**

```cpp
// Rule 11 -- every OS thread that holds a ProtoObject* must be a
// protoCore-REGISTERED thread, or everything it holds must be pinned in a
// ProtoRootSet.
//
// There is no registerThread() in protoCore.  runningThreads is incremented
// only in the ProtoThreadImplementation constructor (core/Thread.cpp:27), which
// runs only for a thread created through ProtoSpace::newThread
// (headers/protoCore.h:2011).  A ProtoContext built on a raw std::thread gets
// thread == nullptr unless it happens to run on space->mainThreadId
// (core/ProtoContext.cpp:66-76).
//
// Now read GC Phase 2: root collection walks space->threads and calls
// scanContexts on each registered thread's context chain, taking
// automaticLocals, returnValue, pendingRoot and the young-chain head
// (core/ProtoSpace.cpp:381-411).  A thread ABSENT from that list is never
// root-scanned, so its live objects are swept under it.
//
// Compare the failure modes.  Rule 2: the collector never finishes -- a hang,
// nothing corrupted.  Rule 11: the collector finishes and is WRONG -- a
// use-after-free at a distance.  Rule 11 is the more dangerous of the two and
// it is checked first.
static void probeThread(void* user, const Host::ThreadKind& kind, ProtoContext* ctx)
{
    auto* acc = static_cast<ThreadAccumulator*>(user);
    const bool hasContext  = (ctx != nullptr);
    const bool hasThread   = hasContext && ctx->thread != nullptr;
    const bool discoverable = hasContext &&
        ProtoContext::getCurrentThread(ctx) != nullptr;
    acc->record(kind.name, hasContext, hasThread, discoverable);
}
```
The verdict: every enumerated thread kind must report `hasThread && discoverable`. A kind that reports `hasContext && !hasThread` is a **Fail** whose `detail` names the kind and quotes the Phase-2 citation, because that is precisely the unscanned-roots shape.

**Done when** the case returns `Pass` for a `SelfHost` whose thread kind is created with `ProtoSpace::newThread`, and `Fail` naming the kind for `NonConformingHost_RawStdThread`, which creates a `ProtoContext` on a bare `std::thread` (Task 9 Step 2). Note in the case's comment that `protoCore/test/ConcurrentMarkSafetyTests.cpp:77` uses exactly that shape deliberately, with everything pinned — it is safe there and a trap to copy, and Task 16 Step 3 annotates it.

- [ ] **Step 2: `stw.quorum_completes` — rule 2**

The assertion is behavioural and has exactly one honest form: **with the runtime's idle-blocking threads running, a collection must still complete.**

```cpp
// Rule 2 -- every registered protoCore thread must park.  A thread that blocks
// does so inside ProtoContext::UnmanagedScope (headers/protoCore.h:1730-1744).
//
// STW Phase 1 "waits for all application threads to reach a parked state
// (allocation safepoints, explicit synchToGC calls, or threads inside
// UnmanagedScope)" (docs/GarbageCollector.md Phase 1).  A registered thread
// blocked on a condition variable outside an UnmanagedScope reaches none of
// the three, so parkedThreads never reaches runningThreads and NO CYCLE EVER
// COMPLETES.  protoClojure's idle actor worker on queueCv_ was this.
//
// The assertion cannot be "the thread is inside an UnmanagedScope" -- protoCore
// cannot see the embedder's call stack.  It is: with your idle threads up, does
// gcCycleCount advance?  That is the property that matters and the only one
// observable from here.
static CaseResult caseQuorumCompletes(Host& host)
{
    const char* kId = "stw.quorum_completes";
    ProtoContext* ctx = host.mainContext();
    ProtoSpace& space = *ctx->space;

    ThreadAccumulator acc;
    if (!host.forEachThreadKind(&holdIdle, &acc))
        return {kId, 2, Status::NotApplicable,
                "Host::forEachThreadKind() unavailable -- rule 2 needs the "
                "runtime's own threads to be idle while a cycle is requested"};

    // holdIdle left each kind's thread blocked in its normal idle wait and
    // signalled us; now demand a cycle and give it a generous deadline.
    const uint64_t before = space.getGCCycleCount();
    CycleReport r = driveCycles(space, ctx, /*maxCycles=*/2, /*deadlineMs=*/10000);
    acc.releaseAll();

    const uint64_t advanced = space.getGCCycleCount() - before;
    if (advanced == 0)
        return {kId, 2, Status::Fail,
                "gcCycleCount did not advance in 10 s while these thread kinds "
                "were idle: " + acc.kindList() + ".  STW Phase 1 is waiting for "
                "a registered thread that is blocked outside an UnmanagedScope "
                "(GarbageCollector.md Phase 1).  This is the protoClojure "
                "queueCv_ shape."};
    return {kId, 2, Status::Pass,
            "gcCycleCount advanced by " + std::to_string(advanced) +
            " with idle kinds: " + acc.kindList() + "; " + describe(r, 0)};
}
```
**Done when** the case returns `Pass` under `SelfHost` and `Fail` after 10 s under `NonConformingHost_BlocksOutsideUnmanaged`. **The deadline is the assertion**, so the case must also be registered with a ctest `TIMEOUT` above 10 s — Task 11 Step 3 sets `--timeout 120`.

- [ ] **Step 3: Make the timeout non-negotiable**

A hang is rule 2's and rule 8's signature, so a case that hangs *the runner* destroys the diagnosis. Register in `Runner.cpp`:

```cpp
// Every case carries its own internal deadline; nothing in this library waits
// without one.  Verified by the grep in this step's done-when.
```
**Done when** `grep -rn 'wait(\|acquire()\|join()' protoCore/conformance/*.cpp | grep -v 'wait_for\|wait_until\|deadline'` returns nothing — every wait in the library is bounded.

- [ ] **Step 4: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add conformance/CaseThreads.cpp && git -C $P commit  # "feat(conformance): rules 2 and 11 (P4)"
```
**Done when** the build succeeds and `protocore-conformance-isolate --list` includes `stw.quorum_completes` and `thread.registered`.

---

### Task 5: Rule 3's static half — the three shapes, and the honest admission about the rest

**Files:** create `protoCore/scripts/conformance/shapes/*.query` (three `clang-query` files) and the `local_across_alloc` check inside `scripts/conformance/check_static.py`.

**Interfaces:** consumes each embedder's `compile_commands.json` (from `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`). Produces three findings classes and, when `clang-query` is absent, a degraded regex mode that says it is degraded.

- [ ] **Step 1: State the limit before writing the check**

Write this into `docs/EMBEDDER-CONFORMANCE.md` §C3 and as the header comment of the shapes directory:

> Deciding *in general* whether a `ProtoObject*` is held across an allocation requires knowing, for an arbitrary call, whether it can allocate, and whether an arbitrary local is still live afterwards. That is escape analysis over the whole call graph. A checker that claimed to do it would have a false-negative rate nobody could estimate — and, being believed, would be **worse than the checklist it replaced**. So rule 3's general form is a judgement item (C3). What is mechanised here is the **three shapes this project actually met**, each of which is a syntactic pattern.

**Done when** the paragraph is present in both places and `grep -n 'escape analysis' protoCore/docs/EMBEDDER-CONFORMANCE.md` matches.

- [ ] **Step 2: Shape A — `unique_ptr::reset` on a pin holder (protoST `MailboxCursor::adopt`)**

`shapes/A-uniqueptr-reset-pin.query`:
```
set output diag
match cxxMemberCallExpr(
  callee(cxxMethodDecl(hasName("reset"))),
  on(expr(hasType(cxxRecordDecl(hasName("unique_ptr"))))),
  hasArgument(0, cxxNewExpr())
) bind "shapeA"
```
The finding's text must explain the mechanism, because the shape looks harmless: `std::unique_ptr::reset(new T(...))` **constructs the replacement before destroying the old one**, so a holder whose destructor releases a GC pin releases it *after* the new pin exists — or, in the failing order, a holder whose constructor allocates runs while the old pin is still counted and the new object is not yet rooted. protoST's fix was to make the pin a **by-value, constructor-allocated member** and call `TransientPin::reset()` on it (`src/runtime/TransientPin.h:156-159`, used at `src/runtime/STRuntime.cpp:1517-1532`), which has no such window.

**Done when** the query, run against protoST's `compile_commands.json`, returns **zero** matches (the bug is fixed) and, run against a fixture containing the old shape (`scripts/conformance/shapes/fixtures/shapeA_bad.cpp`), returns exactly one.

- [ ] **Step 3: Shapes B and C**

- **Shape B — a CAS snapshot used after an allocating call** (protoScala `Mailbox::push`). Match a `varDecl` initialised from a `load()`/`compare_exchange` result whose type is `ProtoObject*` or a `Proto*` pointer, followed in the same compound statement by a call to any of `newList|newString|newTuple|newObject|appendLast|setAttribute|fromUTF8String|createSymbol`, followed by a use of that var. This is a *heuristic* and is labelled `severity=warn`, not `error`, because the var may legitimately be rooted by an intervening `setAutomaticLocal` — which is exactly what protoScala's fix does (`src/runtime/Mailbox.cpp:50-58`: `scope.setAutomaticLocal(0, cur)` before `appendLast`). The check therefore **suppresses the finding when a `setAutomaticLocal`, `pendingRoot` assignment, `ProtoRootSet::add` or `CriticalSection` appears between the snapshot and the allocation**, and reports the suppression in verbose mode so a reviewer can see the reasoning.
- **Shape C — a raw `ProtoObject*` field on a struct pushed onto a lock-free structure** (protoClojure `ActorMessage`). Match a `recordDecl` with a `ProtoObject*` field and no `ProtoRootSet::Handle` field, whose instances are passed to an `atomic` `store`/`compare_exchange`. `severity=error`: a raw pointer in a lock-free structure is never rooted by anything, and protoClojure's answer was to delete the struct and make a message a `ProtoList` held in a rooted `ProtoMPSCQueue` (`src/runtime/ActorScheduler.cpp:82-93`).

**Done when** each shape has a `*_bad.cpp` fixture that matches and a `*_good.cpp` fixture that does not, and `python3 scripts/conformance/check_static.py --self-test-shapes` reports `3/3 shapes discriminate`.

- [ ] **Step 4: The degraded mode must announce itself**

When `clang-query` is not on `PATH`, the checker falls back to regexes and **must** emit, at the top of its output and into its JSON as `"shapes_mode": "degraded"`:

> `clang-query` not found; shapes A–C were matched by regex. This mode has false negatives and is **not** evidence. Install `clang-tools` and re-run before recording a conformance result.

**Done when** `PATH=/usr/bin:/bin python3 scripts/conformance/check_static.py --shapes-only --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["shapes_mode"])'` prints `full` on a machine with `clang-query` and `degraded` without it, and the human output carries the warning in both the header and the footer.

- [ ] **Step 5: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add scripts/conformance/shapes/ && git -C $P commit  # "feat(conformance): rule 3's three named shapes (P4)"
```
**Done when** `python3 $P/scripts/conformance/check_static.py --self-test-shapes` exits 0.

---

### Task 6: `heap.ceiling_progress` — rule 8, in its own process

**Files:** create `protoCore/conformance/CaseHeap.cpp` and `protoCore/conformance/main.cpp`.

**Interfaces:** consumes `ProtoSpace::setHeapLimits`, `waitForHeapHeadroom`, `outOfMemoryCallback`, `Host::runProducerConsumer`. Produces `heap.ceiling_progress` and the `protocore-conformance-isolate` executable.

- [ ] **Step 1: Why this case cannot run in-process**

```cpp
// Rule 8 -- no heap-ceiling wait may be reachable only by the thread that can
// free.  P2's finding: with the producers and the single consumer all parked in
// ProtoSpace::waitForHeapHeadroom and the GC idle, the only exit is the OOM
// abort.
//
// The failure mode is std::abort() (core/ProtoSpace.cpp:1548-1554):
//
//   "protoCore: heap hard limit %d cells reached; live set %lu cells,
//    last cycle reclaimed 0 -- out of memory"
//
// reached after two consecutive zero-reclaim cycles.  An abort takes the whole
// test binary with it, so this case MUST run one-per-process, which is what
// protocore-conformance-isolate is for.  isAbortingCase("heap.ceiling_progress")
// returns true and runAll() skips it in-process with that instruction in
// `detail` -- a Skipped that says how to run it, never a silent pass.
//
// Note what this case has to do that no runtime does: SET A CEILING.  No
// runtime in the family calls setHeapLimits, so maxHeapSize is 0,
// waitForHeapHeadroom returns immediately (core/ProtoSpace.cpp:1495) and the
// P2 finding is LATENT rather than fixed.  The case configures the ceiling
// itself, which is the only way to reach the code at all.
```

- [ ] **Step 2: The case, and the three outcomes it distinguishes**

```cpp
static CaseResult caseCeilingProgress(Host& host)
{
    const char* kId = "heap.ceiling_progress";
    ProtoContext* ctx = host.mainContext();
    ProtoSpace& space = *ctx->space;

    // Install the one recovery hook that exists before the abort, so that a
    // runtime which would have aborted instead reports a distinguishable
    // outcome.  The callback must not allocate; it only records.
    space.outOfMemoryCallback = &recordOomAndReturnNone;

    space.setHeapLimits(/*softCells=*/120000, /*hardCells=*/300000);
    const bool completed = host.runProducerConsumer(50000);

    if (!completed)
        return {kId, 8, Status::NotApplicable,
                "Host::runProducerConsumer() unavailable -- rule 8 needs the "
                "runtime's own producer/consumer topology"};
    if (g_oomCallbackFired)
        return {kId, 8, Status::Fail,
                "the workload reached the heap ceiling with zero reclamation and "
                "only the outOfMemoryCallback prevented std::abort().  Every "
                "producer and the consumer were parked in waitForHeapHeadroom "
                "with the GC idle -- the P2 topology.  A conforming runtime "
                "keeps at least one thread able to free."};
    return {kId, 8, Status::Pass,
            "completed 50,000 units under a 300,000-cell hard ceiling; "
            "gcCycleCount=" + std::to_string(space.getGCCycleCount()) +
            " reclaimedLastCycle=" + std::to_string(
                space.reclaimedLastCycle.load(std::memory_order_relaxed))};
}
```
The three outcomes: **Pass** (progress under the ceiling), **Fail** (the OOM callback fired, i.e. the P2 topology was reached), and **the process died** — which the isolate runner reports as `Fail` from the outside, by exit status, with the abort's own stderr captured. The third is why the isolate exists.

- [ ] **Step 3: `main.cpp` — one case per process, exit status as the verdict**

```cpp
// protocore-conformance-isolate -- runs ONE case in its own process, so a case
// whose failure mode is std::abort() reports a result instead of destroying the
// run.  The embedder registers it as a ctest test per aborting case.
//
// Exit codes: 0 Pass, 1 Fail, 2 NotApplicable, 3 Skipped, 4 usage error.
// A crash (SIGABRT/SIGSEGV) is a Fail to the caller, and ctest reports the
// signal -- which for rule 8 is the finding itself.
int main(int argc, char** argv) { /* --list | --case=<id> */ }
```
The embedder's CMake registers it as:
```cmake
add_test(NAME conformance.heap.ceiling_progress
         COMMAND <embedder-isolate-binary> --case=heap.ceiling_progress)
set_tests_properties(conformance.heap.ceiling_progress PROPERTIES TIMEOUT 180)
```
**The isolate binary must be built by the embedder**, not protoCore's, because it must link the embedder's own `Host`. So protoCore installs the `main.cpp` logic as a macro in `protoCoreConformance.h` (`PROTOCORE_CONFORMANCE_ISOLATE_MAIN(HostType)`), and protoCore's own copy is the reference build.

**Done when** `$P/build_p4/conformance/protocore-conformance-isolate --case=heap.ceiling_progress; echo $?` prints a recorded number, `--list` prints every id one per line, and the macro is documented in the installed header.

- [ ] **Step 4: Prove the case can detect the P2 topology**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
$P/build_p4/test/proto_tests --gtest_filter='ConformanceSelfCheck.CeilingProgress*' < /dev/null
```
**Done when** `NonConformingHost_AllThreadsWaitForHeadroom` — a host whose `runProducerConsumer` parks every thread it created in `waitForHeapHeadroom` and never drains — makes the case report `Fail` or abort, and the self-check records which. This is Task 9's matrix entry and **the case is not complete without it**: rule 8 is the case most likely to be written in a form that can never fail, because the happy path is simply "the workload finished".

- [ ] **Step 5: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add conformance/CaseHeap.cpp conformance/main.cpp headers/protoCoreConformance.h
git -C $P commit  # "feat(conformance): rule 8 in an isolated process (P4)"
```
**Done when** the build succeeds and `ctest --test-dir $P/build_p4 -R 'conformance.heap' -N < /dev/null` lists the test.

---

### Task 7: `external.perennial_never_finalized` — rule 7's runtime half

**Files:** create `protoCore/conformance/CaseExternal.cpp`.

**Interfaces:** consumes `ProtoContext::fromExternalPointer`, `newExternalBuffer`, `Host::externalBytesAccounted`.

- [ ] **Step 1: The characterisation test nobody has written — a perennial wrapper's finalizer never runs**

`docs/MemoryModel.md` §5 states it: *"A perennial wrapper is never swept, so its finalizer never runs; and nothing runs finalizers at process exit — `~ProtoSpace` (`core/ProtoSpace.cpp:1325`) does not sweep. External memory still held at shutdown is released by the operating system tearing the process down, not by protoCore."*

That is a documented trap with no test. The case asserts the documented behaviour so that a future change to it is a visible failure rather than a silent behaviour change under every embedder:

```cpp
// Rule 7 (runtime half) -- a perennial wrapper's finalizer never runs, not
// even at process exit.  This is a CHARACTERISATION test: it asserts what
// MemoryModel.md section 5 says, so that a change to it cannot land silently.
//
// Two wrappers are built: one through a real context (collectable) and one
// with a null context (perennial, via the posix_memalign path in
// ProtoContext::allocCell).  Cycles are driven.  The collectable one's
// finalizer must run; the perennial one's must NOT.
//
// The finalizer itself obeys the contract it is testing: it increments a
// std::atomic<int> and does nothing else.  It does not allocate, does not
// touch a ProtoObject*, does not CAS into a shared structure and cannot
// block (GarbageCollector.md section 7).
static std::atomic<int> g_collectableFinalized{0};
static std::atomic<int> g_perennialFinalized{0};
static void finalizeCollectable(void*) { g_collectableFinalized.fetch_add(1); }
static void finalizePerennial(void*)   { g_perennialFinalized.fetch_add(1); }
```
The verdict: `g_collectableFinalized == 1 && g_perennialFinalized == 0`. Either half being wrong is a `Fail` whose message quotes `MemoryModel.md` §5 and names which half moved.

**Done when** the case returns `Pass` in protoCore's own build and its `detail` reports both counters. If the collectable finalizer does **not** fire within the deadline, report `Fail` with `"the sweep did not reach the wrapper; MemoryModel.md section 5 says there is no promptness guarantee, so this may be a deadline problem rather than a contract violation — raise the deadline before reporting a kernel bug."` A case that cannot tell those two apart would be worse than none.

- [ ] **Step 2: The accounting probe, honest about what it can and cannot see**

```cpp
// MemoryModel.md section 5 draws the boundary and explains why it cannot be
// moved: "External size is whatever the embedder declares. ... A collection
// policy driven by that number is a policy driven by a figure that can drift
// arbitrarily from reality, and the kernel has no way to detect the drift."
//
// A conformance suite is in exactly the same position.  So this probe answers
// only the question it CAN answer -- does an accounting exist at all -- and
// hands the rest to checklist C7.
const unsigned long bytes = host.externalBytesAccounted();
if (bytes == (unsigned long) -1)
    return {kId, 7, Status::NeedsReview,
            "the runtime keeps no total of the memory it allocates outside "
            "protoCore.  MemoryModel.md section 5 assigns that total to the "
            "embedder ('Count it'), because protoCore cannot see it: external "
            "bytes do not count toward heapSize, create no collection pressure "
            "and are not in the traced graph.  Whether this runtime needs a "
            "total is checklist C7, not something this case can decide."};
```
**Done when** a host returning `-1` yields `NeedsReview` (never `Pass`, never `Fail`), and a host returning a number yields `Pass` with the number in `detail`. Note the prediction: protoScala and protoClojure use **no** external wrappers at all, so `NeedsReview` there is trivially correct and Task 10's checklist records it as N/A in one line.

- [ ] **Step 3: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add conformance/CaseExternal.cpp && git -C $P commit  # "feat(conformance): rule 7 runtime half (P4)"
```
**Done when** the build succeeds and the case appears in `--list`.

---

### Task 8: `check_static.py` — every **S** check, as a ratchet

**Files:** create `protoCore/scripts/conformance/check_static.py`, `protoCore/scripts/conformance/rules.json`, and the five `<R>/conformance-allow.txt` files (populated in Tasks 11–14).

**Interfaces:** consumes an embedder's source tree and its `conformance-allow.txt`. Produces text and `--json` output, exit 0 (clean) / 1 (unjustified site) / 2 (stale allowlist entry).

- [ ] **Step 1: The ratchet contract, written before the checks**

```python
"""check_static.py -- the static half of the embedder conformance suite (P4).

THE CONTRACT, and the reason it is a ratchet rather than a gate:

A static check cannot distinguish a legitimate use from a violation in every
case.  fromUTF8String is CORRECT for a string that is not an attribute key.
getOwnAttributeDirect MUST be compared against nullptr -- that is its
documented convention (headers/protoCore.h:260-264) and a checker that flagged
it would train every reader to ignore this tool.

So each embedder carries conformance-allow.txt: one line per known site, each
with a WRITTEN JUSTIFICATION.  The exit status is driven by the DELTA:

  * a site not in the allowlist            -> exit 1 (the ratchet closed)
  * an allowlist entry whose site is gone  -> exit 2 (stale; delete the line)
  * a count that changed but is covered    -> exit 0, reported, not failed

THE RAW COUNT IS NEVER THE GATE.  A gate on the count would have to be either
zero (impossible on day one) or a magic number (meaningless on day two).
"""
```
Allowlist line format, one per line, `#` comments allowed:
```
<rule-id> <path>:<line> <sha1-of-the-line's-text> :: <justification>
```
The line-text hash is what makes the ratchet honest: it makes an entry stale when the line **changes**, not only when it moves, so an allowlisted `fromUTF8String` that is silently turned into an attribute key re-opens the finding.

**Done when** `python3 check_static.py --help` documents all three exit codes and the format, and a hand-written 3-line allowlist against a fixture tree produces one of each exit code in three runs.

- [ ] **Step 2: `symbol_key_source` — rule 4**

Two findings at two severities, and the distinction is the point:

- **`error`** — a `fromUTF8String` / `fromUTF8` / `fromStdString` result reaching `setAttribute`, `getAttribute`, `hasAttribute`, `getOwnAttribute*` or `setAttributeIfEqual` as the **name** argument, directly or through a local assigned in the same function. Not interned ⇒ a different pointer for the same text ⇒ the key matches nothing, **except** for ≤ 6 ASCII bytes, which protoCore embeds in the pointer word (`INLINE_STRING_MAX_BYTES = 6`, `headers/proto_internal.h:300`) and which therefore match by accident. That accident is what makes the bug so bad: a 5-byte name works, a 7-byte name does not, and nothing errors. protoST's own source records the same observation verbatim (`src/modules/STModuleProvider.cpp:107-116`), and protoScala records it as a warning comment at `src/compiler/BytecodeModule.cpp:288`.
- **`info`** — a `createSymbol` call with a **string literal** in a file that also has a well-known-symbol cache. This is **not** a violation: the key is correct and nothing is silent; the cost is a `SymbolTable::intern` lookup on a possibly-hot path, and the risk is drift between the cache and the literal. protoST has six such sites in `src/modules/object_prims.cpp` (`:229`, `:296`, `:336`, `:451`, `:585`, `:693`) bypassing `Bootstrap.sym.className` / `sym.classSide` (`src/runtime/Bootstrap.h:114-176`). Reporting these as failures would be an overclaim that gets the whole tool ignored, so they are `info` and **do not affect the exit status**.

**Done when** the check reports **0 errors** for protoScala (which has zero `fromUTF8String` calls — its single hit is the warning comment at `BytecodeModule.cpp:288`), and reports protoST's six sites at `info` with the exit status still 0 on that account alone.

- [ ] **Step 3: `attr_sentinel` — rule 6, as a per-function table**

`rules.json` carries the table from §D6 as data, so the check is a lookup and not a heuristic:

```json
{ "attr_sentinel": {
    "getAttribute":            { "absent": "PROTO_NONE", "invalid": "nullptr" },
    "getAttributeOrDefault":   { "absent": "PROTO_NONE", "invalid": "nullptr" },
    "getOwnAttribute":         { "absent": "PROTO_NONE", "invalid": "nullptr" },
    "getOwnAttributeDirect":   { "absent": "nullptr",    "invalid": "nullptr" },
    "hasAttribute":            { "absent": "PROTO_FALSE", "note": "a boolean OBJECT, never a C++ bool" },
    "hasOwnAttribute":         { "absent": "PROTO_FALSE", "note": "a boolean OBJECT, never a C++ bool" } } }
```
The finding fires when a comparison's sentinel does not match the called function's `absent` value — so `getOwnAttributeDirect(...) == nullptr` is **clean** (protoClojure `src/runtime/Primitives.cpp:130-132`) and `getAttribute(...) != nullptr` is an **error**, because it is true for every object. protoScala documents that exact trap inline at `src/runtime/Values.cpp:157` and the check must agree with the comment.

A second finding, `severity=error`: any use of a `hasAttribute` / `hasOwnAttribute` result in a C++ boolean context (`if (obj->hasAttribute(...))`) — the result is `PROTO_TRUE` (`1217UL`) or `PROTO_FALSE` (`193UL`), both non-null, so the condition is **always true**. This is not in the maintainer's list and it is the cheapest silent bug in protoCore's whole surface.

**Done when** the check reports 0 errors for protoScala, protoST and protoClojure (all three predicted clean), and a fixture containing `getAttribute(k) != nullptr` and `if (o->hasAttribute(k))` produces exactly two errors.

- [ ] **Step 4: `external_finalizer` — rule 7's static half**

Three findings:
1. **`error` (new sites only)** — a `fromExternalPointer(p)` call with the `finalizer` argument defaulted or `nullptr`. Every existing site goes to the allowlist with its justification, because a null finalizer is legitimate when the embedder frees the memory itself. protoST has three, all null: `src/debugger/DapServer.cpp:257`, `src/runtime/STRuntime.cpp:671`, `src/runtime/ExecutionEngine.cpp:2452-2453`. Whether each is right is checklist C7.
2. **`error`** — a function used as a finalizer whose body contains a call to any protoCore allocating or attribute API, a `compare_exchange`, a `lock()`, a `wait(`, a `join(` or a `->` dereference of a `ProtoObject*`. The contract is absolute (`docs/GarbageCollector.md` §7 plus the non-blocking paragraph): a finalizer *"never allocates cells, never publishes to a shared structure with compare-and-swap, never loops over protoCore data and never dereferences other `ProtoObject*`"*, and *"a finalizer must also not block"* because it runs on the single GC thread inside the sweep. Only the finalizer's own body is analysed — one level deep. **A finalizer that calls an embedder helper is `warn`, not `error`, and is a C7 item**, because following it is the escape analysis §C3 declines to attempt.
3. **`info`** — `setHeapLimits` is never called, and/or `outOfMemoryCallback` is never installed. Predicted for **all five runtimes** (Task 1 Step 5). Reported so the design question is on the record for P5; not a failure, because whether a runtime should bound its heap is a maintainer decision, not a rule.

**Done when** the check reports protoST's three `fromExternalPointer` sites, zero for protoScala and protoClojure (neither uses external wrappers at all), and the `info` line about `setHeapLimits` for every runtime.

- [ ] **Step 5: `critsec_across_block` — rule 12**

A `ProtoContext::CriticalSection` in scope at a `wait(`, `acquire()`, `join(`, `sleep`, `lock()` on a non-leaf mutex, or an `UnmanagedScope` construction, in the same or a nested block. `severity=error`, with this explanation:

> `parkForStopTheWorld` skips parking while `criticalSectionDepth > 0`, and `ProtoContext::safepoint()` skips young-chain submission at depth > 0 (`core/ProtoContext.cpp:376`). A critical section held across a blocking wait therefore reproduces rule 2's hang **and** rule 1's non-submission, through a mechanism that looks like correct code. An `UnmanagedScope` inside a `CriticalSection` is doubly wrong: the section exists to protect a half-built structure, and the scope tells the collector to stop waiting for this thread.

Four runtimes use `CriticalSection`: protoST (`src/modules/STModuleProvider.cpp`), protoClojure (`src/runtime/Primitives.cpp`), protoJS (`src/runtime/ProtoInterpreter.cpp`, `src/StringPrototype.cpp`, `src/TypeBridge.cpp`), protoPython (`src/library/PythonEnvironment.cpp`). protoScala uses none.

**Done when** the check runs over all five and its findings are recorded; the prediction is 0 errors, and a fixture with an `UnmanagedScope` nested inside a `CriticalSection` produces one.

- [ ] **Step 6: `global_symbol_assumption` — rule 9a (P3)**

Reuse P3's own census, which already defines the two shapes that break (`protoScala/docs/plans/2026-09-24-phase-p3-global-interning.md` Task 9): **(a)** a raw symbol pointer cached in a `static` or global across space lifetimes, **(b)** any code that assumes a symbol is valid only within one space. Both must be zero. Implemented as: a `static`/`namespace`-scope variable of type `const ProtoString*` / `const ProtoObject*` assigned from `createSymbol`, without a comment carrying the token `PROTOCORE_GLOBAL_SYMBOL_OK`.

**Note that protoST's `Bootstrap.sym` WKS cache (32 members, `src/runtime/Bootstrap.h:114-176`) is exactly shape (a) if it is a static** — so this check's first run must establish whether it is a member of a per-space object (fine) or a file-scope static (a finding). Determine it in Task 12 Step 2 and allowlist with the reason, or report it.

**Done when** the check exists, is gated on `PROTOCORE_VERSION_MINOR >= 2`, and reports `Skipped: requires P3` below that.

- [ ] **Step 7: `module_key_shape` — rule 9c (P3)**

Flag any map, cache or comparison keyed by a module path **alone**. P3 rules the identity as **provider GUID + logical path + version**, rendered with `\x1F` separators (P3 §D11), and records that `SharedModuleCache` was keyed by path with the provider prefix stripped, so `import st.counter_lib` and a local `counter_lib.scala` collapsed into one module with the first load winning. protoScala's `Session::loadForeign` already keys on `providerSpec + "/" + logicalPath` (`src/repl/Session.cpp:474`), which is closer but still not the ruled triple.

**Done when** the check flags a `std::map`/`unordered_map` whose key expression is a bare `logicalPath`/`path` variable in a module-resolution file, reports zero for a tree using `ModuleIdentity`, and is `Skipped` pre-P3.

- [ ] **Step 8: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add scripts/conformance/check_static.py scripts/conformance/rules.json
git -C $P commit  # "feat(conformance): static checks as a justified ratchet (P4)"
```
**Done when** `python3 $P/scripts/conformance/check_static.py --self-test` exits 0 and its fixture suite covers one positive and one negative per check.

---

### Task 9: The self-check — rule 10, the task that makes every other task believable

**Files:** create `protoCore/test/ConformanceSelfCheckTests.cpp`, `protoCore/conformance/SelfHost.h` (internal), `protoCore/scripts/conformance/mutate.py`. Modify `protoCore/test/GCSurvivorRechainTests.cpp` (comment only).

**Interfaces:** consumes the conformance library and `ProtoSpace::newThread`. Produces `ConformanceSelfCheck.*` ctest entries and `mutate.py`.

**Why this task is not optional.** protoST's suite looked green for its entire history while the collector reclaimed nothing. protoScala shipped six map fixtures (`tests/conformance/18-maps-and-sets/map-identity-keys-are-distinct.scala`, `map-classification-round-trip.scala`, `map-overriding-equals-switches-the-classification.scala`, `map-value-keys-are-structural.scala`, `map-equals-without-hashcode-misses.scala`, `set-classification-mirrors-map.scala`) that could not detect a broken key classification, because for a class that does not override `equals`/`hashCode` the value path and the identity path are **observationally identical through the language** — only the white-box unit test at `tests/unit/test_collections.cpp:221-229` distinguishes them, and it records that *"answering `false` for every key ... passes every conformance fixture and fails here."* Phase 6 shipped two of its own. **A GC or concurrency test that cannot fail is worse than no test**, because it converts absence of evidence into evidence of absence.

- [ ] **Step 1: `SelfHost` — protoCore's own reference implementation of `Host`**

A conforming `Host` implemented against protoCore directly: `makeGarbage` allocates `n` cells in a child `ProtoContext` and destroys it; `makeSequenceGarbage` builds and discards `ProtoList`s; `forEachThreadKind` reports one kind, `"selfhost-worker"`, created through `ProtoSpace::newThread`; `runProducerConsumer` drives a `ProtoMPSCQueue` with two producers and one consumer, each draining; `internAttributeKey` calls `ProtoString::createSymbol`; `externalBytesAccounted` returns a real total.

**Done when** `runAll(selfHost)` reports `Pass` for every non-P3 case and `NotApplicable` for none. **If any case is `NotApplicable` under `SelfHost`, that case is not finished** — `SelfHost` is by construction able to supply every capability, so a `NotApplicable` there means the capability is unreachable and the case would be `NotApplicable` for everyone.

- [ ] **Step 2: The mutation matrix — one deliberately broken host per case**

| Case | `NonConformingHost_*` | The mutation |
|---|---|---|
| `gc.young_submitted` | `NoSafepoint` | `makeGarbage` allocates in a **long-lived** context and never calls `safepoint()` — protoST's S15 exactly |
| `gc.transient_reclaimed` | `PerennialSequence` | `makeSequenceGarbage` builds `ProtoTuple`s — protoClojure's pre-Track-C vectors |
| `gc.host_stress` | `UnrootedSnapshot` | `runProducerConsumer` holds a CAS snapshot in a bare local across `appendLast` — protoScala's `Mailbox::push` before its fix |
| `stw.quorum_completes` | `BlocksOutsideUnmanaged` | the worker waits on a condition variable with **no** `UnmanagedScope` — protoClojure's `queueCv_` |
| `thread.registered` | `RawStdThread` | the worker is a bare `std::thread` with its own `ProtoContext`, never `newThread` |
| `heap.ceiling_progress` | `AllThreadsWaitForHeadroom` | every thread parks in `waitForHeapHeadroom` and none drains — the P2 topology |
| `external.perennial_never_finalized` | `FinalizerOnPerennial` | asserts the inverse, to prove the case is not merely reading its own expectation |
| `module.root_survives_cycle` | `UnrootedModule` | registers a module without rooting it (P3-gated) |

Each is a `TEST` in `ConformanceSelfCheckTests.cpp` of exactly this shape:

```cpp
// Rule 10 -- a case that cannot fail is worse than no case.  Every entry below
// runs one conformance case against a host built to violate exactly that rule,
// and REQUIRES a Fail.  If one of these ever passes, the corresponding case has
// stopped measuring anything and every green conformance report that relied on
// it is void.
TEST(ConformanceSelfCheck, YoungSubmittedCatchesMissingSafepoint) {
    NonConformingHost_NoSafepoint host;
    const auto r = proto::conformance::runOne(host, "gc.young_submitted");
    EXPECT_EQ(r.status, proto::conformance::Status::Fail) << r.detail;
    // And the message must be diagnostic, not merely negative: a failure that
    // does not name both numbers sends the reader back to guessing.
    EXPECT_NE(r.detail.find("created"), std::string::npos) << r.detail;
    EXPECT_NE(r.detail.find("safepoint"), std::string::npos) << r.detail;
}
```
**Done when** `ctest --test-dir $P/build_p4 -R ConformanceSelfCheck --output-on-failure < /dev/null` passes with **one test per row** of the table above, and `protocore-conformance-isolate --list | wc -l` equals the number of rows plus the cases whose mutation is a static check.

- [ ] **Step 3: The runner's own vacuity checks**

Three more self-checks, on the harness rather than on a case:

```cpp
TEST(ConformanceSelfCheck, NotApplicableIsNeverReportedAsPass) {
    EmptyHost host;   // implements ONLY mainContext(), name() and makeGarbage()->0
    for (const auto& r : proto::conformance::runAll(host)) {
        EXPECT_NE(r.status, proto::conformance::Status::Pass)
            << r.id << " passed for a host that supplies no capability: "
            << r.detail;
        EXPECT_FALSE(r.detail.empty()) << r.id << " reported no detail";
    }
}
TEST(ConformanceSelfCheck, UnknownCaseIdFailsRatherThanSkips) { /* ... */ }
TEST(ConformanceSelfCheck, ProtoCoreNamesNoRuntime) {
    // grep-equivalent, executed: the library must not mention a runtime.
}
```
**Done when** all three pass. The first is the single most important test in the phase: **it is the assertion that the suite cannot repeat protoST's mistake in its own voice.**

- [ ] **Step 4: `mutate.py` — the per-embedder mutation gate**

The matrix above proves the cases work against synthetic hosts. It does **not** prove they work against a real runtime's adaptor, which is a different thing: an adaptor whose `makeGarbage` accidentally keeps its garbage reachable would make `gc.young_submitted` pass for the wrong reason. So each embedder runs **one** mutation on its own adaptor:

```bash
python3 protoCore/scripts/conformance/mutate.py \
  --repo /home/gamarino/Documentos/proyectos/protoST \
  --mutation drop-safepoint \
  --expect-red conformance.gc.young_submitted
```
`mutate.py` copies the repository's conformance-adaptor file to scratch, applies a named textual mutation (`drop-safepoint` comments out every `safepoint()` call in the adaptor's garbage-making path), rebuilds **only** the embedder's conformance target, asserts the named case turns red, then restores the file and rebuilds. It refuses to run on a dirty tree and it restores in a `finally`.

**Done when** `mutate.py --list` names the mutations, and Tasks 11–14 each record one green→red→green cycle for their runtime. **A runtime whose mutation does not turn its case red has not run the suite; it has run a decoration.**

- [ ] **Step 5: Assert protoCore names no runtime**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
grep -rEIl 'protoST|protoJS|protoPython|protoClojure|protoScala' \
  $P/conformance/ $P/headers/protoCoreConformance*.h; echo "exit=$?"
```
**Done when** the grep matches nothing (`exit=1`). References to a runtime belong in `docs/EMBEDDER-CONFORMANCE.md` and in this plan, never in the library — that is §D2's whole point, and a test rather than an intention.

- [ ] **Step 6: Annotate the vacuous pattern where it already lives**

Add to `protoCore/test/GCSurvivorRechainTests.cpp`, above `:145`:

```cpp
    // NOTE (P4): this assertion is sound for what T2 tests -- one cell, one
    // reference dropped -- but it is the SHAPE to avoid in a GC test that
    // creates bulk garbage.  `freeAfter > freeBefore` is satisfied by a single
    // cell, and Track Y measured a forced cycle reclaiming 4-7 cells where the
    // workload had created 205,120, with a `> 0` assertion passing.  For a bulk
    // workload use conformance/CycleDriver.h::checkProportionalReclaim, which
    // takes the denominator as an argument and offers no way to ask the
    // vacuous question.
```
**Done when** the comment is present, `ctest --test-dir $P/build_p4 -R GCSurvivor --output-on-failure < /dev/null` still passes, and no behaviour changed (`git diff --stat` shows only added comment lines).

- [ ] **Step 7: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add test/ConformanceSelfCheckTests.cpp conformance/SelfHost.h \
  scripts/conformance/mutate.py test/GCSurvivorRechainTests.cpp
git -C $P commit  # "test(conformance): the self-check matrix -- every case must fail under a named mutation (P4)"
```
**Done when** `ctest --test-dir $P/build_p4 --output-on-failure < /dev/null` reports **438 + the new tests**, all passing, and the new count is recorded in `$S/baseline-protoCore-after.txt`.

---

### Task 10: `docs/EMBEDDER-CONFORMANCE.md` and the judgement checklist

**Files:** create `protoCore/docs/EMBEDDER-CONFORMANCE.md` and the template `protoCore/docs/templates/CONFORMANCE-template.md`.

- [ ] **Step 1: The normative rule table**

The document opens with the twelve rules, each as: the rule in one sentence, the mechanism (S/R/J), **why omitting it is silent**, the case or check id, and the protoCore citation. It is the rule table of this plan, promoted to normative text in protoCore where it belongs — not in a plan file in protoScala, which is where it lives today by accident.

**Done when** every rule in the table has at least one `file:line` citation into protoCore, and `docs/TESTING.md` gains a "Conformance" section linking to it.

- [ ] **Step 2: The per-function sentinel table (§D6)**

Ship the table from §D6 verbatim, with its three citations, plus this paragraph:

> `getOwnAttributeDirect` returns `nullptr` both for "absent" and for "the receiver is not an object cell", so it **cannot distinguish them**. That is a real expressive gap and it is why this class of confusion exists at all. The documented way round it is to probe with `hasOwnAttribute` or `hasAttribute` first. P4 documents the gap and does not close it: changing a return convention is an ABI-visible semantic change, and this phase is additive.

**Done when** the table is present and `check_static.py`'s `rules.json` is generated from it or verified against it by a test, so the document and the checker cannot drift.

- [ ] **Step 3: The three judgement items, with the answer format**

```markdown
### C3 — Is any `ProtoObject*` held across an allocation only in a C++ local?
**Mechanised:** `gc.host_stress` under ASan, and shapes A–C.
**Not mechanised:** the general case. Deciding it requires escape analysis over
the whole call graph; a checker that claimed to do it would have an unknown
false-negative rate and, being believed, would be worse than this question.
**Answer by:** naming every path in this runtime that (a) holds a
`ProtoObject*` in a C++ local and (b) allocates or CASes while holding it, and
stating for each how it is rooted — `setAutomaticLocal`, `pendingRoot`,
`ProtoRootSet`, a `CriticalSection`, or an argument that no allocation occurs.
**Reviewed:** <name> <date> <commit>
```
`C5` (which structure rule 5 is measured on, and why) and `C7` (external accounting and release correctness, quoting `MemoryModel.md` §5 on why the kernel cannot decide it) follow the same shape.

**Done when** the three items are present, each names what **is** mechanised alongside what is not, and the template carries a `Reviewed:` line that the runner's `--strict-review` mode requires.

- [ ] **Step 4: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add docs/EMBEDDER-CONFORMANCE.md docs/templates/CONFORMANCE-template.md docs/TESTING.md
git -C $P commit  # "docs(conformance): the normative rule table and the judgement checklist (P4)"
```
**Done when** `grep -c '^| ' $P/docs/EMBEDDER-CONFORMANCE.md` is at least 12 and every case id in `caseIds()` appears in the document (assert with a shell loop).

---

### Task 11: protoScala — the reference adaptor, written first because it is predicted clean

**Files:** create `protoScala/tests/unit/ConformanceHost.h`, `protoScala/tests/unit/ConformanceTests.cpp`, `protoScala/conformance-allow.txt`, `protoScala/docs/CONFORMANCE.md`. Modify `protoScala/tests/unit/CMakeLists.txt`.

**Why protoScala first.** It is predicted to pass rules 1–7 and 12 cleanly (Task 1 Step 6), it already uses GoogleTest (`tests/CMakeLists.txt:1-10`), and it is the repository whose CLAUDE.md already carries three of the rules as prose ("never map transient data to `ProtoTuple`", the `PROTO_NONE` convention at `src/runtime/Values.cpp:157`, the rooting comment at `src/runtime/Mailbox.cpp:50-54`). **A first adaptor written against a runtime expected to fail teaches you nothing about whether the adaptor is right** — a red result is ambiguous between a real finding and a broken adaptor, and Task 12 onward depends on that ambiguity being already resolved.

- [ ] **Step 1: The adaptor**

```cpp
// protoScala's conformance Host.  Each capability is implemented through
// protoScala's OWN evaluator, never through direct protoCore calls: the point
// of the suite is to audit what protoScala does, and a Host that reaches past
// its runtime audits protoCore instead.
class ScalaConformanceHost final : public proto::conformance::Host
{
public:
    ScalaConformanceHost() : session_() {}
    const char*   name() const override { return "protoScala"; }
    proto::ProtoContext* mainContext() override { return session_.rootContext(); }

    unsigned long makeGarbage(unsigned long requested) override {
        // Evaluate protoScala source that builds and drops objects, and report
        // what protoCore actually charged us.  The denominator comes from
        // allocatedCellsCount, not from an estimate (CycleDriver.h).
        const unsigned long before = mainContext()->allocatedCellsCount;
        const unsigned long iterations = requested / 8 + 1;
        session_.eval("var i = 0; while (i < " + std::to_string(iterations) +
                      ") { val t = new Object(); i = i + 1 }");
        return mainContext()->allocatedCellsCount - before;
    }

    unsigned long makeSequenceGarbage(unsigned long requested) override {
        // protoScala's user-visible sequence.  Recorded in docs/CONFORMANCE.md
        // C5 as: List, built by CollectionPrimitives; NOT a ProtoTuple, by the
        // rule in CLAUDE.md and the comment at CollectionPrimitives.cpp:630.
        /* ... eval a loop building and dropping Lists ... */
    }

    bool forEachThreadKind(ThreadBody body, void* user) override {
        // protoScala creates one kind: the actor-scheduler worker
        // (src/runtime/ActorScheduler.cpp), which blocks on work_.acquire()
        // inside an UnmanagedScope (:490) and is created through protoCore.
        const ThreadKind kind{"actor-worker", /*blocksWhenIdle=*/true,
                              ThreadVerdict::Registered};
        /* ... spawn through protoScala's own scheduler, run body, join ... */
    }

    bool runProducerConsumer(unsigned long units) override {
        // protoScala's Mailbox over ProtoMPSCQueue -- the exact path P2's
        // finding was measured on, and the path whose CAS snapshot is now
        // rooted before appendLast (src/runtime/Mailbox.cpp:50-58).
    }

    const ProtoObject* internAttributeKey(const char* text) override {
        return proto::ProtoString::createSymbol(mainContext(), text);
    }
private:
    scala::Session session_;
};
```
**Done when** the file compiles and `ScalaConformanceHost` overrides `mainContext`, `name`, `makeGarbage`, `makeSequenceGarbage`, `forEachThreadKind`, `runProducerConsumer` and `internAttributeKey` — the seven capabilities every runtime must supply. `externalBytesAccounted` is deliberately **not** overridden, because protoScala uses no external wrappers at all, and the resulting `NeedsReview` is answered in one line in C7.

- [ ] **Step 2: Wire it into the existing test target**

```cpp
// protoScala/tests/unit/ConformanceTests.cpp
#include "ConformanceHost.h"
#include <protoCoreConformanceGTest.h>
PROTOCORE_CONFORMANCE_GTEST(ScalaConformanceHost)
PROTOCORE_CONFORMANCE_ISOLATE_MAIN_TARGET(ScalaConformanceHost)  // rule 8
```
and in `tests/unit/CMakeLists.txt`, link `protoCore::conformance`.

- [ ] **Step 3: Run, and check the run is not vacuous**

```bash
R=/home/gamarino/Documentos/proyectos/protoScala; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
rm -rf $R/build_release && cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build $R/build_release -j4
ctest --test-dir $R/build_release -N -R conformance < /dev/null | tail -5
bash /home/gamarino/Documentos/proyectos/protoCore/scripts/conformance/run.sh $R/build_release 4 600 \
  2>&1 | tee $S/conformance-protoScala.txt
grep -c 'detail' $S/conformance-protoScala.txt
```
**Done when** `ctest -N -R conformance` lists exactly `caseIds().size()` entries, the run completes, **and every reported case carries a non-empty `detail`**. A green run whose details are empty is the failure mode this phase exists to end, so the `grep -c` is part of the done-when, not decoration.

- [ ] **Step 4: The ASan run, without which rule 3 is not evidence**

```bash
R=/home/gamarino/Documentos/proyectos/protoScala; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
rm -rf $R/build_asan && cmake -S $R -B $R/build_asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build $R/build_asan -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir $R/build_asan -R 'conformance.gc' \
  --output-on-failure < /dev/null 2>&1 | tee $S/asan-protoScala.txt
grep -c 'ERROR: AddressSanitizer' $S/asan-protoScala.txt
```
**Done when** the grep prints `0`. `detect_leaks=0` is deliberate: protoCore defers collection by design and perennials are never freed, so LeakSanitizer reports the architecture rather than a bug (memory: memory_pressure benchmark not meaningful).

- [ ] **Step 5: The mutation — green, red, green**

```bash
python3 /home/gamarino/Documentos/proyectos/protoCore/scripts/conformance/mutate.py \
  --repo /home/gamarino/Documentos/proyectos/protoScala \
  --mutation keep-garbage-reachable \
  --expect-red conformance.gc.young_submitted
```
**Done when** the script reports `green -> red -> green`. `keep-garbage-reachable` makes `makeGarbage` retain its objects in a `ProtoRootSet`; the case must then fail. **This is what distinguishes running the suite from decorating with it**: without it, a `makeGarbage` that accidentally keeps its garbage alive, or that allocates nothing at all, produces a pass for the wrong reason. The exception to the "no fixes in P4" rule (Global Constraints) applies here and only here: if this step reveals the *adaptor* is wrong, fix the adaptor — it is this phase's own code.

- [ ] **Step 6: `symbol.trust_symbols_clean` — §D11's runtime case**

```bash
R=/home/gamarino/Documentos/proyectos/protoScala; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
PROTOCORE_TRUST_SYMBOLS=1 ctest --test-dir $R/build_release --output-on-failure < /dev/null \
  2>&1 | tail -20 | tee $S/trustsymbols-protoScala.txt
```
**Done when** the pass count equals the Task 1 Step 3 baseline. protoScala is predicted to be identical, because it has **zero** `fromUTF8String` calls (its single hit is the warning comment at `src/compiler/BytecodeModule.cpp:288`). A difference here would be the most interesting result in the whole phase, because it would mean an uninterned key is reaching protoCore from a path nobody has found by grep.

- [ ] **Step 7: The static check, the allowlist, and the review**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; R=/home/gamarino/Documentos/proyectos/protoScala
python3 $P/scripts/conformance/check_static.py --repo $R --init-allowlist > $R/conformance-allow.txt
python3 $P/scripts/conformance/check_static.py --repo $R; echo "exit=$?"
```
Then write `protoScala/docs/CONFORMANCE.md` from the template with the three **J** answers: **C3** (every path holding a `ProtoObject*` across an allocation, and how each is rooted — the `Mailbox.cpp:50-58` `setAutomaticLocal` is the model answer), **C5** (`List`, and why not `ProtoTuple`, citing `CollectionPrimitives.cpp:630`), **C7** (N/A — no external wrappers, one line).

**Done when** `check_static.py --repo $R` exits 0, every allowlist line carries a justification (`grep -c ' :: ' $R/conformance-allow.txt` equals the non-comment line count), and `docs/CONFORMANCE.md` has all three `Reviewed:` lines filled.

- [ ] **Step 8: Commit, and record the result**

```bash
R=/home/gamarino/Documentos/proyectos/protoScala
git -C $R switch -c feature/p4-conformance
git -C $R add tests/unit/ConformanceHost.h tests/unit/ConformanceTests.cpp \
  tests/unit/CMakeLists.txt conformance-allow.txt docs/CONFORMANCE.md docs/STATUS.md
git -C $R commit  # "test(conformance): run protoCore's embedder conformance suite (P4)"
```
**Done when** `ctest --test-dir $R/build_release --output-on-failure < /dev/null` reports the Task 1 Step 3 baseline **plus** the conformance entries, with no previously-passing test newly failing, and `docs/STATUS.md` records the new total and the first-run conformance result. The new deviation, if any, is **D103** — `STATUS.md`'s registry runs to D102 (`:795`), and this plan's own `D1`–`D12` are plan-local and do not enter it.

---

### Task 12: protoST and protoClojure — the two runtimes with predicted failures

**Files:** per repo: `tests/unit/ConformanceHost.h`, `tests/unit/ConformanceTests.cpp`, `conformance-allow.txt`, `docs/CONFORMANCE.md`, and the test `CMakeLists.txt`.

- [ ] **Step 1: protoST — Catch2, not GoogleTest**

protoST's unit suite is Catch2 (`tests/unit`), so it uses `protoCoreConformanceCatch2.h`. Its `Host`:
- `forEachThreadKind` reports one kind, `"st-worker"`, `Registered`, which blocks on `impl_->workerSem.acquire()` inside `UnmanagedScope` (`src/runtime/STRuntime.cpp:871-874`) — the shape protoClojure got wrong and protoST got right.
- `runProducerConsumer` drives protoST's `ProtoMPSCQueue` mailbox, which is the path `MailboxCursor::adopt` sits on (`src/runtime/STRuntime.cpp:1517-1532`, now a by-value `TransientPin`). **Run this under ASan**: the `adopt` bug was unreachable until S15 was fixed and 848 non-ASan tests could not see it.
- `externalBytesAccounted` — protoST has three `fromExternalPointer` sites, all with a `nullptr` finalizer, so this must return a real total or `-1` honestly, and C7 must answer for all three.

- [ ] **Step 2: protoST — run it, with stdin closed, and settle the WKS-static question**

```bash
R=/home/gamarino/Documentos/proyectos/protoST; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
rm -rf $R/build && cmake -S $R -B $R/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build $R/build -j4
bash /home/gamarino/Documentos/proyectos/protoCore/scripts/conformance/run.sh $R/build 4 600 \
  2>&1 | tee $S/conformance-protoST.txt
# The rule-9a question from Task 8 Step 6: is Bootstrap::Symbols a per-space
# member or a file-scope static?  A static would be shape (a).
grep -n 'struct Symbols' -A 4 $R/src/runtime/Bootstrap.h
grep -rn 'static .*Symbols\|Symbols  *sym' $R/src | head
```
**Done when** the conformance run completes (the `run.sh` `< /dev/null` is what makes this survive S18), and the step's report states whether `Bootstrap::Symbols` is per-space (fine) or a file-scope static (a rule-9a finding to allowlist with its reason).

- [ ] **Step 3: protoST — file S18 as a finding, do not fix it here**

Add to `protoST/docs/CONFORMANCE.md`:

> **Harness hazard S18.** `tests/unit/test_debugger.cpp` first case omits `setInputStream`, where its three siblings supply it, so it falls through to the real `std::cin` (`src/debugger/DebuggerRuntime.cpp:75`) and blocks for ~14 minutes. Every conformance invocation closes stdin (`protoCore/scripts/conformance/run.sh`). **Fixing the test is protoST's, not P4's** — but note that a suite whose runner can hang makes rules 2, 8 and 11 undiagnosable, because a hang is their signature, so the closed stdin is not optional.

**Done when** the paragraph is present and `timeout 120 bash .../run.sh $R/build 4 60` exits 0.

- [ ] **Step 4: protoClojure — expect two red cases, and confirm the mechanism before reporting**

```bash
R=/home/gamarino/Documentos/proyectos/protoClojure; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
# Confirm the premise of the rule-1 prediction before believing the red.
grep -rn 'safepoint' $R/src --include=*.cpp --include=*.h    # expected: none
git -C $R log --oneline -S 'safepoint()' -- src | head
grep -n 'REINCLUDE_SURVIVORS' $R/CMakeLists.txt /home/gamarino/Documentos/proyectos/protoCore/CMakeLists.txt
rm -rf $R/build_release && cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build $R/build_release -j4
bash /home/gamarino/Documentos/proyectos/protoCore/scripts/conformance/run.sh $R/build_release 1 600 \
  2>&1 | tee $S/conformance-protoClojure.txt
```
**Done when** the report states three things: (i) whether `safepoint` appears in `src/` at all, (ii) the commits that added it (`1bcc433`) and reverted it (`dde94c8`), and (iii) whether `PROTOCORE_GC_REINCLUDE_SURVIVORS` is ON in the protoCore this build links. **Only with (iii) confirmed ON is a red `gc.young_submitted` a real finding** — the revert's stated reason was that the safepoint *"only served the allocation-budget GC trigger being reverted in protoCore"*, and if that trigger really were off, the case would correctly report `NotApplicable` instead. Getting this order right is what separates a finding from an accusation.

- [ ] **Step 5: protoClojure — rule 2's three unwrapped joins**

```bash
R=/home/gamarino/Documentos/proyectos/protoClojure
sed -n '70,85p'    $R/src/runtime/ActorScheduler.cpp     # shutdown(): bare t->join(ctx)
sed -n '1592,1602p' $R/src/runtime/Primitives.cpp        # future join
sed -n '1920,1930p' $R/src/runtime/Primitives.cpp        # shutdown join
sed -n '183,193p'  $R/src/runtime/ActorScheduler.cpp     # the idle wait, correctly wrapped
```
**Done when** the report shows the contrast: `popReady_` declares `UnmanagedScope parked(ctx)` before `queueCv_.wait` (`:187-189`, restored by `a6b722d` after `e4bca30` was reverted in `2bb3b8d`) while the three joins have none. **A partial fix that a green suite certified is the exact pattern this phase exists to surface**, and it is the single best illustration of why a rule needs a case rather than a commit.

- [ ] **Step 6: Both runtimes — mutation, ASan, allowlist, review, commit**

Per repo, as Task 11 Steps 4–8. protoST's mutation is `drop-safepoint` (expect `conformance.gc.young_submitted` red); protoClojure's is `wrap-joins` applied **in reverse** — since its rule-2 case is predicted already red, the mutation that proves the case works is the *fix*: wrap the three joins in `UnmanagedScope` in a scratch copy and require `conformance.stw.quorum_completes` to turn **green**. Record it as `red -> green -> red`.

**Done when** each repo has: a completed conformance run with every `detail` populated, an ASan run with zero `AddressSanitizer` errors, a mutation cycle recorded in both directions where applicable, a justified allowlist, three answered **J** items, and a suite total equal to its Task 1 baseline plus the conformance entries. **No runtime fix is committed in this phase** (Global Constraints); protoClojure's safepoint and its three joins become P5's first two commits.

---

### Task 13: protoPython — the largest runtime, and two predicted findings the maintainer's list did not contain

**Files:** create `protoPython/test/ConformanceHost.h`, `protoPython/test/ConformanceTests.cpp`, `protoPython/conformance-allow.txt`, `protoPython/docs/CONFORMANCE.md`. Modify `protoPython/CMakeLists.txt`.

**Interfaces:** note that protoPython consumes protoCore by `add_subdirectory`, so it reaches the library as the in-tree target `protoCoreConformance`, not through `find_package`. This is the path Task 2 Step 5's guard exists for and **this task is its only proof.**

- [ ] **Step 1: Build directory discipline, before anything else**

Use `build_release/`. **Never `build-release/`** — it is corrupted (memory: build dirs) and it still exists in the tree. State the path explicitly in every command in this task.

- [ ] **Step 2: Prove the `add_subdirectory` consumption path**

```bash
R=/home/gamarino/Documentos/proyectos/protoPython
rm -rf $R/build_release && cmake -S $R -B $R/build_release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build $R/build_release --target protoCoreConformance -j4
grep -rn 'protoCoreConformance' $R/build_release/CMakeCache.txt $R/CMakeLists.txt | head
```
**Done when** the target builds inside protoPython's tree and **no install rule ran** — verify by `test ! -d $R/build_release/install`. If the target is missing, the guard in `conformance/CMakeLists.txt` is wrong: the library must be built unconditionally and only *installed* under the top-level guard (Task 2 Step 5), and this is the step that catches a guard placed one line too high.

- [ ] **Step 3: The adaptor, and the rule-5 capability that is predicted to fail**

```cpp
unsigned long makeSequenceGarbage(unsigned long requested) override {
    // protoPython's user-visible sequences are list AND tuple.  C5 records
    // that this capability uses TUPLE, deliberately: a Python tuple is a
    // first-class sequence a user gets from a literal, and protoPython builds
    // it out of protoCore's ProtoTuple -- whose nodes are interned and
    // perennial.  Measuring the list instead would make the case pass and
    // would be choosing the workload to get the answer.
    //
    // Predicted result: FAIL.  src/compiler/CppGenerator.cpp:356 builds a
    // fresh positional-args ProtoTuple via newTupleFromList on every
    // AOT-compiled call site; OP_BUILD_TUPLE and OP_LIST_TO_TUPLE allocate per
    // execution; and *args binding allocates one per call
    // (src/library/ExecutionEngine.cpp:604-609).
    /* ... eval: for i in range(n): t = (i, i+1, i+2)   ... */
}
```
**Done when** the capability is implemented against the tuple, C5 records **why the tuple and not the list**, and the observed result is recorded whichever way it goes. Choosing the list here would be the same defect as protoScala's six vacuous map fixtures: a workload selected so that the answer cannot be bad.

- [ ] **Step 4: Confirm the rule-6 finding as a live defect, not a lint opinion**

```bash
R=/home/gamarino/Documentos/proyectos/protoPython
sed -n '11420,11435p' $R/src/library/PythonEnvironment.cpp
$R/build_release/protopy -c 'print(b"abc".find(b"b"), b"abc".find(42))' ; echo "exit=$?"
```
**Done when** the report states whether `py_bytes_find`'s `getAttribute(context, ..."__data__") == nullptr` guard is reachable at all. It cannot be: `getAttribute` returns `PROTO_NONE` for an absent attribute and `nullptr` only for invalid input (`protoCore/core/ProtoObject.cpp:773-781`), so the branch is dead and `bytes.find()` does not short-circuit for a non-data-bearing needle. **Establish the user-visible consequence before filing it** — a static finding with no demonstrated behaviour is the kind of report that gets a tool switched off.

- [ ] **Step 5: Rule 2's shutdown finding, measured rather than asserted**

```bash
R=/home/gamarino/Documentos/proyectos/protoPython
sed -n '278,292p' $R/src/runtime/main.cpp; sed -n '360,392p' $R/src/runtime/main.cpp
```
**Done when** the report states that the shutdown polling loops call `usleep(50000)` while reading `runningThreads`, for up to 5 s, on the main thread with no `UnmanagedScope` — so a stop-the-world request during shutdown waits the full 5 s. Note the severity honestly: **this is a latency finding at teardown, not a hang**, because the loop terminates on its own. Rule 2's case may well pass, since it exercises steady state rather than shutdown, and the static check is what reports this. Saying so is the difference between a finding and an alarm.

- [ ] **Step 6: Run, mutate, ASan, `PROTOCORE_TRUST_SYMBOLS`, allowlist, review, commit**

As Task 11 Steps 3–8, with `build_release/`. protoPython's mutation is `drop-safepoint` on `ExecutionEngine.cpp:3981`'s cadence inside the adaptor's workload.

**Done when** `ctest --test-dir $R/build_release --output-on-failure < /dev/null` reports **582/583 plus the conformance entries**, with `protopy_import_site` still the only pre-existing failure. **Record, additionally, that `protopy_import_site` has no skip or xfail marker anywhere in the repository** (`CMakeLists.txt:311-316` registers it with a `regression_gate` label and a 120 s timeout and nothing else): a failure documented only in a maintainer's memory is one refactor away from being attributed to this phase. File it as a finding in `docs/CONFORMANCE.md`.

---

### Task 14: protoJS — the constrained build, and the phase's largest expected finding

**Files:** create `protoJS/tests/ConformanceHost.h`, `protoJS/tests/ConformanceTests.cpp`, `protoJS/conformance-allow.txt`, `protoJS/docs/CONFORMANCE.md`. Modify `protoJS/tests/CMakeLists.txt`.

**Interfaces:** protoJS uses **Catch2** with `catch_discover_tests` (`tests/CMakeLists.txt:4-29`), so it uses `protoCoreConformanceCatch2.h` — the same adapter as protoST.

- [ ] **Step 1: The build discipline, which is not negotiable**

```bash
R=/home/gamarino/Documentos/proyectos/protoJS
rm -rf $R/build_p4 && cmake -S $R -B $R/build_p4 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build $R/build_p4 --target protojs_conformance      # NO -j, ever
```
**No `-j` at all.** `-j >= 2` hangs DEV12 (memory: protoJS build parallelism). A separate `protojs_conformance` target keeps the conformance build to one translation unit plus a link, so the check is reachable without rebuilding the 34 ctest tests.

**Done when** the target links and `nproc`-parallel building was never invoked — check the shell history of the step.

- [ ] **Step 2: test262 must be unreachable from the conformance run**

```bash
R=/home/gamarino/Documentos/proyectos/protoJS
ctest --test-dir $R/build_p4 -N -R '^conformance' < /dev/null | tee /tmp/x >/dev/null
ctest --test-dir $R/build_p4 -N -R '^conformance' < /dev/null | grep -ci 'test262'; echo "want 0"
```
**Done when** the second command prints `0`. test262 is protoJS's own regression gate, not a conformance signal, and the runner must not be able to start it. Its baseline (`built-ins/{Object,Reflect,Proxy}` at 3619 passing) is recorded in Task 1 and **not re-measured here**. Note the discrepancy to settle with the maintainer: `protoJS/CONFORMANCE_JS.md:72` documents a *different* ten-pattern built-ins set (9,400 tests, 71.9%) as its conformance baseline, and the runner defaults to `TEST262_CONCURRENCY=1` (`tests/test262/runner/test262_runner.js:372-378`), which already matches the sequential rule.

- [ ] **Step 3: Rule 7 — the four finalizers, which are the phase's highest-value static catch**

```bash
R=/home/gamarino/Documentos/proyectos/protoJS
sed -n '118,140p' $R/src/HTTPModule.cpp        # freeServerState: thread.join() + rs->remove()
sed -n '728,744p' $R/src/HTTPModule.cpp        # freeClientRequestState: rs->remove()
sed -n '210,235p' $R/src/NetModule.cpp         # freeServerState / freeSocketState
sed -n '355,370p' $R/src/WorkerThreadsModule.cpp  # freeWorkerState
sed -n '24,35p'  $R/src/GCBridge.cpp           # finalizeJSValue -> QuickJS JS_FreeValue
python3 /home/gamarino/Documentos/proyectos/protoCore/scripts/conformance/check_static.py \
  --repo $R --check external_finalizer; echo "exit=$?"
```
**Done when** the checker reports all four as `error` with the contract quoted, and the report states the consequence in the kernel's own terms: each of these runs **on the single GC thread inside the sweep**, so a `thread.join()` there *"stalls collection for the whole space"* and — with a heap limit configured — *"stalls every mutator waiting for reclamation behind it"* (`protoCore/docs/MemoryModel.md` §5). `rs->remove()` is separately forbidden: a finalizer *"never publishes to a shared structure with compare-and-swap"* (`docs/GarbageCollector.md` §7), and `ProtoRootSet::remove` takes the set's internal mutex. `GCBridge.cpp:27-32` calling QuickJS from the GC thread is a fifth finding of a different kind — a cross-runtime concurrency hazard — and is reported at `warn` with a C7 question, because whether QuickJS's allocator is safe there is not protoCore's to decide.

- [ ] **Step 4: Rule 11 — verify protoJS's three thread shapes rather than failing them**

```bash
R=/home/gamarino/Documentos/proyectos/protoJS
grep -rn 'newThread' $R/src | head                      # the registered kind
sed -n '195,210p' $R/src/JSContext.cpp                  # pool shutdown joins, unwrapped
sed -n '230,240p' $R/src/JSContext.h                    # WorkerThreads own ProtoSpace
grep -rn 'enqueueCallback' $R/src | head -5             # the hand-off that holds nothing
```
The adaptor declares three kinds with §D12's verdicts: `"deferred-worker"` → `Registered`, `"io-pool"` → `HoldsNothing`, `"worker-thread"` → `OwnSpace`. **The case verifies each declaration** — `HoldsNothing` must allocate nothing, `OwnSpace` must report a different `ProtoSpace`.

**Done when** all three kinds are declared and verified, and the report records that protoJS is conforming here **in a shape the naive form of rule 11 would have failed** — which is §D12's entire justification and worth stating in `docs/CONFORMANCE.md` so the next reviewer does not "fix" it.

- [ ] **Step 5: `PROTOCORE_TRUST_SYMBOLS=1` — ask the maintainer first**

```bash
R=/home/gamarino/Documentos/proyectos/protoJS; S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
# STOP.  This re-runs protoJS's ctest suite, sequentially, with the kernel's
# defensive content fallback disabled.  It is the §D11 case and it is expected
# to fail loudly, because protoJS has 596 key-shaped fromUTF8String sites
# (corrected 2026-09-25 from 248, which no measurement supports: protoJS's own
# docs/CONFORMANCE.md "Rule 4" reports 1,401 fromUTF8String occurrences against
# 114 createSymbol, of which a defensible 596 are key-shaped, bounds 596-628,
# with the method recorded in the P4 report).
# ASK THE MAINTAINER BEFORE RUNNING IT, and never run a full test262 sweep here.
PROTOCORE_TRUST_SYMBOLS=1 ctest --test-dir $R/build_p4 --output-on-failure < /dev/null \
  2>&1 | tail -40 | tee $S/trustsymbols-protoJS.txt
```
**Done when** the maintainer has approved, the run has completed sequentially, and the **number of newly failing tests is recorded**. That number is §D11's answer for protoJS and the input to P5's scope. Per §D11 it is a **reported finding in P4, a hard failure from P5**, so a large number here does not fail this phase.

- [ ] **Step 6: Run, allowlist, review, commit**

As Task 11 Steps 3, 5, 7 and 8, with **no `-j`** throughout and `run.sh <build> 1 900` (one job, a longer timeout).

**Done when** `ctest --test-dir $R/build_p4 --output-on-failure < /dev/null` reports **34/34 plus the conformance entries**, the allowlist carries a justified line for every one of the 248 key-shaped sites **or** a single wildcard entry per file with a collective justification and a P5 ticket reference (247 individual justifications is a ritual, not a review — the checker must support a file-level entry for exactly this case, and Task 8 Step 1's format must allow `<path>:*`), and `docs/CONFORMANCE.md` answers C3, C5 and C7 including the `GCBridge` question.

---

### Task 15: The post-P3 rules — 9a, 9b and 9c

**Files:** create `protoCore/conformance/CaseModules.cpp`; extend `check_static.py` with `global_symbol_assumption` and `module_key_shape` (Task 8 Steps 6–7).

**Gate:** this task runs only if Task 1 Step 1 recorded protoCore at `2.2.0` or later. Below that, the cases are written, registered, and report `Skipped: requires protoCore >= 2.2.0 (P3)`, and Task 17 Step 4 records rule 9 as unverified. **The cases are written either way** — a case that exists and reports `Skipped` with a reason is a commitment; a case that does not exist is a gap that will be forgotten.

- [ ] **Step 1: `module.root_survives_cycle` — 9b, and the distinction P3 is built on**

```cpp
// Rule 9b -- the module list is a GC root.
//
// P3's central distinction, and the thing this case exists to keep true:
// "NEVER FREED IS NOT THE SAME AS IS A GC ROOT."  A perennial cell is not
// swept, but it is also not SCANNED, so the references it holds do not keep
// their targets alive.  A symbol gets away with perennial allocation alone
// because it is self-contained bytes.  A module object's contents are ordinary
// collectable objects in a space's heap, so the list must be a real root the
// mark enters through -- which is why P3 builds ModuleRootTable in the shape of
// TupleInterner (capture a published count under STW, walk the entries in
// concurrent mark) rather than simply leaking the list.
//
// The case: register a module through the kernel, drop every other reference to
// it, drive cycles, then read a variable out of it.  A module whose list is
// merely unfreed fails here and passes every "does it leak" test.
```
**Done when** the case passes against `SelfHost` post-P3 and fails against `NonConformingHost_UnrootedModule`, which registers the module object without rooting it.

- [ ] **Step 2: `module.alias_rejected` — 9c**

Uses `Host::loadSamePathTwoProviders()`. P3 §D11 rules the identity as **provider GUID + logical path + version**, canonicalised as `<providerGUID>\x1F<logicalPath>\x1F<version>`, with the empty version permanently reserved for "declares none". The case requires the two loads to be **distinct modules**. The bug it closes: `SharedModuleCache` was keyed by path with the provider prefix stripped, so `import st.counter_lib` and a local `counter_lib.scala` collapsed into one module with the first load winning — *"the same failure class as the 6-versus-7-byte interning bug: wrong answer, no error."*

**Done when** the case reports `Pass` for a post-P3 tree, `NotApplicable` for a runtime with no provider abstraction, and the static `module_key_shape` check reports zero path-only keys.

- [ ] **Step 3: 9a — the global-symbol census, reusing P3's own definition**

Run `check_static.py --check global_symbol_assumption` over all five runtimes. Both of P3's named shapes must be zero: **(a)** a raw symbol pointer cached in a static or global across space lifetimes, **(b)** code that assumes a symbol is valid only within one space.

**Done when** all five report zero, or every non-zero site is allowlisted with the reason. The one known candidate is protoST's `Bootstrap::Symbols` (32 members, `src/runtime/Bootstrap.h:114-176`), resolved in Task 12 Step 2. Note that **post-P3 this shape becomes *safe*** — interning is global, so a cached symbol pointer is valid for the process — which means the check's purpose changes from "this is a bug" to "this assumed a property that is now guaranteed; record that it depends on it". Write the finding's text accordingly, because a check whose message is wrong about *why* trains people to ignore it.

- [ ] **Step 4: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
git -C $P add conformance/CaseModules.cpp scripts/conformance/check_static.py
git -C $P commit  # "feat(conformance): the post-P3 module and interning rules (P4)"
```
**Done when** the cases appear in `--list` and report either a real result or `Skipped` with the version reason.

---

### Task 16: Documentation, version and the spec

**Files:** modify `protoCore/CMakeLists.txt`, `protoCore/CHANGELOG.md`, `protoCore/docs/GarbageCollector.md`, `protoCore/docs/MemoryModel.md`, `protoCore/docs/TESTING.md`, `protoCore/test/ConcurrentMarkSafetyTests.cpp`; create `protoScala/docs/platform/CONFORMANCE-SPEC.md`.

- [ ] **Step 1: Version `2.2.0` → `2.3.0`, SOVERSION unchanged**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
sed -i 's/^    VERSION 2\.2\.0$/    VERSION 2.3.0/' $P/CMakeLists.txt
grep -n 'VERSION 2\.3\.0\|PROTOCORE_ABI_SOVERSION' $P/CMakeLists.txt
```
**Done when** the version is `2.3.0`, `PROTOCORE_ABI_SOVERSION` is still `2`, and `CHANGELOG.md` records the addition under `## [2.3.0]`: the conformance library, the four headers, the two scripts, `docs/EMBEDDER-CONFORMANCE.md`, and — explicitly — that **nothing existing changed**, so `SameMajorVersion` consumers are unaffected.

- [ ] **Step 2: Fix the documentation gap `safepoint()` itself has**

`headers/protoCore.h:1655-1670` documents `safepoint()` as the STW-handshake hook and says nothing about young-generation submission — yet the implementation makes it *the only* submission point under `PROTOCORE_GC_REINCLUDE_SURVIVORS` (`core/ProtoContext.cpp:363-389`). **That omission is a plausible contributing cause of S15 and of protoClojure's revert**: an embedder reading only the header would conclude that a CPU-bound loop needs a safepoint and an allocating loop does not, which is the opposite of the truth for reclamation. Add to the doc comment:

```cpp
         * IT IS ALSO THE ONLY PLACE A CONTEXT'S YOUNG GENERATION IS SUBMITTED
         * to `dirtySegments` (under PROTOCORE_GC_REINCLUDE_SURVIVORS, and when
         * the per-context allocation threshold has been crossed).  A context's
         * young chain is recorded by GC Phase 2 as a ROOT, not as a candidate,
         * so a chain that is never submitted is live by construction and no
         * cycle can ever reclaim it -- however many cycles run.  An interpreter
         * that never calls safepoint() therefore reclaims NOTHING, silently,
         * while gcCycleCount keeps advancing.  See docs/EMBEDDER-CONFORMANCE.md
         * rule 1, and conformance case `gc.young_submitted`.
```
**Done when** the comment is present, `docs/GarbageCollector.md` §7 and `docs/MemoryModel.md` §5 both link to `EMBEDDER-CONFORMANCE.md`, `docs/TESTING.md` has its "Conformance" section, and the protoST/protoClojure history is cited in `EMBEDDER-CONFORMANCE.md` rule 1 as the motivating evidence. **This is the highest-leverage documentation change in the phase**: every embedder read that header and four of five got it right by other means.

- [ ] **Step 3: Annotate the trap in protoCore's own test suite**

At `protoCore/test/ConcurrentMarkSafetyTests.cpp:77`, above `ProtoContext threadCtx{&space};`:

```cpp
    // NOTE (P4, rule 11): a ProtoContext on a raw std::thread is NOT a
    // registered protoCore thread -- runningThreads moves only in the
    // ProtoThreadImplementation constructor (core/Thread.cpp:27), which runs
    // only for ProtoSpace::newThread.  GC Phase 2 walks space->threads to find
    // root-scanning candidates (core/ProtoSpace.cpp:381-411), so this thread's
    // automaticLocals, returnValue, pendingRoot and young chain are NOT
    // scanned.  That is safe HERE because everything this test holds is pinned
    // explicitly -- and it is a trap to copy into an embedder.  See
    // docs/EMBEDDER-CONFORMANCE.md rule 11 and case `thread.registered`.
```
**Done when** the comment is present, the test still passes, and `git diff --stat` for that file shows additions only.

- [ ] **Step 4: The spec**

`protoScala/docs/platform/CONFORMANCE-SPEC.md`, in the shape of `PMQ-SPEC.md` and `PROTOMAP-SPEC.md`: `## 1. Problem` (the five silent bugs, and why a review cannot see three of them) · `## 2. Semantics` (the twelve rules, the S/R/J classification, the `Host` capability contract, `NotApplicable ≠ Pass`) · `## 3. GC constraints (binding)` (the suite adds **no** stop-the-world work; it only observes) · `## 4. Tagged-pointer budget` (**none consumed**) · `## 5. Tests` (the case list and the self-check matrix) · `## 6. Rollout` (the five runtimes, their first-run results, and what P5 inherits) · `## 7. Decisions (P4)` (D1–D12 with their rulings).

Add to `protoScala/docs/DECISIONS-LOG.md` under a `## Phase P4 — Embedder conformance suite (2026-09-25)` heading, using the existing `| Date | Decision | Taken by | Where |` row format, with `[agent, pending review]` in the third column. New `STATUS.md` deviations, if any, continue from **D103** (`STATUS.md:795` is D102).

**Done when** the spec exists with all seven sections, the decisions log has one row per divergence from this plan, and every rule in the spec cross-references its case id and its protoCore citation.

- [ ] **Step 5: Commit**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore; R=/home/gamarino/Documentos/proyectos/protoScala
git -C $P add CMakeLists.txt CHANGELOG.md headers/protoCore.h docs/ test/ConcurrentMarkSafetyTests.cpp
git -C $P commit  # "docs(conformance): safepoint submits the young generation; rule 11 annotated (P4)"
git -C $R add docs/platform/CONFORMANCE-SPEC.md docs/DECISIONS-LOG.md docs/STATUS.md
git -C $R commit  # "docs(platform): the conformance spec (P4)"
```
**Done when** both commits exist and `ctest --test-dir $P/build_p4 --output-on-failure < /dev/null` still reports the Task 9 total.

---

### Task 17: Hand-off — the baseline, the surprises, and what P5 inherits

**Files:** create `/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance/RESULTS.md`; modify each runtime's `docs/CONFORMANCE.md` and `protoScala/docs/DECISIONS-LOG.md`.

- [ ] **Step 1: The result matrix, in the shape of the prediction matrix**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
for R in protoScala protoST protoClojure protoPython protoJS; do
  echo "=== $R ==="; grep -E '^(PASS|FAIL|NOTAPPLICABLE|SKIPPED|NEEDSREVIEW)' $S/conformance-$R.txt
done | tee $S/RESULTS-raw.txt
```
**Done when** `RESULTS.md` contains one table with the same rows and columns as Task 1 Step 6's prediction table, filled with observed results, and every cell carries the case's `detail` numbers.

- [ ] **Step 2: Diff observed against predicted, and explain every divergence**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
sha256sum -c $S/predictions.sha256    # the predictions must not have been edited
diff <(sed -n '/^| Rule/,/^| 12/p' $S/predictions.md) <(sed -n '/^| Rule/,/^| 12/p' $S/RESULTS.md) \
  | tee $S/divergences.txt
```
**Done when** the sha256 check passes — **if it fails, the predictions were edited after the fact and the phase's central claim is void** — and every line in `divergences.txt` has one paragraph in `RESULTS.md` explaining it. A divergence is not a failure of the phase; an **unexplained** divergence is. Add one row per divergence to `protoScala/docs/DECISIONS-LOG.md`.

- [ ] **Step 3: The self-check must be green, or nothing above is believable**

```bash
P=/home/gamarino/Documentos/proyectos/protoCore
ctest --test-dir $P/build_p4 -R ConformanceSelfCheck --output-on-failure < /dev/null
python3 $P/scripts/conformance/mutate.py --report | tee \
  /home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance/mutations.txt
```
**Done when** every `ConformanceSelfCheck.*` test passes, and `mutations.txt` shows one recorded green→red→green (or red→green→red) cycle **per runtime**. **A runtime with no recorded mutation cycle is reported in `RESULTS.md` as "suite not validated for this runtime"**, not as a pass — because that is exactly the claim protoST's 833 green tests made for five months.

- [ ] **Step 4: What P5 inherits, ordered by the cost of leaving it**

Write into `RESULTS.md` a ranked list with, for each item, the runtime, the rule, the citation and the failure mode. Ordered by **what the failure does**, not by how easy it is to fix:

1. **protoClojure rule 1** — reclaims nothing, for the whole process. Restore the safepoint reverted in `dde94c8`; the revert's premise (that protoCore's allocation-budget trigger was going away) is false, since `PROTOCORE_GC_REINCLUDE_SURVIVORS` is `ON` by default.
2. **protoJS rule 7** — four finalizers that `join()` and call `rs->remove()` on the GC thread inside the sweep, stalling collection for the whole space.
3. **protoClojure rule 2** — three unwrapped joins; a partial fix a green suite certified.
4. **protoPython rule 5** — a fresh perennial `ProtoTuple` per AOT call site and per `BUILD_TUPLE`; the heap grows with call count and never shrinks.
5. **protoPython rule 6** — `py_bytes_find`'s dead `== nullptr` guard.
6. **protoJS rule 4 / §D11** — the `PROTOCORE_TRUST_SYMBOLS=1` failure count, and the residual `__`-prefixed uninterned keys.
7. **Every runtime, rule 8 as a design question** — nobody calls `setHeapLimits` and nobody installs `outOfMemoryCallback`, so no runtime in the family has a bounded heap or a recovery hook. Whether they should is a maintainer decision, and `docs/MemoryModel.md` §6 is the procedure.
8. **protoST S18** — the debugger test that blocks on an open stdin.
9. **protoPython `protopy_import_site`** — a pre-existing failure with no marker in the repository.

**Done when** the list exists, each item names its file and line, and each is either accepted into P5's scope or explicitly deferred by the maintainer with a reason.

- [ ] **Step 5: The two questions this phase leaves open, stated rather than buried**

Record in `RESULTS.md`:

1. **Is `getOwnAttributeDirect`'s conflation of "absent" with "not an object cell" acceptable long-term?** It is the root of rule 6's whole existence and of §D11's severity split. P4 documents it; closing it is an ABI-visible semantic change and needs a maintainer ruling.
2. **Should `safepoint()` be split into two methods** — one that parks and one that submits? Today one call does both, which is why its header documentation described only half of it for months (Task 16 Step 2). A separate `submitYoungGeneration()`-style public hook would let an embedder be explicit, at the cost of one more obligation to forget. **The recommendation is no** — one call that does the right thing is more robust than two that can be mismatched, and the fix is the documentation, which Task 16 Step 2 applies.

- [ ] **Step 6: Final verification across the family**

```bash
S=/home/gamarino/Documentos/proyectos/.agent_scratch/p4-conformance
{ echo "protoCore  $(ctest --test-dir /home/gamarino/Documentos/proyectos/protoCore/build_p4 < /dev/null 2>&1 | tail -3)"
  for R in protoScala protoST protoClojure protoPython; do
    B=build_release; [ $R = protoST ] && B=build
    echo "$R  $(ctest --test-dir /home/gamarino/Documentos/proyectos/$R/$B < /dev/null 2>&1 | tail -3)"
  done
  echo "protoJS  $(ctest --test-dir /home/gamarino/Documentos/proyectos/protoJS/build_p4 < /dev/null 2>&1 | tail -3)"
} | tee $S/final.txt
```
**Done when** `final.txt` shows, for every runtime, its Task 1 baseline **plus** its conformance entries, with **no previously-passing test newly failing** and every conformance failure matching either a Task 1 Step 6 prediction or an explained divergence. Then, and only then, is the phase complete: the deliverable is a **trustworthy baseline and a suite that has been shown to fail**, not a green board.

---

## Self-review against the maintainer's instruction (done while writing; gaps fixed inline)

- *"An audit phase for all embedders that checks protoCore's rules"* — five runtimes, twelve rules, one shared suite (Tasks 11–14). ✔
- *"Evaluate the suite-versus-review shape rather than accept it"* — §Motivation argues **for** the suite on evidence the maintainer did not cite: three of the five bugs were unreachable without a working collector, so a review on 2026-09-23 would have found none of them. ✔
- *"Decide what genuinely can be mechanised and what needs human judgement, and say so per rule"* — the rule table's Mechanism column, with §D4 listing the three **J** items and §D5/§D6/§D11/§D12 arguing four reclassifications: rule 5 **up** to a measurement, rule 6 **sideways** into a per-function table, rule 4 **split by destination** plus a runtime case the kernel already ships, rule 11 **widened** to three conforming shapes. ✔
- *"A plan that claims to mechanise a judgement call is worse than one that admits the split"* — rule 3's general form is **declined** in Task 5 Step 1 with the reason (escape analysis over the whole call graph, unknown false-negative rate, and *worse for being believed*). ✔
- *"Every GC test must assert a reclamation consistent with the garbage it created, and the plan must say this explicitly"* — a Global Constraint, `CycleDriver.h`'s API offers no vacuous question, `kReclaimFraction` is argued in Task 3 Step 2, the 200,000-cell floor is fixed, and Task 2 Step 2's done-when greps for the forbidden shapes. ✔
- *"Read `docs/MemoryModel.md` and cite it"* — §5 is cited for the finalizer contract, the non-blocking rule, the perennial-wrapper trap, the accounting division of labour and the "why going further is impossible" boundary that C7 inherits verbatim. ✔
- *"Read the P3 plan if it exists"* — it does (1,868 lines); rule 9 uses its §D11 identity ruling, its §D12 protoPython `moduleRoots` migration, its Task 9 census shapes, and its "never freed ≠ is a root" distinction. ✔
- *"Predict each embedder's expected result so a surprise is visible as a surprise"* — Task 1 Step 6, written and **hashed** before any case exists, with eight staked predictions and a sha256 check in Task 17 Step 2 that voids the phase's claim if the file was edited after the fact. ✔
- *"Note protoST's S18"* — a Global Constraint, Task 1 Step 4, Task 2 Step 7's runner, and Task 12 Step 3. ✔
- *"Per task: exact Files, Interfaces, numbered 2–5 minute steps with real code, and a done-when verifiable by a command. No placeholders."* — 18 tasks. The code is real and cited; where a body is elided it is elided **inside** a fully-specified signature, never as a `TODO`. ✔
- **A gap found while writing and fixed:** the first draft made rule 4 a pure grep. Reading `core/ProtoObject.cpp` showed `setAttribute` auto-interns, `getAttribute` falls back to content, and `getOwnAttributeDirect` does neither — so the severity depends on the destination, and the kernel already ships `PROTOCORE_TRUST_SYMBOLS=1` as the exact runtime check, unused for two months. §D11 exists because of that read. **A plan that had not read the kernel would have shipped a check that was wrong about why.**
