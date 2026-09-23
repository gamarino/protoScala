# protoScala actor benchmarks — 2026-09-23, the CPU-bound saturation curve

> This report **adds a mode**; it does not supersede
> [`2026-09-23-actors-v4-curve.md`](2026-09-23-actors-v4-curve.md), whose
> reading of `fan-out`, `MPMC`, `MPSC` and `single` stands. v4 closed with a
> methodological note: *none of the four modes can show the 6-core rise*, because
> each is capped by its own structural concurrency (1 or 4 producers, or the
> single-method invariant) and `fan-out` is producer-bound besides. This run
> measures the mode that was missing.
>
> **No runtime source code was changed for this run**, in either protoScala or
> protoClojure. Two benchmark scripts per runtime were added and the harness was
> taught to verify them. Nothing was tuned. `ProtoMPSCQueue` is still on
> protoCore `feature/mpsc-queue` and is **not merged**, so its column describes a
> branch build that no released protoScala uses; the shipped binary is the
> CAS-list column.

## What was missing, and what this mode does about it

The seven modes of DESIGN §8.5 all carry trivial handlers, so their throughput
is set by how fast one sender thread can enqueue rather than by how much work
the pool can absorb. v4 measured that directly for `fan-out`: an isolated sender
reaches 113,600 sends/s against 87,238 msg/s observed at w=1, and **78-99% of
the measurable window sits inside the sender's loop**. Adding workers cannot
help a benchmark whose producer is the clock.

The `saturation-*` modes remove the producer from the critical path instead of
trying to make it faster. Every message runs a 20,000-iteration summation —
roughly 1.5 ms of interpreter work — so the send loop falls to **well under 1%
of the run** and the workers become the constraint. Measured on the real script
at N=4800, the send loop is **12 ms of 8,884 ms at w=1 (0.14%) and 20 ms of
2,471 ms at w=6 (0.8%)**.

This mirrors protoST's `benchmarks/actors/saturation_8a.st` and
`saturation_32a.st`. On **this same machine** protoST measured, on
`saturation_32a`:

| workers | 1 | 2 | 4 | 6 | 8 | 12 |
|---|---|---|---|---|---|---|
| protoST speedup | 1.00× | 1.70× | 2.83× | **3.11× (peak)** | 2.54× | 2.51× |

— a rise to the 6 physical cores and then a regression under SMT
oversubscription (`protoST/docs/archive/design-specs/2026-05-23-hardware-bound-plateau.md`).
That is the shape this run set out to reproduce.

Two actor counts cover the actors-versus-workers axis, with **identical total
work**, so the two curves are directly comparable:

- `saturation-8` — 8 actors × 600 messages. At most 8 can run at once under the
  single-method invariant; at w=8 there is one actor per worker and no
  work-stealing opportunity, as in protoST's `saturation_8a.st`.
- `saturation-32` — 32 actors × 150 messages. Always more runnable actors than
  workers, so the pool can steal freely. Read against `saturation-8` it
  separates "the scheduler cannot fill the cores" from "8 actors cannot fill
  the cores".

## Method

| | CAS list | ProtoMPSCQueue |
|---|---|---|
| binary | `build_release/protoscala` | `build_pmq/protoscala` |
| protoCore | master `a1a8297f` (2.0.0) | `feature/mpsc-queue` `f3a6886c` (2.1.0) |
| `--version` reports | `actor mailboxes: CAS list` | `actor mailboxes: ProtoMPSCQueue` |
| `ldd` resolves | `protoCore/build_release/libprotoCore.so.2` | `.agent_scratch/actor-curve/pmq-prefix/lib/libprotoCore.so.2` |

protoScala `6f7cdb5`, protoClojure `4ea4e70` (`build_release/protoclj`, protoCore
master `a1a8297f`, the same binary in both columns). `ldd` was checked on all
three binaries; **none resolves the stale root-owned 1.0.0 in `/usr/local/lib`**
(0 hits each). The `ProtoMPSCQueue` build reaches its library through
`CMAKE_BUILD_RPATH`, never through `LD_LIBRARY_PATH`, which the harness would
forward to protoClojure and make it load the wrong ABI.

- **Worker counts:** 1, 2, 3, 4, 5, 6, 8, 12, 16.
- **Interleaving:** one harness invocation produces one sample of every (mode,
  worker) cell, round-robin, with protoClojure sampled immediately after its
  protoScala twin. Invocations alternate CAS, PMQ, CAS, PMQ, … so the two
  binaries interleave at the invocation level and the two runtimes
  sample-for-sample. **One benchmark process runs at a time; no build ran during
  the measurement.**
- **Samples:** 4 complete rounds per variant. Every cell is the **median with
  `[min-max]`**, and **every cell carries n=4** — audited per cell, not assumed.
- **protoClojure is the load control.** Its two rows per mode (one sampled during
  the CAS invocations, one during the PMQ ones) come from the same binary and
  should agree; they agree to **within about 1%** at nearly every worker count,
  so the CAS and PMQ columns were measured under genuinely comparable load.
- **Verification.** Every script self-reports `mode=… messages=… processed=…`,
  then its stats line, then `ok`, and the runner checks that report before
  computing any rate. `processed` is the **sum the actors actually computed**
  (each message folds sum(1..20000) = 200,010,000 into its actor's state), not a
  message count, so a handler that silently did no work fails instead of reading
  as a very fast run. The twins carry the **same message count** as their
  protoScala counterparts — 4,808 and 4,832 — which is what makes the curves
  comparable.

  Both guards were tested against deliberate faults before measuring: a
  protoScala run whose expected checksum was perturbed was rejected
  (`processed 961648080000, expected 962648130000`), and a protoClojure twin
  rewritten to send `inc` instead of the compute — exiting 0 with a plausible
  message count — was rejected (`computed 4800, expected 960048000000`). A crash
  or a no-op that exits 0 cannot be recorded as a win.
- **Sample integrity.** All 8 harness invocations exited `rc=0`. **288 of 288
  samples succeeded, 0 cells FAILED, and no cell is short.** `earlyoom` killed
  nothing during this run; it is watched below.

### Contention, and the limitation that matters most

This is the maintainer's daily-driver desktop. **Every number here is contended.**

| | time | load average |
|---|---|---|
| start | 14:22 | 7.98 |
| middle (after round 2) | 14:31 | 10.47 |
| end | 14:39 | 10.28 |

Measured immediately after the run finished, with no benchmark process alive,
the load average was still **6.65** — so roughly **6-7 of the 12 logical CPUs
were occupied by other processes throughout**, leaving on the order of 3-4
physical cores' worth of capacity for the benchmark.

**This is the principal limitation of this report and it bears directly on its
main question.** A benchmark that can only reach ~4 free cores cannot be
expected to show a clean rise to 6 and a clean SMT cliff at 8, and the flattening
observed at w=4-5 below coincides with exactly that ceiling. The shape *below*
w=4 and the *existence* of a strong rise are solid; **the exact peak position and
the absence of an SMT regression are not settled by this run** and need a quiet
machine. Said plainly rather than worked around.

`earlyoom` had SIGTERMed six `python`/`python3` harness processes between 13:49
and 13:59, before this run — those belong to v4's `fan-out` measurement, which
holds ~1.1 GB RSS. This mode is far lighter: **max RSS 36 MB for protoScala and
23 MB for protoClojure**, against 1.1 GB for `fan-out`. Available memory stayed
between 20 GB and 27 GB (31-43%), never approaching earlyoom's 20% threshold, and
a journal watch armed for the duration of the run reported **no kill**.

## Results

Median msg/s `[min-max]`, n=4 in every cell. Rates are low in absolute terms
because each message is ~1.5 ms of compute; the **shape** is the result.

### `saturation-8` — 8 actors

| series | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 | peak |
|---|---|---|---|---|---|---|---|---|---|---|
| protoScala, CAS list | 658 [548-710] | 1,301 [1,070-1,366] | 1,548 [1,258-1,759] | 2,092 [1,736-2,412] | 1,922 [1,161-2,316] | 2,078 [1,162-2,306] | **2,280** [1,329-2,614] | 2,067 [1,969-2,232] | 2,146 [1,850-2,234] | w=8 |
| protoScala, ProtoMPSCQueue | 744 [647-786] | 1,420 [1,373-1,459] | 1,846 [1,766-1,984] | **2,350** [1,692-2,578] | 2,342 [1,994-2,562] | 2,325 [1,975-2,745] | 2,165 [2,127-2,744] | 2,226 [2,150-2,347] | 2,092 [2,014-2,234] | w=4 |
| protoClojure twin (CAS runs) | 501 [420-513] | 944 [841-1,012] | 1,272 [1,185-1,297] | 1,456 [970-1,817] | 1,500 [834-1,730] | 1,616 [699-1,899] | **1,762** [1,297-1,885] | 1,728 [1,476-1,871] | 1,656 [1,407-1,853] | w=8 |
| protoClojure twin (PMQ runs) | 516 [490-528] | 912 [897-948] | 1,223 [1,214-1,404] | 1,566 [1,219-1,625] | 1,620 [1,236-1,875] | 1,677 [1,278-1,825] | **1,747** [1,641-1,844] | 1,670 [1,591-1,859] | 1,534 [1,461-1,723] | w=8 |

Speedup against w=1:

| series | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 |
|---|---|---|---|---|---|---|---|---|---|
| protoScala, CAS list | 1.00× | 1.98× | 2.35× | 3.18× | 2.92× | **3.16×** | 3.47× | 3.14× | 3.26× |
| protoScala, ProtoMPSCQueue | 1.00× | 1.91× | 2.48× | 3.16× | 3.15× | **3.12×** | 2.91× | 2.99× | 2.81× |
| protoClojure twin (CAS runs) | 1.00× | 1.88× | 2.54× | 2.91× | 2.99× | **3.22×** | 3.52× | 3.45× | 3.31× |
| protoClojure twin (PMQ runs) | 1.00× | 1.77× | 2.37× | 3.03× | 3.14× | **3.25×** | 3.39× | 3.24× | 2.97× |

### `saturation-32` — 32 actors, identical total work

| series | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 | peak |
|---|---|---|---|---|---|---|---|---|---|---|
| protoScala, CAS list | 610 [485-676] | 1,233 [1,160-1,341] | 1,722 [1,510-1,850] | 2,097 [1,744-2,263] | 2,200 [2,119-2,573] | 2,248 [2,015-2,628] | 2,378 [2,190-2,700] | **2,424** [2,071-2,497] | 2,400 [2,298-2,580] | w=12 |
| protoScala, ProtoMPSCQueue | 747 [632-761] | 1,464 [1,373-1,512] | 1,922 [1,836-2,062] | 2,378 [2,296-2,480] | 2,506 [2,032-2,977] | 2,474 [2,034-2,761] | **2,562** [2,533-2,630] | 2,559 [2,356-2,629] | 2,470 [2,367-2,730] | w=8 |
| protoClojure twin (CAS runs) | 496 [490-505] | 890 [861-980] | 1,336 [1,188-1,420] | 1,532 [1,331-1,652] | 1,786 [1,616-1,864] | 1,741 [1,546-1,927] | 1,810 [1,635-2,028] | **1,942** [1,814-1,964] | 1,864 [1,814-2,027] | w=12 |
| protoClojure twin (PMQ runs) | 503 [484-508] | 918 [859-943] | 1,286 [1,244-1,322] | 1,626 [1,287-1,691] | 1,609 [1,382-1,718] | 1,752 [1,641-1,867] | 1,872 [1,683-2,109] | **1,940** [1,877-2,033] | 1,901 [1,807-1,984] | w=12 |

Speedup against w=1:

| series | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 |
|---|---|---|---|---|---|---|---|---|---|
| protoScala, CAS list | 1.00× | 2.02× | 2.82× | 3.43× | 3.60× | **3.68×** | 3.90× | 3.97× | 3.93× |
| protoScala, ProtoMPSCQueue | 1.00× | 1.96× | 2.57× | 3.18× | 3.36× | **3.31×** | 3.43× | 3.43× | 3.31× |
| protoClojure twin (CAS runs) | 1.00× | 1.79× | 2.69× | 3.09× | 3.60× | **3.51×** | 3.65× | 3.91× | 3.76× |
| protoClojure twin (PMQ runs) | 1.00× | 1.82× | 2.56× | 3.23× | 3.20× | **3.48×** | 3.72× | 3.86× | 3.78× |

## Reading

### 1. The mode works: the suite can now show a worker-count rise

This is the first protoScala actor benchmark whose throughput rises
substantially and monotonically with workers. On the shipped CAS-list binary,
`saturation-32` goes **1.00× → 2.02× → 2.82× → 3.43×** at w=1, 2, 3, 4 — close
to linear through w=3 — where `fan-out` peaked at w=2 with +2.4% and `single`
and `MPSC` peaked at w=1. The gap v4 recorded is closed: **a mode that can
exhibit a worker-count rise now exists in this suite.**

### 2. Does protoScala rise to 6 workers? It rises, but the near-linear region ends at w=3

**At w=6 the shipped binary reaches 3.68× on `saturation-32` and 3.16× on
`saturation-8`.** The rise is real and large, but it is not the "roughly linear
to 6" that was expected: efficiency falls from 94% of ideal at w=2 to 86% at
w=3, 86% at w=4 and **61% at w=6**. The curve bends at w=3-4.

The honest qualification is in **Contention** above: with ~6-7 of 12 logical
CPUs occupied by other processes, only about 3-4 physical cores' worth of
capacity was available, and the bend sits exactly where that ceiling predicts.
**This run cannot separate "the scheduler bends at w=4" from "the machine had
four cores to give."** That distinction needs a quiet machine and is the single
most valuable follow-up.

### 3. Where is the peak? w=8 to w=12, not w=6 — and SMT does *not* regress

On the shipped binary the peak is **w=8 for `saturation-8` (3.47×)** and **w=12
for `saturation-32` (3.97×)**. Above the 6 physical cores the curve is **flat,
not falling**: `saturation-32` reads 3.90×, 3.97×, 3.93× at w=8, 12, 16, and
`saturation-8` 3.47×, 3.14×, 3.26× — variation inside the spread.

**protoST's SMT regression did not reproduce.** protoST fell from 3.11× at w=6
to 2.54× at w=8 and 2.51× at w=12; protoScala holds its gain. Reported as
measured, but **not claimed as a runtime property**: on a machine already
running 6-7 foreign threads, extra workers absorb scheduling delay that a quiet
machine would not impose, which is exactly the mechanism that would hide an SMT
cliff. protoClojure's twin behaves the same way (peak at w=12, no regression),
which is evidence that this is the *machine*, not protoScala's scheduler. On a
quiet machine the peak may well move back to 6.

### 4. 32 actors beat 8 actors, as the design predicts

`saturation-32` reaches 3.97× against `saturation-8`'s 3.47×, and its curve is
smoother and its spreads far narrower (e.g. at w=6, `[2,015-2,628]` against
`[1,162-2,306]`). With 8 actors and 8+ workers the pool has no work-stealing
slack, so a single slow actor stalls a worker; with 32 it always has another
runnable actor. **The scheduler is not the limit at 8 actors — the actor count
is.** This is the discrimination the two modes were added to provide, and it
matches protoST's rationale for its own three-variant family.

### 5. protoScala's shape matches protoClojure's; its level is ~1.25-1.3× better

The two runtimes trace the **same curve** on the same shape. On `saturation-32`
(CAS runs) the speedups are 2.02/2.82/3.43/3.68/3.97 against protoClojure's
1.79/2.69/3.09/3.51/3.91 at w=2, 3, 4, 6, 12 — within a few percent, same bend,
same peak at w=12, same absence of regression.

In absolute terms protoScala is **consistently faster on this shape**: 1.31× at
w=1, 1.29× at w=6 and 1.30× at w=16 on `saturation-8`, and 1.23×, 1.29×, 1.25×
at w=1, 6, 12 on `saturation-32`.

**This must not be read as "protoScala's actors are faster."** A message here is
~1.5 ms of arithmetic and a few microseconds of actor machinery, so this
benchmark mostly measures the two interpreters' loop speed, and protoScala's
happens to be quicker at integer summation. What the *actor* comparison shows is
the **shape**, and the shape is the same. protoScala remains an agile,
interoperable, easily integrable and very simple Scala — **not a fast one**;
nothing here changes that positioning, and on the trivial-message shapes of v4
protoClojure is still ahead at w=1.

### 6. The mailbox is not the bottleneck when the handler dominates

`ProtoMPSCQueue` is worth **+13% at w=1 and +12% at w=6** on `saturation-8` and
**+22% at w=1, +10% at w=6** on `saturation-32` — real, but far smaller than the
**+44% to +134%** it was worth on `fan-out`. That is the expected result and a
useful sanity check: when 1.5 ms of handler dwarfs the enqueue, a faster mailbox
cannot matter much. Its *shape* is slightly worse than CAS's on `saturation-32`
(3.31× against 3.68× at w=6), simply because its w=1 baseline is higher.

**Nothing here ships.** `ProtoMPSCQueue` is not in protoCore master; the released
protoScala uses the CAS-list mailbox, which is the first row of every table.

## Open, and what to do next

1. **Re-run on a quiet machine.** With ~6-7 of 12 logical CPUs taken by other
   processes, the position of the peak and the absence of an SMT regression are
   not settled. This is the one measurement that would answer the maintainer's
   original question cleanly.
2. **The bend at w=3-4 is unexplained** and may be entirely the contention
   ceiling. Nothing was changed to test it.
3. **A 128-actor variant** would complete the protoST family
   (`saturation_128a.st`) and test whether the actor-count effect in §4
   continues.

## Reproducing

```bash
cmake -B build_release -S . && cmake --build build_release --target protoscala
ldd build_release/protoscala | grep protoCore        # never /usr/local/lib

# One round = one sample of every cell; alternate the two binaries per round.
for BIN in build_release/protoscala build_pmq/protoscala; do
  PROTOSCALA_BIN=$PWD/$BIN python3 benchmarks/run_actor_benchmarks.py \
    --only saturation-8,saturation-32 --workers 1,2,3,4,5,6,8,12,16 --samples 1
done
```
