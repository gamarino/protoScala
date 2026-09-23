# protoScala actor benchmarks — 2026-09-23, CAS list against `ProtoMPSCQueue`

> ## ⚠ Partly superseded by [`2026-09-23-actors-v4-curve.md`](2026-09-23-actors-v4-curve.md)
>
> **The measurements below stand; two conclusions drawn from them do not.**
> Nothing in this file has been rewritten. What v4 overturns, precisely:
>
> 1. **This report measured only w=1 and w=16.** Two points cannot tell "does
>    not scale" apart from "scales, then pays oversubscription". v4 measured
>    1, 2, 3, 4, 5, 6, 8, 12 and 16 on the same two binaries and found a real
>    but **one-worker-wide** scaling region with the queue (+16.6% from w=1 to
>    the w=2 peak), breaking at w=3 — well inside the 6 physical cores.
> 2. **The `MPSC` "−11% with the queue" recorded here does not reproduce: it
>    was noise.** It came from three samples at two worker counts with
>    overlapping bands. Across the full curve the queue is a **wash at w=1**
>    (145,636 against 147,022, −0.9%) and **better at every higher worker
>    count**: +28% at w=2, +23% at w=3, +19% at w=6, +20% at w=8, +7% at w=16.
>    The sign anomaly is closed; the queue is neutral at w=1 and a modest win
>    from w=2 up.
> 3. **The comparison against protoClojure that motivates this report is not
>    like-for-like.** The two `fan-out` scripts measure different work —
>    protoScala rotates its target on every send, protoClojure sends 1000
>    consecutive messages per actor. Given protoScala's rotating shape,
>    protoClojure collapses too, and harder: 143,900 → 97,100 → 78,400 →
>    64,100 msg/s at w=1, 4, 6, 16 (**−55%**, against protoScala-with-queue's
>    −32%). So "protoScala loses throughput as workers are added while
>    protoClojure gains" is a property of the two scripts, not of the two
>    schedulers, and the ranking reverses from w=2 up once the shape is equal.
> 4. **The remaining regression is not the scheduler at large.** `MPMC` rises
>    **+113%** to its w=4 peak on the same scheduler and the same binaries.
>    `fan-out` is **producer-bound in its send path**: an isolated sender
>    reaches 113,600 sends/s against 87,238 msg/s observed at w=1, and the send
>    loop slows from 8.80 s to 18.44 s as workers go 1 → 16.
> 5. **Known gap:** no mode in this suite can exhibit a rise up to the 6
>    physical cores; every mode is capped by 1 or 4 producers or by the
>    single-method invariant.
>
> `ProtoMPSCQueue` is still **not merged**, so every queue number here and in
> v4 describes a branch build that no released protoScala uses.

> **This does not supersede [`2026-09-23-actors-v2.md`](2026-09-23-actors-v2.md).**
> That report describes the *shipped* configuration and still does.
> `ProtoMPSCQueue` (protoCore `feature/mpsc-queue`) **has not been merged**:
> re-validating it against the new protoCore master turned up two failing
> protoCore tests and a stop-the-world pause that is still linear in the
> batch, so the branch was held. This report exists to answer one question
> that v2 left open, and its `ProtoMPSCQueue` column is measured against a
> **branch build that no released protoScala uses**.

## The question

`2026-09-23-actors-v2.md` recorded that protoScala's `fan-out` throughput
*fell 46%* from 1 to 16 workers (104,384 → 56,475 msg/s) while
protoClojure's *rose 61%* (314,711 → 507,122), and named the CAS-list
fallback mailbox as a **plausible but explicitly unconfirmed** cause. This
run answers it by measuring the same binary twice, built only against two
different protoCore libraries.

**Answer: partly fixed.** The queue roughly doubles `fan-out` throughput at
16 workers and it takes a third off the scaling loss — but the loss does not
go away and the direction does not change. protoScala still loses throughput
as workers are added while protoClojure gains. The mailbox was a large part
of the cost; it was not the whole cause, and the remaining regression needs
its own investigation.

## Method

Two protoScala binaries from the same source tree (`646ff28`), differing only
in which protoCore they were compiled and linked against:

| | CAS list | ProtoMPSCQueue |
|---|---|---|
| binary | `build_release/protoscala` | `build_pmq/protoscala` |
| protoCore | master `a1a8297f` (2.0.0) | `feature/mpsc-queue` `f3a6886c` (2.1.0) |
| `--version` reports | `actor mailboxes: CAS list` | `actor mailboxes: ProtoMPSCQueue` |

The mailbox actually in use was confirmed three independent ways, not
assumed: the CMake status line (`protoScala: actor mailboxes on protoCore
ProtoMPSCQueue`), `protoscala --version`, and `nm -uC build_pmq/protoscala`,
which shows undefined references to `proto::ProtoMPSCQueue::push`,
`::takeAll`, `::isEmpty`, `::asObject`, `proto::ProtoContext::newMPSCQueue`
and `proto::ProtoObject::asMPSCQueue` — symbols that simply do not exist in
the other build.

Each binary resolves its own protoCore through `RUNPATH`, verified with
`ldd`. This matters: an earlier attempt used `LD_LIBRARY_PATH`, which the
harness passes to the child processes it spawns, so protoClojure — built
against 2.0.0 — loaded the 2.1.0 library and segfaulted on every sample
(`exit -11`). That is the stale-ABI hazard, caught by the harness's rule
that a failed cell is never turned into a rate.

`benchmarks/run_actor_benchmarks.py` interleaves round-robin: one protoScala
sample, then the matching protoClojure sample, then the next cell. Every
script self-reports the work it did and the runner verified that report
before computing any rate. protoClojure (`build_release/protoclj`, protoCore
master `a1a8297f`) is the load control in both columns — it is the same
binary either way, so a drift in its numbers between the two columns is
ambient load, not a change under test.

This is the maintainer's daily-driver desktop: VS Code, Chrome and PyCharm
run throughout. **These numbers are contended.** Load average ran 7.3 to 11.5
for the whole session and is recorded per run below. Absolute msg/s is
indicative; the ratio to protoClojure and the 1→16 worker *shape* are what
the question turns on, and both are measured under the same ambient load.

## `fan-out`: 9 samples per cell

Three runs per binary, alternating CAS, PMQ, CAS, PMQ, CAS, PMQ, three
samples per cell per run, pooled. Load average 8.1 to 10.2 across the six
runs.

| binary | w=1 median [min-max] | w=16 median [min-max] | 1 → 16 |
|---|---|---|---|
| CAS list | **116,712** [110,962-118,577] | **67,573** [65,756-71,693] | **-42.1%** |
| ProtoMPSCQueue | **188,699** [187,428-197,387] | **137,328** [129,175-139,921] | **-27.2%** |
| protoClojure (during CAS runs) | 345,321 [328,049-352,945] | 586,738 [556,548-593,422] | +69.9% |
| protoClojure (during PMQ runs) | 351,594 [341,495-352,936] | 583,607 [574,059-594,234] | +66.0% |

protoClojure's two rows agree within 2%, so the two columns were measured
under equivalent load and the protoScala difference is the mailbox.

| | w=1 | w=16 |
|---|---|---|
| ProtoMPSCQueue vs CAS list | **+61.7%** | **+103.2%** |
| ratio to protoClojure, CAS list | 0.34x | 0.12x |
| ratio to protoClojure, ProtoMPSCQueue | 0.54x | **0.24x** |

The CAS-list column reproduces v2's shape (-42% here against v2's -46%, at
higher absolute rates because the load window and the protoCore build differ),
which is what makes the comparison readable.

## All seven modes, w=1 and w=16, 3 samples per cell

Wider spreads than the table above — three samples on a loaded machine. Use
these for direction, not for precision. Load average 7.3 → 9.1 (CAS run) and
8.4 → 10.9 (PMQ run).

| mode | CAS w=1 | PMQ w=1 | CAS w=16 | PMQ w=16 |
|---|---|---|---|---|
| single | 161,385 [105,547-162,561] | 196,187 [169,643-197,698] | 125,043 [92,059-125,079] | 123,106 [112,651-125,786] |
| fan-out | 116,611 [73,749-120,855] | 158,092 [147,275-181,940] | 69,394 [48,222-73,425] | 107,353 [88,406-110,120] |
| MPSC | 196,268 [157,403-201,938] | 172,598 [148,857-181,246] | 131,258 [105,912-133,556] | 117,108 [105,907-121,238] |
| MPMC | 203,449 [166,983-209,685] | 180,247 [150,904-187,789] | 238,704 [219,199-243,322] | 280,143 [279,498-330,356] |
| ping-pong | 19,708 [19,580-20,602] | 19,776 [18,079-21,003] | 16,724 [16,439-18,632] | 17,194 [17,086-19,543] |
| await | 19,015 [12,848-21,621] | 20,369 [18,919-22,729] | 38,057 [20,143-38,810] | 47,464 [38,070-48,365] |
| priority | 115,027 [68,445-130,245] | 135,668 [104,915-153,666] | 87,604 [78,438-105,110] | 113,526 [101,333-124,259] |

`MPSC` reads *worse* with the queue in this table (-11% at both worker
counts). It is the one mode whose whole shape is four OS producers on one
mailbox, so it is the mode where the queue should help most, and the sign is
wrong. With three samples and min-max bands that overlap
(`[157,403-201,938]` against `[148,857-181,246]`) this is not a result; it is
a cell that needs its own 9-sample run before anything is said about it.
Recorded rather than dropped.

## Reading

1. **The hypothesis is partly confirmed, not confirmed.** The CAS-list
   mailbox cost `fan-out` about half its throughput at 16 workers — a real
   and large effect, and the largest single improvement measured in this
   session. But with the queue in place `fan-out` still falls 27% from 1 to
   16 workers while protoClojure rises 66% over the same range. Something
   else in protoScala's actor scheduling loses throughput as workers are
   added, and it is now the dominant term. The per-turn three-band backlog
   scan and ready-queue contention were the other candidates v2 named; both
   remain untested.

2. **The gap to protoClojure narrows but does not close.** At 16 workers
   protoScala goes from 0.12x to 0.24x of protoClojure. protoScala is an
   agile, interoperable, very simple Scala, not a fast one; this does not
   change that, and it is not meant to.

3. **Nothing here is shipped.** `ProtoMPSCQueue` is not in protoCore master.
   The released protoScala still uses the CAS-list mailbox and the numbers
   in `2026-09-23-actors-v2.md` remain the ones that describe it.

## Reproducing

```bash
# CAS-list column (the shipped configuration)
cmake -B build_release -S . && cmake --build build_release
PROTOSCALA_BIN=$PWD/build_release/protoscala \
  python3 benchmarks/run_actor_benchmarks.py --only fan-out --workers 1,16 --samples 3

# ProtoMPSCQueue column: protoScala must be built against a protoCore whose
# protoCore.h declares newMPSCQueue, and must carry its own RUNPATH so the
# library never leaks to protoClojure through the environment.
cmake -B build_pmq -S . -DPROTO_CORE_PREFIX=<prefix> -DCMAKE_BUILD_RPATH=<prefix>/lib
cmake --build build_pmq
./build_pmq/protoscala --version    # must report: actor mailboxes: ProtoMPSCQueue
ldd build_pmq/protoscala | grep protoCore
ldd ../protoClojure/build_release/protoclj | grep protoCore   # must differ
PROTOSCALA_BIN=$PWD/build_pmq/protoscala \
  python3 benchmarks/run_actor_benchmarks.py --only fan-out --workers 1,16 --samples 3
```
