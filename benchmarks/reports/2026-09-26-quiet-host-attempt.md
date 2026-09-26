# The "quiet host" measurements — 2026-09-26

> **This report supersedes nothing.** It adds two results and it *refuses* two.
> It does not change a single published benchmark figure, because the condition
> those figures were waiting for — a quiet host — **did not hold during this
> window**, and the measurement that proves that is in §1.
>
> Earlier reports stand as written:
> [`2026-09-24-suite.md`](2026-09-24-suite.md),
> [`2026-09-24-phase4-actors.md`](2026-09-24-phase4-actors.md),
> [`2026-09-23-actors-v5-saturation.md`](2026-09-23-actors-v5-saturation.md).

Four questions were put to this window, all four of them blocked until now on
machine load: the cold-start budget, a ThreadSanitizer run against the actors,
a re-measurement of the published benchmarks, and the saturation curve's two
open questions.

**Two are settled here. Two are not, and they are reported as not settled
rather than answered at the precision the host could not support.**

| # | Question | Outcome |
|---|---|---|
| 1 | Cold-start budget, `< 25 ms`, 4 binary/mode combinations | **MISSED** — refuted, not certified |
| 2 | ThreadSanitizer against the actors | **Run for the first time. Dirty.** One race is protoScala's own |
| 3 | Re-measure the published benchmark tables | **Not done.** Preconditions absent (§1) |
| 4 | Saturation curve: does the bend move to `w=6`? does SMT regress? | **Not settled.** Preconditions absent (§1) |

---

## 1. The host was not quiet, and this is the measurement that says so

This matters more than any other number here, because questions 3 and 4 are
*defined* by it, so it is reported first and with its operands.

`uptime`'s load average was the metric earlier reports gated on. It is the wrong
metric to gate on alone — it counts runnable *and* uninterruptible tasks — so
both are recorded below: load average beside `mpstat`'s idle percentage, which
converts directly into busy logical CPUs on this 12-CPU host.

| when | what was running | load avg (1 m) | CPU busy (of 12) |
|---|---|---|---|
| 08:38 | nothing of this session's | 1.68 | **2.31** |
| 08:38 | nothing of this session's | 2.25 | 2.30 |
| 08:43 | nothing of this session's | 4.86 | 2.03 |
| 08:52 | nothing of this session's | 4.35 → 4.79 | **3.26 – 4.75** |
| 09:0x | cold-start rounds (§2) | 4.03 – 4.22 | not sampled |

Busy-CPU figures are `12 × (100 − %idle) / 100` from `mpstat 5 1`; three
consecutive samples at 08:52 gave 4.75, 4.02 and 3.26.

**The foreign load never went below about 2.3 of 12 logical CPUs, and by 08:52
it was rising through 4.7.** It was spread evenly rather than confined: a
`mpstat -P ALL 5 2` at 08:38 put **every one of the 12 CPUs between 14 % and
32 % busy**, with a mean idle of 80.76 %. No core was free to be borrowed.

The named contributors, from `ps`/`top`, are the maintainer's desktop: a VS Code
process averaging **83 % of one core sustained over 3.4 days**, PyCharm
(9.1 % of RAM), and roughly forty Chrome processes; `gnome-shell` and
`Xwayland` above them. Nothing of this session's own was running during the
08:38 and 08:43 samples.

Two further facts about the host, both recorded because they bear on whether a
heavy benchmark run was advisable at all:

- **Swap was full**: 2,046.7 MiB of 2,048.0 MiB used, 1.3 MiB free, against
  39.7 GiB of 63.7 GiB RAM in use. `earlyoom` is active and has SIGTERMed
  harness processes on this host before.
- Context-switch and interrupt rates sat near **14,000/s each** with nothing of
  this session's running (`vmstat 1 10`).

### What that permits, and what it forbids

Load of ~2.3–4.7 busy CPUs is **better than every prior attempt on record**
(the v5 saturation report measured 6.65 with no benchmark running; the
cold-start attempts sat at 2.97, 4, 5.6, 8.7 and 11.2). It is not a quiet host.

- It is **good enough** for a cold-start verdict, because the verdict that came
  out is a *refutation*: contention can only push a start-up median up, so a
  median above target on a loaded host still refutes the target. §2 says so
  with the load recorded beside every round.
- It is **good enough** for ThreadSanitizer, which is a correctness tool. A
  race it reports is a race whatever the load; §3 is therefore the one result
  here that the host cannot have distorted.
- It is **not good enough** to republish the README's tables (§4), and **not
  good enough** to settle the saturation curve's peak position or the SMT
  question (§5), because both of those are claims about behaviour *at 8 and 12
  workers on 12 CPUs* — exactly the region that 2.3–4.7 busy foreign CPUs
  destroys. Publishing them would have repeated the error this project has just
  finished correcting in 32 figures.

---

## 2. Cold start: MISSED, and the budget is refuted rather than certified

**Verdict: MISSED, for all four binary/mode combinations, at every load
sampled.** `benchmarks/cold-start.sh` exited **1 in 12 of 12 cases**.

The budget's own done-when is written in `docs/DECISIONS-LOG.md`:
`benchmarks/cold-start.sh <binary> 21` **must exit 0**. That is the rule applied
here, harness overhead included, rather than a re-defined one (§2.3 quantifies
that overhead, and deliberately does not deduct it).

### 2.1 The quietest sample of the window

Taken at load 1.84 — the lowest the host reached — on the shipped Release
binary, 21 runs per mode, **21/21 verified in both**:

| mode | median | min | max | target | verdict |
|---|---|---|---|---|---|
| script | **26.38 ms** | 22.50 | 30.06 | 25 | MISSED |
| repl | **25.43 ms** | 23.20 | 29.57 | 25 | MISSED (straddles) |

The REPL case is the near one: its median misses by 0.43 ms and its spread
crosses the target at both ends. `script` misses by 1.38 ms.

### 2.2 Three interleaved rounds, both builds

`RelWithDebInfo` did not exist in this tree and was built for this measurement
(`build_rwdi`, `-DCMAKE_BUILD_TYPE=RelWithDebInfo`, 65.7 s). Both binaries were
confirmed by `ldd` against the workspace `libprotoCore.so.3` → `2.5.0`, never
`/usr/local`'s 1.0.0. 21 runs per cell, **all 252 runs verified**, load
4.03–4.22 throughout:

| build | mode | r1 | r2 | r3 | median of medians |
|---|---|---|---|---|---|
| Release | script | 28.67 | 26.93 | 28.03 | **28.03** |
| Release | repl | 29.50 | 27.07 | 26.89 | **27.07** |
| RelWithDebInfo | script | 28.61 | 27.51 | 28.37 | **28.37** |
| RelWithDebInfo | repl | 27.97 | 27.95 | 27.98 | **27.97** |

Per-round min/max spreads ran from 23.77 to 43.88 ms; the widest single cell was
Release/script round 1 (25.45–43.88).

**Release and RelWithDebInfo are indistinguishable.** RelWithDebInfo is
0.34–0.90 ms higher on the median-of-medians, against a within-cell spread of
3–18 ms. This measurement does not support a claim that either build is faster.

### 2.3 Two operands the verdict should be read with

- **The harness's own floor is ~5 ms.** `cold-start.sh` times a pipeline — two
  `date +%s%N` forks, the binary, a pipe and an `awk` — and for the REPL case an
  extra `sh -c` and `printf`. Timing that same construct around trivial
  commands, 21 runs each, gives medians of **5.55 ms (`/bin/true`), 4.79 ms
  (`/bin/echo hi`) and 4.67 ms (`sh -c true`)**. So of the 26.38 ms script
  median, roughly **21.4 ms is protoScala** and ~5 ms is the harness.
  This is recorded as an operand, **not deducted**: the budget's done-when is
  the script's exit status, and changing that is a maintainer's decision, not a
  measurement's.
- **Start-up here is kernel-bound, not interpreter-bound.** Across 42 runs the
  harness used 0.26 s of user time against **1.07 s of system time** — about
  25 ms of `sys` per run — consistent with process creation, `mmap` and dynamic
  linking rather than with prelude work.

### 2.4 What did not reproduce

The recorded Phase 6 figure is **23.73 ms (script) / 23.89 ms (REPL)** at load
2.97. This window measured **26.38 / 25.43 at load 1.84** — worse by 2.65 and
1.54 ms at a *lower* load, on a binary confirmed to be using the precompiled
prelude image (`--version` reports it; `build_release/generated/PreludeImage.cpp`
is present).

**Load is therefore not the variable that explains the gap, and this report does
not explain it.** Stated as unsettled. Candidates not separated here: a change
in the tree between that measurement and `2c120d5`, a difference in how that
figure was taken, or a host-state variable neither run recorded. Anyone
re-opening this should start by rebuilding the commit the 23.73 ms came from and
interleaving it against `2c120d5` in one window, which is the method
`DECISIONS-LOG.md` already prescribes for this question.

### Reproducing

```bash
cmake -B build_rwdi -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build_rwdi --target protoscala -j4
ldd build_rwdi/protoscala | grep protoCore       # never /usr/local/lib
benchmarks/cold-start.sh ./build_release/protoscala 21   # as of 2026-09-26: exit 1
benchmarks/cold-start.sh ./build_rwdi/protoscala   21   # as of 2026-09-26: exit 1
```

---

## 3. ThreadSanitizer against the actors: run for the first time, and dirty

This had never been run. `protoCore/docs/FIELD-NOTES.md` recorded the reason as
mechanical rather than a matter of will, and it was right on both counts.

### 3.1 The build option had to be added, and a second obstacle appeared

protoScala had **no sanitizer option at all**. One was added —
`-DPROTOSCALA_SANITIZER=thread|address|undefined` in `CMakeLists.txt` — and it
refuses an unknown value, warns that protoCore must carry the same sanitizer,
and takes `-DPROTOCORE_LIBRARY=` so the instrumented core can be pointed at.

protoCore was built with TSan **at 2.5.0** for this run
(`-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"`,
26.7 s) into a scratch directory, leaving protoCore's own tree untouched. The
existing `protoCore/build_*_tsan` trees are 2.4.0 and were not used.
`ldd` confirms the TSan binary loads that library and `libtsan.so.2`.

`setarch -R` is required on this kernel, as FIELD-NOTES says. **A second site
needed it that FIELD-NOTES did not list:** the build itself. Phase 6's
precompiled prelude runs `protoscala-precompile` *at build time*, and once that
tool is instrumented it aborts with `FATAL: ThreadSanitizer: unexpected memory
mapping`, failing the build at 45 %. Wrapping the whole build — `setarch -R
cmake --build …` — fixes it, because the personality is inherited by children.
That recipe is in the new option's comment.

### 3.2 Results by site

A single-threaded `hello.scala` is **clean**. Everything below is a real
multi-threaded run, `halt_on_error=0`, so the counts are distinct reports.

| workload | TSan reports |
|---|---|
| `hello.scala` (single-threaded) | **0** |
| `13-actors/ask-with-no-reply` | 7 |
| `13-actors/await-one-worker` | 15 |
| `13-actors/many-actors` | 24 |
| `13-actors/spawn-and-send-indent` | 55 |
| `13-actors/stats` | 78 |
| `13-actors/priority-bands` | 88 |
| 8 threads × 25,000 sends into one actor | **989** |
| 8 plain `Thread.start`, **no actor** | 15 |

**The 8×25,000 stress run printed `200000` — exactly right.** No message was
lost, duplicated or concurrently handled; the single-method invariant held. It
exited 66, which is TSan's race exit code, not a wrong answer. Under TSan it
took 237 s.

### 3.3 protoCore's races versus protoScala's own

**Separated by a control**: the same binary running 8 plain threads that touch
no actor still reports 15 races, 13 of them at protoCore sites. So the
protoCore sites are **not actor-specific** and are reached by any multi-threaded
protoScala program.

**protoCore (pre-existing, not protoScala's to fix).** Every one of the 989
stress reports and every report in the six actor cases tops out in protoCore.
Aggregated by the site TSan names:

| site | share of stress reports |
|---|---|
| `core/SparseListAlgorithms.h` `getAt` (lines 94–98) | 762 |
| `core/SparseListAlgorithms.h` `setAt` (118–126) | 143 |
| `core/SparseListAlgorithms.h` `removeAt` (139–155) | 39 |
| `core/SparseListAlgorithms.h` `nodeSize` (33) | 31 |
| `core/ProtoObject.cpp` `getOwnAttributeDirect` (1793) | 8 |
| remainder (`ProtoString`, `ProtoSpace::submitYoungGeneration`, `setAttributeIfEqual`, `Thread.cpp`) | the rest |

The mechanism, read off a full report: a worker thread in
`ActorScheduler::nextMessage` (`ActorScheduler.cpp:235`) calls
`setAttribute` on the actor's **mutable** `ProtoObject`, which reaches
`ProtoSparseList::setAt` and **constructs a sparse-list node**
(`ProtoSparseList.cpp:75`); concurrently another thread reads that same node
through `getAttribute` → `resolveMutableSnapshot` → `resolveMutableState` →
`implGetAt`. The two accesses are 4 bytes apart in one cell of a 16 MiB arena
from `ProtoSpace::getFreeCells`.

protoCore's documented model
(`docs/MUTABLE_SHARDING_AND_CACHE_REFACTOR.md`) makes `mutableRoot[16]` an
`std::atomic` updated by `compare_exchange_weak`, which is a **persistent**
structure's contract: a writer builds new nodes and publishes them by CAS, so a
reader holding an older root keeps reading intact nodes. TSan reporting races on
the *nodes* is evidence that this contract is not holding under this workload.
**This report does not claim to have identified which of the two possible causes
it is** — a node still reachable from a reader's snapshot being written in
place, or a missing release/acquire edge between node initialisation and the CAS
publish — and it does not rule out a TSan false positive arising from
synchronisation TSan cannot see. FIELD-NOTES records a pre-existing
"40–116-warning baseline accepted as benign" in protoCore, which this run
neither confirms nor overturns. **Reported to protoCore, diagnosed no further
here**, because protoScala is calling a documented, advertised-as-lock-free API.

**protoScala's own — one site, and it is a real bug.**
Exactly one race names a protoScala file as the race site, and it is *not* in
the actor scheduler. It is in `Thread.start` (D49):

```
src/runtime/ActorPrimitives.cpp:29    ActiveCallContext g_threadBlueprint{};   // plain global
src/runtime/ActorPrimitives.cpp:431   g_threadBlueprint = *activeCallContext();  // written by the SPAWNING thread
src/runtime/ActorPrimitives.cpp:407   ExecutionEngine::ActiveCallGuard guard(g_threadBlueprint.engine,
                                                                            g_threadBlueprint.layout);
                                      //                        ^ read by the SPAWNED thread
```

TSan: *"Read of size 8 … by thread T3 / Previous write of size 8 … by thread
T1"*, `threadEntry` against `threadObject_start`.

`ActiveCallContext` is two raw pointers; `g_threadBlueprint` is a plain
non-atomic, mutex-free global with **no happens-before edge** to the thread that
reads it. It is also a **single** global shared by every `Thread.start`, so it
is overwritten by the next spawn whether or not the previous child has read it.

Severity: the race is real; the consequence is currently latent. Every
`Thread.start` in the tests installs the *same* engine and layout, so the value
read is correct by coincidence, and an aligned 8-byte load on x86-64 will not
tear. It becomes a wrong-engine bug as soon as two spawns with different
`ActiveCallContext` values overlap. **A handoff channel for a thread's starting
context should not be a mutable global**; passing it through the thread argument
that already carries the handle would remove the race by construction rather
than by luck. Not changed here: it is a behaviour change in the concurrency
surface, it wants its own test, and this window's mandate was to measure.

### Reproducing

```bash
cmake -B ../.agent_scratch/core_tsan -S ../protoCore \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build ../.agent_scratch/core_tsan --target protoCore -j3

cmake -B build_tsan -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DPROTOSCALA_SANITIZER=thread -DPROTOSCALA_PROTOST_INTEROP=OFF \
      -DPROTOCORE_LIBRARY=$PWD/../.agent_scratch/core_tsan/libprotoCore.so
setarch -R cmake --build build_tsan --target protoscala -j3   # setarch -R is needed for the BUILD
ldd build_tsan/protoscala | grep -E 'protoCore|tsan'

TSAN_OPTIONS=halt_on_error=0 setarch -R ./build_tsan/protoscala \
    tests/conformance/13-actors/priority-bands.scala
```

`PROTOSCALA_PROTOST_INTEROP=OFF` is deliberate: protoST's static libraries are
not instrumented, and mixing them in would produce both false negatives and
false positives.

---

## 4. The published benchmark tables were not re-measured

Not done, and deliberately not attempted at the precision that republishing
would imply. The general suite's published figures were taken at load
3.11 → 4.21 and the actor table at 3.59 → 9.09; this window offered
2.3 → 4.7 busy CPUs and rising (§1). That is an improvement of a factor of
roughly two on the actor table's conditions and **no improvement at all** on the
general suite's.

Republishing a table measured under materially the same contention as the table
it replaces would add a date and no confidence. **The README's figures therefore
stand unchanged, with the contention caveats they already carry.**

What a future window needs, stated so it can be checked before the run rather
than regretted after it: `mpstat 5 1` showing **idle ≥ 92 %** (≤ 1 busy CPU of
12) sustained across three samples, with the desktop's editors and browser
closed. Neither the load average alone nor a subjective "nothing is running" is
sufficient — §1 is what that looks like when it is wrong.

---

## 5. The saturation curve's two open questions remain open

v5 left two things open and named the reason: 6–7 of 12 logical CPUs carrying
foreign load. Those two questions are:

1. does the near-linear region end at `w=6` rather than `w=3`?
2. does protoST's documented SMT regression above six workers appear?

**Both are still open.** Not measured in this window, and the reason is the same
one v5 gave, at half the magnitude: both questions are about the curve **at
`w=8` and `w=12` on a 12-CPU host**, and 2.3–4.7 busy foreign CPUs — spread
across all 12, so not avoidable by pinning — leaves the top of the curve as
confounded as v5 found it. A bend measured against 7.3–9.7 available CPUs
cannot distinguish "protoScala's scheduler bends here" from "the host ran out of
cores here", which is precisely v5's §2 caveat restated.

v5's reading is unchanged: the rise below `w=4` and the *existence* of a strong
rise are solid; the peak position and the absence of an SMT regression are not
settled. **This report adds no new evidence for or against either.**

The measurement remains worth taking, with protoClojure's twin interleaved as
the load control exactly as v5 did, on a host that clears the §4 gate.

---

## What this window changed in the tree

- `CMakeLists.txt` — added `PROTOSCALA_SANITIZER` (thread/address/undefined),
  the first sanitizer option this project has had.
- this report.

No benchmark figure, no README table and no runtime source was changed. The
protoScala race in §3.3 is **reported, not fixed**.
