# protoScala actor benchmarks — 2026-09-24

The seven modes of docs/DESIGN.md §8.5 plus the two CPU-bound `saturation-*`
modes. Every script self-reports the work
it did and this runner verified that report before computing any rate; a
cell that failed any check is printed as FAILED and never as a number.

This machine is a shared daily-driver desktop (VS Code, Chrome and PyCharm
run throughout; load-average floor ~2-3 on 12 logical CPUs). Rather than wait
for a quiet window, every cell was sampled 5 times, round-robin across
modes, worker counts and runtimes (one protoScala sample, then one protoClojure
sample at the same mode/workers when a twin exists, then the next cell), so
ambient load hits every column alike. Median msg/s is the headline number and
`[min-max]` is its spread; ratios to protoClojure are the primary comparison,
absolute msg/s is indicative only.

| | |
|---|---|
| machine | AMD Ryzen 5 5500U with Radeon Graphics |
| cores | 6 physical / 12 logical |
| date | 2026-09-24 02:51 |
| protoScala | 89ff66a |
| binary | `/home/gamarino/Documentos/proyectos/protoScala/build_release/protoscala` (RelWithDebInfo) |
| actor mailboxes | ProtoMPSCQueue |
| protoCore | 983bbf98 |
| protoClojure | a517280 |
| protoClojure binary | `/home/gamarino/Documentos/proyectos/protoClojure/build_release/protoclj` |
| samples per cell | 5, interleaved round-robin |
| load average at start | 7.99 / 7.73 / 7.36 |
| load average at midpoint | 8.48 / 7.51 / 7.52 |
| load average at end | 8.43 / 7.77 / 7.53 |

## Rates (messages per second, median [min-max] over 5 interleaved samples, verified)

| mode | w=1 | w=2 | w=4 | w=6 | w=8 | w=16 | peak (median) |
|---|---|---|---|---|---|---|---|
| single | 146,274 [133,989-187,738] | 163,386 [158,493-179,274] | 152,691 [123,580-161,190] | 133,771 [102,365-139,658] | 127,707 [108,125-132,232] | 116,432 [106,034-121,675] | 163,386 @ w=2 |
| fan-out | 164,331 [145,664-170,023] | 176,401 [169,425-185,013] | 165,400 [151,782-174,395] | 153,551 [146,815-164,255] | 141,814 [135,739-146,930] | 105,955 [103,334-108,216] | 176,401 @ w=2 |
| MPSC | 180,070 [160,549-184,084] | 174,840 [139,697-182,585] | 141,371 [101,856-166,826] | 144,065 [118,403-144,557] | 133,694 [125,453-134,601] | 119,125 [117,908-123,244] | 180,070 @ w=1 |
| MPMC | 181,536 [166,236-187,708] | 265,117 [246,147-285,765] | 383,875 [341,758-394,358] | 358,561 [301,688-362,874] | 329,481 [323,813-333,553] | 307,112 [303,013-322,294] | 383,875 @ w=4 |
| ping-pong | 20,372 [19,677-21,269] | 21,734 [20,655-24,369] | 20,233 [18,658-21,764] | 18,356 [12,005-18,547] | 17,107 [12,826-17,537] | 17,570 [17,233-18,075] | 21,734 @ w=2 |
| await | 20,953 [19,056-21,179] | 33,918 [33,683-36,979] | 51,747 [40,193-53,366] | 53,007 [47,173-56,024] | 50,901 [47,271-53,720] | 45,653 [41,510-46,096] | 53,007 @ w=6 |
| priority | 138,523 [126,842-146,445] | 125,393 [116,020-130,554] | 115,933 [112,218-129,644] | 119,463 [108,124-122,306] | 112,789 [107,552-117,649] | 107,903 [104,883-113,039] | 138,523 @ w=1 |
| saturation-8 | 756 [722-762] | 1,484 [1,292-1,542] | 2,439 [2,350-2,747] | 2,507 [2,414-2,561] | 2,425 [2,208-2,669] | 2,387 [2,086-2,492] | 2,507 @ w=6 |
| saturation-32 | 740 [569-793] | 1,369 [1,081-1,393] | 2,544 [2,252-2,699] | 2,538 [1,651-2,934] | 2,578 [1,957-2,832] | 2,344 [2,235-2,570] | 2,578 @ w=8 |

## What each mode measures

- **single** (`benchmarks/actors/actor-throughput.scala`, N=1000000): 1 sender thread x 1 actor: the per-actor pipeline floor. The single-method invariant serialises one actor, so more workers cannot help.
- **fan-out** (`benchmarks/actors/actor-fanout.scala`, N=1000000): 1 sender thread x 1000 actors: ready-queue stress.
- **MPSC** (`benchmarks/actors/actor-mpsc.scala`, N=1000000): 4 OS producer threads -> 1 actor: sender contention on one mailbox.
- **MPMC** (`benchmarks/actors/actor-mpmc.scala`, N=1000000): 4 OS producer threads x 4 actors, round-robin.
- **ping-pong** (`benchmarks/actors/actor-pingpong.scala`, N=100000): ask/reply latency through the cooperative suspension: every round trip is two messages, one frame snapshot and one resume.
- **await** (`benchmarks/actors/actor-await.scala`, N=100000): 100 caller actors awaiting one shared echo actor. Must complete at every worker count, including 1.
- **priority** (`benchmarks/actors/actor-priority.scala`, N=200000): a Low-band flood plus 1000 timed High-band asks; the p50/p99 below are the High-band ask latencies measured with System.nanoTime.
- **saturation-8** (`benchmarks/actors/actor-saturation-8.scala`, N=4800): 8 actors x N/8 CPU-bound messages (20,000-iteration summation each, ~1.5 ms) from one sender. The only modes that can exhibit a rise up to the physical core count: the send loop is under 1% of the run, so the workers and not the sender are the constraint. 8 actors means at most 8 can run at once under the single-method invariant. Mirrors protoST's saturation_8a.st. `processed` is the sum the actors computed, not a message count.
- **saturation-32** (`benchmarks/actors/actor-saturation-32.scala`, N=4800): 32 actors x N/32 CPU-bound messages, identical total work to saturation-8. More runnable actors than workers at every worker count, so it separates 'the scheduler cannot fill the cores' from '8 actors cannot fill the cores'. Mirrors protoST's saturation_32a.st.

## High-band ask latency under a Low-band flood

Latencies pooled across the 5 interleaved samples for each worker count.

| workers | samples (pooled) | p50 (µs) | p99 (µs) |
|---|---|---|---|
| 1 | 5000 | 27.2 | 2765.9 |
| 2 | 5000 | 31.0 | 2910.6 |
| 4 | 5000 | 33.4 | 3066.6 |
| 6 | 5000 | 35.7 | 2929.5 |
| 8 | 5000 | 38.1 | 3038.0 |
| 16 | 5000 | 40.4 | 3057.5 |

## protoClojure, same machine, same conditions (interleaved sample-for-sample)

protoClojure's twin scripts (`actor-throughput.clj`, `actor-fanout.clj`,
`actor-mpsc.clj`, `actor-mpmc.clj`) run the same four shapes with their own
1M-message sizes, invoked directly (not through protoClojure's own
`actor-bench.sh` wrapper) so every sample interleaves with the matching
protoScala sample at the same mode and worker count. `ping-pong`, `await`
and `priority` have no protoClojure twin, so they are not compared.

| mode | workers | protoScala msg/s [min-max] | protoClojure msg/s [min-max] | ratio (median) |
|---|---|---|---|---|
| single | 1 | 146,274 [133,989-187,738] | 249,992 [208,785-310,718] | 0.59x |
| single | 2 | 163,386 [158,493-179,274] | 292,607 [257,975-317,234] | 0.56x |
| single | 4 | 152,691 [123,580-161,190] | 274,624 [238,728-298,214] | 0.56x |
| single | 6 | 133,771 [102,365-139,658] | 271,653 [197,386-301,051] | 0.49x |
| single | 8 | 127,707 [108,125-132,232] | 292,164 [227,919-306,634] | 0.44x |
| single | 16 | 116,432 [106,034-121,675] | 284,862 [251,714-291,368] | 0.41x |
| fan-out | 1 | 164,331 [145,664-170,023] | 307,294 [283,999-314,014] | 0.53x |
| fan-out | 2 | 176,401 [169,425-185,013] | 451,734 [432,285-474,899] | 0.39x |
| fan-out | 4 | 165,400 [151,782-174,395] | 506,813 [460,393-526,007] | 0.33x |
| fan-out | 6 | 153,551 [146,815-164,255] | 497,542 [481,852-517,634] | 0.31x |
| fan-out | 8 | 141,814 [135,739-146,930] | 505,623 [478,967-521,857] | 0.28x |
| fan-out | 16 | 105,955 [103,334-108,216] | 501,617 [469,827-516,027] | 0.21x |
| MPSC | 1 | 180,070 [160,549-184,084] | 283,021 [219,802-285,857] | 0.64x |
| MPSC | 2 | 174,840 [139,697-182,585] | 274,406 [165,153-283,186] | 0.64x |
| MPSC | 4 | 141,371 [101,856-166,826] | 282,346 [191,515-291,421] | 0.50x |
| MPSC | 6 | 144,065 [118,403-144,557] | 273,008 [258,348-289,619] | 0.53x |
| MPSC | 8 | 133,694 [125,453-134,601] | 281,422 [259,256-287,158] | 0.48x |
| MPSC | 16 | 119,125 [117,908-123,244] | 276,623 [267,705-298,426] | 0.43x |
| MPMC | 1 | 181,536 [166,236-187,708] | 230,683 [221,270-232,730] | 0.79x |
| MPMC | 2 | 265,117 [246,147-285,765] | 337,988 [335,768-351,039] | 0.78x |
| MPMC | 4 | 383,875 [341,758-394,358] | 322,493 [312,863-334,071] | 1.19x |
| MPMC | 6 | 358,561 [301,688-362,874] | 331,820 [324,507-340,095] | 1.08x |
| MPMC | 8 | 329,481 [323,813-333,553] | 348,986 [311,606-352,694] | 0.94x |
| MPMC | 16 | 307,112 [303,013-322,294] | 350,275 [318,947-361,011] | 0.88x |
| saturation-8 | 1 | 756 [722-762] | 545 [507-552] | 1.39x |
| saturation-8 | 2 | 1,484 [1,292-1,542] | 1,046 [971-1,085] | 1.42x |
| saturation-8 | 4 | 2,439 [2,350-2,747] | 1,804 [1,662-1,913] | 1.35x |
| saturation-8 | 6 | 2,507 [2,414-2,561] | 1,959 [1,786-2,052] | 1.28x |
| saturation-8 | 8 | 2,425 [2,208-2,669] | 2,066 [1,960-2,106] | 1.17x |
| saturation-8 | 16 | 2,387 [2,086-2,492] | 2,046 [1,886-2,233] | 1.17x |
| saturation-32 | 1 | 740 [569-793] | 539 [423-552] | 1.37x |
| saturation-32 | 2 | 1,369 [1,081-1,393] | 1,000 [838-1,106] | 1.37x |
| saturation-32 | 4 | 2,544 [2,252-2,699] | 1,826 [1,261-1,885] | 1.39x |
| saturation-32 | 6 | 2,538 [1,651-2,934] | 2,021 [1,207-2,315] | 1.26x |
| saturation-32 | 8 | 2,578 [1,957-2,832] | 1,846 [1,526-2,187] | 1.40x |
| saturation-32 | 16 | 2,344 [2,235-2,570] | 1,951 [1,686-2,185] | 1.20x |

## How to reproduce

```bash
benchmarks/actor-bench.sh --name actors
```

