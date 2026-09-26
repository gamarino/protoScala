# The quiet window — 2026-09-26

> **This report supersedes numbers; it deletes nothing.** It settles all four
> questions that [`2026-09-26-quiet-host-attempt.md`](2026-09-26-quiet-host-attempt.md)
> put to a host that was not quiet, two of which that report correctly refused
> to answer. Earlier reports stand as written and are cited where this one
> disagrees with them.

The host **was** quiet this time, and that single fact changes three of the four
answers. The gate was re-verified immediately before each measurement with
`mpstat -P ALL 5 3`, and every measurement carries its own three idle samples.

| # | Question | Outcome |
|---|---|---|
| 1 | Cold-start budget, `< 25 ms` | **MET — certified.** The 2026-09-26 `MISSED` verdict is withdrawn; 23.73 ms reproduces and is beaten |
| 2 | ThreadSanitizer against the actors | **`g_threadBlueprint` gone; no protoScala-sited report in nine workloads** — but that is a non-reproduction, not a clearance (§2.4). protoCore's sparse-list reports judged **false positives**, with a reason |
| 3 | Re-measure the published tables | **Done.** Actor and general-suite tables re-measured; the general suite moved **against** protoScala |
| 4 | Saturation curve: bend at `w=6`? SMT regression? | **Both settled.** Peak moves to **w=6**; the SMT regression **appears**, in the load control too |

---

## 0. The gate, and a precondition that did not hold

### 0.1 The four gate readings

`mpstat -P ALL 5 3`, the `all` row of each sample. Busy CPUs is
`12 × (100 − %idle) / 100` on this 12-CPU host.

| before measurement | idle samples (%) | mean | busy CPUs | load avg (1 m) |
|---|---|---:|---:|---:|
| 1 — cold start | 95.59 / 94.97 / 95.61 | **95.39** | 0.55 | 1.68 |
| 2 — ThreadSanitizer | 95.79 / 94.64 / 93.23 | **94.55** | 0.65 | 1.18 |
| 3 — saturation curve | 95.15 / 95.46 / 95.67 | **95.43** | 0.55 | 0.42 |
| 4a — actor table | 94.30 / 95.49 / 95.35 | **95.05** | 0.59 | 1.22 |
| 4b — general suite | 94.57 / 95.23 / 95.49 | **95.10** | 0.59 | 1.27 |

All five pass the ≥ 92 % threshold, and all five are **quieter than the 93.14 %
entry condition**. No measurement was taken below the gate, and none had to be
discarded.

**This is the operand the previous window lacked.** That report gated partly on
`uptime`'s load average and recorded its quietest cold-start round at "load
1.84" — but its own `mpstat` never put the foreign load below **2.3 busy CPUs**.
This window ran at **0.55–0.65 busy CPUs**, roughly **four times quieter** in the
metric that matters. Load average and busy CPUs are not interchangeable, and the
gap between them is the whole of §1.

### 0.2 A precondition that was stated and did not hold

The task recorded that a sibling agent in protoScala had been asked to do
documentation-only work and run no builds. **It was verified rather than
trusted, and it was not the case.** In `protoScala/`, timestamps show that
74 seconds before this window opened:

- `build_release/protoscala`, `protoscalac`, `protoscala-precompile` and
  `libprotoScala.so.0.6.0` were **rebuilt at 17:35:34**;
- a benchmark harness ran and wrote
  `benchmarks/reports/2026-09-26-interpreted-vs-transpiled.md` at **17:44:52**;
- `CHANGELOG.md`, `docs/STATUS.md`, `docs/ROADMAP.md` and
  `docs/DECISIONS-LOG.md` were edited at 17:40.

Nothing of it was *running* during any measurement here (checked by `pgrep` for
compiler and harness processes before each gate, and confirmed by the gate
readings themselves). The consequence for this report is not contention but
**reproducibility**: that tree carries uncommitted source and doc changes, so
nothing measured in it could be tied to a commit.

**Every measurement below was therefore taken in a separate `git worktree` at
`f835aee`**, with its own build directories, so each figure names a commit:

```
.agent_scratch/quiet-window/tree            worktree at f835aee
.agent_scratch/quiet-window/tree/build_qw_rel    Release
.agent_scratch/quiet-window/tree/build_qw_rwdi   RelWithDebInfo
.agent_scratch/quiet-window/tree/build_qw_tsan   RelWithDebInfo + TSan
.agent_scratch/quiet-window/protoCore-tsan       protoCore 2.5.0 + TSan (out-of-source)
```

Every binary was `ldd`-checked against the workspace `libprotoCore.so.3` →
**2.5.0**, never `/usr/local`'s 1.0.0. protoCore's own tree was not written to.

---

## 1. Cold start: the budget is MET, and 23.73 ms reproduces

**Verdict: `< 25 ms` is met, on both builds, in both modes.
`benchmarks/cold-start.sh` exited 0 in 24 of 24 cells.** The `MISSED` verdict of
2026-09-26 is withdrawn as a **load artefact**.

### 1.1 Three interleaved rounds, both builds, plus the image control

21 runs per cell, **all 378 runs verified**, gate §0.1 row 1:

| build / prelude path | mode | r1 | r2 | r3 | median of medians |
|---|---|---:|---:|---:|---:|
| Release, image | script | 22.36 | 21.57 | 21.63 | **21.63** |
| Release, image | repl | 22.83 | 22.32 | 22.14 | **22.32** |
| RelWithDebInfo, image | script | 21.73 | 21.97 | 21.41 | **21.73** |
| RelWithDebInfo, image | repl | 22.20 | 22.44 | 21.88 | **22.20** |
| Release, `PROTOSCALA_PRELUDE_NO_IMAGE=1` | script | 24.32 | 24.09 | 23.89 | **24.09** |
| Release, `PROTOSCALA_PRELUDE_NO_IMAGE=1` | repl | 24.77 | 24.66 | 24.44 | **24.66** |

Per-cell spreads ran 19.88–34.05 ms; the widest cell was Release/image/repl
round 1 (21.31–34.05).

- **Release and RelWithDebInfo are indistinguishable** — 0.10 ms and 0.12 ms
  apart on the median-of-medians, against 1.5–12.7 ms within-cell spreads. This
  confirms the previous window's one load-independent finding.
- **The image is worth 2.46 ms (script) and 2.34 ms (repl)**, which agrees with
  the 1.92 / 2.12 ms originally measured at 0.6.0 and with the 2.46 ms the
  in-process stage measurement predicted.

### 1.2 Does 23.73 ms reproduce? Yes — and the control is decisive

The cleanest available experiment is the **same binary, different load**.
`build_rwdi/protoscala` was built at 08:42 on 2026-09-26 for the previous
window, has not been rebuilt since, and was measured then at load 4.03–4.22.
Re-running that unchanged binary here:

| binary (unchanged since the loaded window) | script | repl | measured before | Δ |
|---|---:|---:|---|---:|
| `build_rwdi/protoscala` | **22.16** | **22.02** | 28.37 / 27.97 | **−6.21 / −5.95** |
| `build_release/protoscala` | **21.42** | **22.26** | 26.38 / 25.43 | **−4.96 / −3.17** |
| `build_pkg/protoscala` (what the package ships) | **21.34** | **21.34** | — | — |

Same binary, same harness, same tree; only the load differs. **Load was the
variable all along.** Five independently built binaries now land in
21.34–22.32 ms, all below the published 23.73 / 23.89 ms.

**So the 23.73 ms is reproduced and beaten, and it should no longer be described
as unreproducible.** What the previous report got right was refusing to explain
the gap; what it got wrong was concluding that load could not be the
explanation, because it compared a load *average* of 1.84 against a busy-CPU
floor it had itself measured at 2.3.

### 1.3 A reporting bug found while doing this

`build_release/protoscala --version` prints

```
prelude: compiled at start-up (no precompiled image in this build)
```

**and this is false.** The image is present and in use:
`build_qw_rel/generated/PreludeImage.cpp` is 425 KB / 2,353 lines,
`nm` shows `protoScala::preludeImageData()` and
`protoScala::buildPreludeImage(...)` exported from `libprotoScala.so.1`, and
setting `PROTOSCALA_PRELUDE_NO_IMAGE=1` moves the median by 2.4 ms — which it
could not do if there were no image to disable.

The cause is a lost usage requirement.
`target_compile_definitions(protoscala_runtime PUBLIC PROTOSCALA_HAVE_PRELUDE_IMAGE)`
still reaches `src/runtime/Prelude.cpp`, which is *in* that object library and
does take the image path. It no longer reaches `src/main.cpp`, which is in the
`protoscala` executable and links the **shared library** `protoScala` — a change
that arrived with `3fa7ff0` ("publish libprotoScala.so with one installed
header") at 10:21 today. `build_pkg` and `build_rwdi` were configured before
that commit and still report correctly, which is why the three build trees
disagree.

**Consequence: `--version` is not a valid oracle for whether the image is in
use in a post-`3fa7ff0` build, and the previous report's confirmation by that
route was reading a string that had stopped being true.** Its substantive
conclusion — that the binary *was* using the image — happens to be right.
Not fixed here; it is a one-line CMake change in a tree another agent holds.

### 1.4 The two operands, carried forward and re-measured

- **The harness's own floor is ~4.7 ms on this host**: the identical construct
  around `/bin/true` measures **4.67 ms** (21 runs, 3.20–5.66), `/bin/echo hi`
  5.07 ms, `sh -c true` 4.46 ms. Still **stated and not deducted** — the
  done-when in `docs/DECISIONS-LOG.md` is the script's exit status.
  A useful cross-check: 42 bare runs of the binary with no harness around them
  take 0.819 s wall, i.e. **19.5 ms per run**, against the harness's 21.6 ms
  median. The floor is *not* simply additive, so the ~5 ms should not be
  subtracted from 21.6 to claim 16.6.
- **Start-up is still kernel-bound**: across the same 42 runs, **0.172 s user
  against 0.669 s sys** — sys is 3.9× user. A direction to investigate, not a
  defect, exactly as before.

### 1.5 A hypothesis raised and refuted

This host is `amd-pstate-epp`, governor `powersave`, EPP `balance_power`,
1.11–4.06 GHz. That raises a real candidate: a 22 ms burst on an *idle*
machine might never boost, so a quiet host could be the *wrong* host for a
cold-start measurement and a loaded host could look faster.

**The measurement refutes it for this workload.** The quiet host is 3–6 ms
*faster*, not slower, in every cell. Contention dominates clock ramp here.
Recorded because it is a trap worth naming, and because it is the reason the
mean CPU MHz is logged beside each gate reading (1,746–3,410 MHz across the
five gates, with no corresponding movement in the medians).

### Reproducing

```bash
git worktree add <scratch>/tree f835aee
cmake -B build_qw_rel  -S . -DCMAKE_BUILD_TYPE=Release
cmake -B build_qw_rwdi -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build_qw_rel --target protoscala -j4
ldd build_qw_rel/protoscala | grep protoCore      # never /usr/local/lib
benchmarks/cold-start.sh ./build_qw_rel/protoscala 21   # as of 2026-09-26: exit 0
PROTOSCALA_PRELUDE_NO_IMAGE=1 \
  benchmarks/cold-start.sh ./build_qw_rel/protoscala 21 # as of 2026-09-26: exit 0, ~2.4 ms slower
```

---

## 2. ThreadSanitizer: protoScala is clean; protoCore's reports are false positives

Built with `-DPROTOSCALA_SANITIZER=thread` (added in `2eb662d`) against a
matching TSan protoCore 2.5.0. **`setarch -R` is needed at both sites** — the
test run *and* the build, because the precompiled prelude runs an instrumented
`protoscala-precompile` at build time. Confirmed: the wrapped build completed in
48.3 s and did **not** abort at 45 %.

### 2.1 `g_threadBlueprint` is gone

`grep` finds no `g_threadBlueprint` in `src/` — `63d3505` removed it — and,
more to the point, **TSan no longer reports it in any workload**: zero
occurrences across the six actor cases, the 8×25,000 stress and the no-actor
control, against 2 occurrences in the previous control alone.

**Across all nine workloads, no protoScala frame is the access site of any
report.** This was checked rather than assumed: over every report in every log,
frame `#0` of every `Read`/`Write`/`Previous write` line is a protoCore file, and
every `SUMMARY` site is a protoCore file (the breakdown is in §2.3).

**This is a non-reproduction, not a clearance — see §2.4.** `ActorScheduler.cpp`
appears 6,934 times in these logs as a *caller* frame, never as an access site.
A race is nondeterministic, and its absence from nine runs on one host does not
retire it.

### 2.2 Results by site, and what is unchanged

`halt_on_error=0`, so counts are distinct reports. Counts vary run to run with
the interleaving and are *not* a metric; what matters is the site.

| workload | reports (2026-09-26, quiet) | previous | protoScala-sited |
|---|---:|---:|---:|
| `hello.scala` (single-threaded) | **0** | 0 | 0 |
| `13-actors/ask-with-no-reply` | 1 | 7 | 0 |
| `13-actors/await-one-worker` | 15 | 15 | 0 |
| `13-actors/many-actors` | 26 | 24 | 0 |
| `13-actors/spawn-and-send-indent` | 59 | 55 | 0 |
| `13-actors/stats` | 150 | 78 | 0 |
| `13-actors/priority-bands` | 90 | 88 | 0 |
| 8 threads × 25,000 sends into one actor | 760 | 989 | 0 |
| 8 plain `Thread.start`, **no actor** | 7 | 15 | 0 |

**The correctness invariants still hold.** The stress printed **`200000`,
exactly right** — no message lost, duplicated or concurrently handled — and the
control printed `plain-threads-done`. Under TSan the stress took **82 s** here
against 237 s in the loaded window, which is one more operand on how much the
host mattered. The stress's 760 reports sit inside the 727 / 770 / 805 range
recorded for post-`63d3505` runs, so this run is consistent with those rather
than better.

Site breakdown over all nine logs, by `SUMMARY` line — **every site is a
protoCore file**: `SparseListAlgorithms.h` 1,038 (lines 94–98 dominate with
831), `ProtoObject.cpp` 43, `ProtoString.cpp` 7, `Thread.cpp` 7,
`ProtoSpace.cpp` 2, `proto_internal.h` 2.

### 2.3 The 13 protoCore sparse-list races: **false positives**

The control — 8 plain threads that touch no actor — reported 7 protoCore races
this time (6 in `SparseListAlgorithms.h` `getAt`, 1 in
`ProtoObject::getOwnAttributeDirect`); the previous run's 13 came from the same
sites plus `removeAt`, which did not recur. **Verdict: TSan false positives
arising from arena cell reuse that TSan cannot see.** Four independent reasons,
in descending order of strength:

**1. Every "Previous write" is a constructor.** Read off the reports, without
exception:

```
Previous write of size 8 ... by thread T1:
  #0 proto::ProtoSparseListImplementation::ProtoSparseListImplementation(...)
       protoCore/core/ProtoSparseList.cpp:64
  #1 setAt<proto::ProtoSparseListImplementation>  SparseListAlgorithms.h:118
```

A constructor writes memory that **no other thread can legitimately hold a
reference to yet** — the object does not exist until it returns, and is
published only afterwards. So the reader in `getAt` cannot be reading *that*
object. The only way both accesses land on the same bytes is that the cell
previously held a *different* object.

**2. The algorithm never writes a published node.** `SparseListAlgorithms.h`
is a genuinely persistent AVL tree: every `setAt` and `removeAt` path is
`new(context) Node(...)` and returns the new node — there is no in-place
mutation of an existing node anywhere in the file — and `getAt` performs only
const reads of `isEmpty`, `key`, `previous`, `next`, `value`. A real data race
needs a write to a live node, and no such write exists in the code TSan blames.

**3. The arena defeats TSan's shadow invalidation.** All 7 reports name **one**
location: `heap block of size 16777216` — the 16 MiB
`kMaxBytesPerOSAllocation` that `ProtoSpace.cpp:1895` obtains with a single
`posix_memalign`. TSan clears a location's access history on `free()`.
protoCore never frees a cell; it recycles cells through per-context freelists
and GC sweep, with **zero `__tsan_*` or `ANNOTATE_*` annotations anywhere in
protoCore**. So a write recorded when address X held node A stays in the shadow
for the life of the process and is reported against a later read of node B at
the same X, with no happens-before edge required or expected.
Corroborating detail: the racing offsets recur at cell-relative 0/8/16/24/56/60
— `key`/`value`/`previous`/`next` and a compiler-coalesced 5-byte
`height`+`isEmpty` store — and TSan pairs a **"write of size 5" at `…f8f8`
against a "read of size 1" at `…f8fc`**, two different access shapes over the
same bytes.

**4. The control cannot contain a genuine race.** `plain_threads.scala`'s eight
threads each only increment two private locals and share no user object. Two
threads' private frames cannot genuinely race, yet the same `getAt` reports
appear. That is an address collision in a shared arena, not contended state.

**What this does not prove, stated plainly.** Identical TSan output would be
produced by a *real* bug in which the GC recycles a cell still reachable by a
running reader. The stacks alone cannot separate that from benign reuse. It is
strongly disfavoured — no in-place write exists, the 8×25,000 stress computed
exactly 200000, and the 1,344-case suite passes, none of which a live-cell
recycle would survive — but it is not excluded. **The way to settle it to
certainty** is to annotate protoCore's freelist handoff with
`__tsan_acquire`/`__tsan_release` (or poison recycled cells) and re-run: real
races survive annotation, reuse artefacts vanish. That was not done here,
because protoCore was not to be modified. This also gives a concrete reading of
the "40–116-warning benign baseline" `protoCore/docs/FIELD-NOTES.md` records:
benign is the right call, and now it has a mechanism.

### 2.4 The scheduler race did not reproduce, and is **not** cleared

While this window was running, a documentation-only commit — `4908bce`,
"docs(actors): diagnose the scheduler's second race" — diagnosed
`ActorScheduler.cpp:227` (line 229 at `f835aee`) as a **real protoScala defect**
by reading the source: `finishTurn`'s non-suspended path CASes `sched` 1→0 and
*then* calls `hasWork`/`highestPendingBand`, which read `ActorState::pendingIdx[b]`
and walk the actor's `__pend<b>__` list while the new owner's `nextMessage`
writes both (`nextMessage` reads and increments `a->pendingIdx[band]` at lines
223–226 and re-stores `pendingKey` at 233).

**That race did not appear in any of the nine workloads measured here.**
`pendingIdx` is a plain `unsigned` in protoScala's own struct, so a report
against it would have put a protoScala frame at `#0`, and none did.

**This is reported as a non-reproduction and nothing more.** It does not
contradict that diagnosis and must not be quoted as retiring it: a data race is
nondeterministic, TSan reports only the interleavings it happens to observe, and
nine runs on one host is not a proof of absence. Both statements are true
together — *this* run's reports are all protoCore-sited, *and*
`ActorScheduler.cpp` holds a defect that has been diagnosed from the source and
is pending a fix. The fix's own check should be a re-run of the TSan stress case
compared against the 727 / 770 / 805 / **760** post-`63d3505` totals.

---

## 3. The published tables, re-measured

### 3.1 Actors — 7 modes, 5 samples per cell, interleaved with protoClojure

Gate §0.1 row 4a. protoScala `f835aee`, protoCore `da5f19e2` (2.5.0),
protoClojure `39d8353`, **ProtoMPSCQueue mailboxes** (the superseded table was
measured on CAS-list mailboxes at load 3.59→9.09). Every cell verified its own
message count. Full cells:
[`2026-09-26-qw-actors.md`](2026-09-26-qw-actors.md) and
[`2026-09-26-qw-saturation.md`](2026-09-26-qw-saturation.md).

| Mode | peak msg/s (median) | at | protoClojure at the same worker count | ratio |
|---|---:|---|---:|---:|
| single | 212,406 [210,807-230,738] | 2 workers | 257,742 [241,538-268,202] | **0.82×** |
| fan-out | 243,108 [233,633-252,268] | 2 workers | 483,679 [450,546-493,075] | **0.50×** |
| ping-pong | 26,939 [26,298-27,588] | 4 workers | no twin | — |
| await | 67,271 [66,136-71,312] | 6 workers | no twin | — |
| priority | 157,089 [153,404-160,404] | 2 workers | no twin | — |
| saturation-8 | 2,947 [2,567-3,034] | 12 workers | 2,679 [1,939-3,020] | **1.10×** |
| saturation-32 | 3,373 [3,178-3,402] | 6 workers | 2,874 [2,655-2,979] | **1.17×** |

**Where protoScala loses, plainly.** On `fan-out` it does not merely lose, it
loses *increasingly*: protoClojure scales 292,983 → 670,082 msg/s from 1 to 4
workers while protoScala peaks at 2 workers and then falls, so the ratio decays
**0.67× → 0.50× → 0.34× → 0.32× → 0.29× → 0.23×** at w = 1, 2, 4, 6, 8, 16.
On `single` the ratio decays 0.84× → 0.59× over the same range, because
protoScala's rate *falls* with added workers (210,572 → 149,160) where
protoClojure's is flat (~253,000). Both are consistent with the single-method
invariant serialising one actor while the added workers still cost
synchronisation — protoScala pays for workers it cannot use. This is reported as
measured; the positioning is an agile, interoperable, easily integrable and very
simple Scala, **not a fast one**.

protoScala leads only on the two CPU-bound saturation shapes (1.10× and 1.17×),
where the work per message is large enough to dominate mailbox cost.

Ask latency, pooled 5,000 samples per worker count: p50 rises 24.4 → 33.9 µs
from w=1 to w=16, p99 2,131 → 2,486 µs.

### 3.2 The general suite — and it moved **against** protoScala

Gate §0.1 row 4b. 5 runs per cell, cold process, every printed result verified.
Full table: [`2026-09-26-qw-suite.md`](2026-09-26-qw-suite.md).

| Workload | protoScala (ms) | CPython 3.14 (ms) | ÷ CPython | was (contended) |
|---|---:|---:|---:|---:|
| `int_sum_loop` | 24.1 [23.4-24.9] | 32.1 [31.4-32.8] | 0.75× | 0.65× |
| `fib` | 49.3 [49.1-49.3] | 38.1 [37.6-39.8] | 1.29× | 1.07× |
| `str_concat` | 20.8 [20.5-22.0] | 28.6 [27.5-29.6] | 0.73× | 0.57× |
| `range_iterate` | 23.6 [23.3-24.2] | 31.5 [31.1-32.6] | 0.75× | 0.63× |
| `tak` | 27.2 [26.1-28.4] | 29.9 [29.8-35.6] | 0.91× | 0.72× |
| `fib30` | 370.3 [356.8-393.9] | 140.6 [136.4-147.5] | 2.63× | 2.07× |
| `sum_loop` | 63.5 [62.3-64.4] | 76.4 [74.2-77.7] | 0.83× | 0.75× |
| `factorial_100` | 18.9 [17.6-20.6] | 28.2 [27.4-28.4] | 0.67× | 0.54× |
| `attr_lookup` | 37.3 [36.4-39.3] | 37.9 [37.3-41.5] | 0.98× | 0.79× |
| `object_tree` | 382.0 [376.5-414.0] | 166.6 [165.4-170.4] | 2.29× | 1.95× |
| `list_ops` | 398.2 [393.3-418.9] | 1562.8 [1539.3-1867.7] | 0.25× | 0.24× |
| `map_build` | 299.9 [294.6-306.4] | 63.3 [62.6-66.6] | 4.74× | 4.43× |
| **Geomean vs CPython (12)** | | | **1.06×** | **0.91×** |

**The quiet host made protoScala look worse, not better, and that is the
honest headline of this section.** The geomean against CPython moved from
**0.91× to 1.06×** — from 9 % faster to 6 % slower — and **every one of the
twelve ratios moved against protoScala.**

The mechanism is in the operands: **CPython gained more from the quiet host
than protoScala did.** CPython's medians fell 16–22 % (`fib30`
179.6 → 140.6, `object_tree` 209.1 → 166.6, `int_sum_loop` 38.0 → 32.1) while
protoScala's fell 0.5–15 % (`fib30` 372.3 → 370.3, `object_tree`
406.9 → 382.0, `int_sum_loop` 24.6 → 24.1). **The contended figures flattered
protoScala**, and the direction is the one that matters: a re-measurement on a
quiet host is not automatically a better headline, and this one was not.
This report does not isolate *why* CPython is the more load-sensitive of the
two — a plausible candidate is that protoScala's ~20 ms kernel-bound start-up
is a large, load-insensitive constant in every cell — and that is left
**unsettled** rather than asserted.

Cold start under the suite's stricter three-way verdict (MET only when even the
worst sample is below target): **script STRADDLES** (median 21.94, worst 25.54),
**repl MET** (median 21.69, worst 23.36). Both readings agree with §1 on the
median; the script's worst sample still crosses 25 ms on a daily-driver desktop.

### 3.3 One column could not be measured, and the harness caught it

All 12 `protoScala Release` cells report **FAILED**, and its cold-start cells
report **0/21 verified** beside a 4.45 ms median. That 4.45 ms is not a win; it
is a crash. `build_bench/protoscala` (2026-09-24) links
**`libprotoCore.so.2`**, a SOVERSION that no longer exists in the workspace, and
dies in the loader:

```
error while loading shared libraries: libprotoCore.so.2: cannot open shared object file
```

**The harness behaved exactly as this project requires** — it verified printed
output, refused the cell, and named it — which is the mechanism
`feedback_benchmarks_must_self_report` exists for, working. The column is
omitted from the table above rather than published. Any report carrying a
`protoScala Release` column measured after protoCore went to SOVERSION 3 should
be re-checked; `build_bench` needs a rebuild before that slot means anything.

---

## 4. The saturation curve: the bend moves to w=6, and SMT regresses

Gate §0.1 row 3. w = 1, 2, 3, 4, 5, 6, 8, 12, 16; 5 samples per cell;
protoClojure's twin interleaved at every `(mode, w)` as the load control; the
saturation scripts assert the computed sum, not the exit code. Full cells:
[`2026-09-26-qw-saturation.md`](2026-09-26-qw-saturation.md).

Speedup against each series' own `w=1`, from the medians:

| series | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 | peak |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| protoScala `saturation-32` | 1.00× | 1.91× | 2.79× | 3.56× | 3.80× | **4.23×** | 3.71× | 2.40× | 2.30× | **4.23× @ w=6** |
| protoClojure `saturation-32` (control) | 1.00× | 1.89× | 2.77× | 3.46× | 3.83× | **3.99×** | 3.76× | 2.39× | 2.34× | 3.99× @ w=6 |
| protoScala `saturation-8` | 1.00× | 2.04× | 2.57× | 3.56× | 3.46× | 3.36× | 3.14× | 3.59× | 3.32× | 3.59× @ w=12 |
| protoClojure `saturation-8` (control) | 1.00× | 1.89× | 2.38× | 3.42× | 3.32× | 3.00× | 3.02× | 3.67× | 3.23× | 3.67× @ w=12 |

*Previously, contended (v5): protoScala `saturation-32` read
1.00 / 2.02 / 2.82 / 3.43 / 3.60 / 3.68 / 3.90 / 3.97 / 3.93, peak 3.97× @ w=12.*

### 4.1 Does the bend move to w=6? **Yes, on `saturation-32`.**

The near-linear region now runs to **w=4**, and the curve keeps *rising* to a
peak at **w=6 — exactly the six physical cores**. Per-worker efficiency:
**95.7 % at w=2, 93.1 % at w=3, 88.9 % at w=4**, then 76.0 % at w=5 and 70.5 %
at w=6. Under contention the same series bent at w=3 (efficiency 61 % by w=6)
and peaked at w=12.

So the v5 report's open question resolves the way it suspected: the bend at
`w=3` **was** the machine's spare capacity, not the scheduler. The peak moved
from w=12 to w=6, and 3.68× at w=6 became **4.23×**.

### 4.2 Does the SMT regression appear? **Yes, decisively — and in the control too.**

Above the 6 physical cores `saturation-32` **falls**: 4.23× → 3.71× (w=8) →
2.40× (w=12) → 2.30× (w=16), a **46 % loss from peak**. The v5 reading of
"flat, not falling" (3.90 / 3.97 / 3.93) is overturned as a contention artefact
— on a loaded host the extra workers were competing with foreign load, which
flattened exactly the region the question was about.

**The control is what makes this a result rather than a protoScala finding.**
protoClojure's twin, measured back to back at every worker count, traces the
same shape: 3.99× → 3.76× → 2.39× → 2.34×. Two independent runtimes on the
same protoCore, same hardware, same moment, both regressing above 6 workers.
That is a property of the **platform and the machine**, not of protoScala's
scheduler, and it reproduces the shape protoST documented (3.11× at w=6 with
regressions at 8 and 12).

### 4.3 What `saturation-8` does not settle

`saturation-8` does **not** show the clean shape: it peaks at w=12 (3.59×),
dips at w=6, and its spreads are wide ([2,549-3,220] at w=6). Its 8 actors cap
concurrency at 8 under the single-method invariant, so it cannot express a
6-core peak cleanly. **The SMT answer above rests on `saturation-32` and its
control; `saturation-8` is reported as not settling it.** Its protoClojure twin
has the same irregular shape, which again points away from protoScala.

---

## 5. What is still unsettled

1. **Whether protoCore's sparse-list reports are *certainly* benign.** §2.3
   gives four reasons for false positive and names the one experiment that would
   close it (annotate the freelist handoff and re-run). Not done; protoCore was
   not to be modified.
2. **`ActorScheduler.cpp`'s diagnosed race** (§2.4) did not reproduce here and is
   **not** cleared by that. It remains open, with a fix decided and unapplied.
3. **Why CPython is more load-sensitive than protoScala** (§3.2). The direction
   is measured and the ratios are published; the mechanism is a hypothesis.
4. **`saturation-8`'s shape** (§4.3) — irregular in both runtimes, not
   explained here.
5. **The `--version` prelude-image misreport** (§1.3) is diagnosed but not
   fixed, and `build_bench` (§3.3) is not rebuilt; both live in a tree another
   agent holds.
6. **Absolute milliseconds remain indicative.** Ratios between interleaved
   columns are the primary result. Where a figure will move again, the
   reproducing command is given with an "as of 2026-09-26".
