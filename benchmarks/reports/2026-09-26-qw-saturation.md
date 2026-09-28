# protoScala actor benchmarks — 2026-09-26

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

> **Note added 2026-09-27.** The paragraph above is the runner's standing
> boilerplate, and for *this* run it understates the method: the host **was**
> gated on measured idle before the measurement — `mpstat -P ALL 5 3`, mean idle
> 95.43 % (0.55 busy CPUs of 12) — and gate reading 3 in
> [`2026-09-26-quiet-window.md`](2026-09-26-quiet-window.md) §0.1 is that
> measurement's. The load averages tabulated below are context and not the gate:
> they rise during the run and include this benchmark's own worker pools. The
> runner's text has since been corrected so that later reports state this
> themselves rather than advertising the opposite of the claim they support.

| | |
|---|---|
| machine | AMD Ryzen 5 5500U with Radeon Graphics |
| cores | 6 physical / 12 logical |
| date | 2026-09-26 18:13 |
| protoScala | f835aee |
| binary | `/home/gamarino/Documentos/proyectos/.agent_scratch/quiet-window/tree/build_qw_rel/protoscala` (Release) |
| actor mailboxes | ProtoMPSCQueue |
| protoCore | da5f19e2 |
| protoClojure | 39d8353 |
| protoClojure binary | `/home/gamarino/Documentos/proyectos/.agent_scratch/quiet-window/protoClojure/build_release/protoclj` |
| samples per cell | 5, interleaved round-robin |
| load average at start | 0.36 / 0.83 / 1.39 |
| load average at midpoint | 6.81 / 3.62 / 2.39 |
| load average at end | 6.73 / 4.74 / 3.04 |

## Rates (messages per second, median [min-max] over 5 interleaved samples, verified)

| mode | w=1 | w=2 | w=3 | w=4 | w=5 | w=6 | w=8 | w=12 | w=16 | peak (median) |
|---|---|---|---|---|---|---|---|---|---|---|
| saturation-8 | 822 [746-885] | 1,680 [1,070-1,765] | 2,114 [1,878-2,205] | 2,930 [2,672-3,075] | 2,842 [2,575-3,368] | 2,758 [2,549-3,220] | 2,583 [2,388-2,857] | 2,947 [2,567-3,034] | 2,728 [2,696-3,053] | 2,947 @ w=12 |
| saturation-32 | 797 [756-839] | 1,526 [1,484-1,725] | 2,226 [2,061-2,338] | 2,835 [2,681-3,013] | 3,029 [2,541-3,639] | 3,373 [3,178-3,402] | 2,959 [2,143-3,219] | 1,910 [1,629-2,897] | 1,836 [1,822-1,896] | 3,373 @ w=6 |

## What each mode measures

- **saturation-8** (`benchmarks/actors/actor-saturation-8.scala`, N=4800): 8 actors x N/8 CPU-bound messages (20,000-iteration summation each, ~1.5 ms) from one sender. The only modes that can exhibit a rise up to the physical core count: the send loop is under 1% of the run, so the workers and not the sender are the constraint. 8 actors means at most 8 can run at once under the single-method invariant. Mirrors protoST's saturation_8a.st. `processed` is the sum the actors computed, not a message count.
- **saturation-32** (`benchmarks/actors/actor-saturation-32.scala`, N=4800): 32 actors x N/32 CPU-bound messages, identical total work to saturation-8. More runnable actors than workers at every worker count, so it separates 'the scheduler cannot fill the cores' from '8 actors cannot fill the cores'. Mirrors protoST's saturation_32a.st.

## protoClojure, same machine, same conditions (interleaved sample-for-sample)

protoClojure's twin scripts (`actor-throughput.clj`, `actor-fanout.clj`,
`actor-mpsc.clj`, `actor-mpmc.clj`) run the same four shapes with their own
1M-message sizes, invoked directly (not through protoClojure's own
`actor-bench.sh` wrapper) so every sample interleaves with the matching
protoScala sample at the same mode and worker count. `ping-pong`, `await`
and `priority` have no protoClojure twin, so they are not compared.

| mode | workers | protoScala msg/s [min-max] | protoClojure msg/s [min-max] | ratio (median) |
|---|---|---|---|---|
| saturation-8 | 1 | 822 [746-885] | 730 [688-772] | 1.13x |
| saturation-8 | 2 | 1,680 [1,070-1,765] | 1,376 [1,308-1,421] | 1.22x |
| saturation-8 | 3 | 2,114 [1,878-2,205] | 1,740 [1,719-1,888] | 1.21x |
| saturation-8 | 4 | 2,930 [2,672-3,075] | 2,496 [2,226-2,661] | 1.17x |
| saturation-8 | 5 | 2,842 [2,575-3,368] | 2,422 [2,293-2,718] | 1.17x |
| saturation-8 | 6 | 2,758 [2,549-3,220] | 2,189 [2,019-2,907] | 1.26x |
| saturation-8 | 8 | 2,583 [2,388-2,857] | 2,202 [1,751-3,131] | 1.17x |
| saturation-8 | 12 | 2,947 [2,567-3,034] | 2,679 [1,939-3,020] | 1.10x |
| saturation-8 | 16 | 2,728 [2,696-3,053] | 2,356 [1,855-3,031] | 1.16x |
| saturation-32 | 1 | 797 [756-839] | 721 [696-739] | 1.11x |
| saturation-32 | 2 | 1,526 [1,484-1,725] | 1,366 [1,309-1,411] | 1.12x |
| saturation-32 | 3 | 2,226 [2,061-2,338] | 2,000 [1,857-2,035] | 1.11x |
| saturation-32 | 4 | 2,835 [2,681-3,013] | 2,498 [2,229-2,622] | 1.14x |
| saturation-32 | 5 | 3,029 [2,541-3,639] | 2,761 [2,569-2,883] | 1.10x |
| saturation-32 | 6 | 3,373 [3,178-3,402] | 2,874 [2,655-2,979] | 1.17x |
| saturation-32 | 8 | 2,959 [2,143-3,219] | 2,714 [1,854-2,898] | 1.09x |
| saturation-32 | 12 | 1,910 [1,629-2,897] | 1,722 [1,670-2,662] | 1.11x |
| saturation-32 | 16 | 1,836 [1,822-1,896] | 1,684 [1,622-1,714] | 1.09x |

## How to reproduce

```bash
benchmarks/actor-bench.sh --name actors
```

