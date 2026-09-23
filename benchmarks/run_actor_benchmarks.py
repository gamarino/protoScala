#!/usr/bin/env python3
"""Actor benchmark harness — the seven modes of docs/DESIGN.md §8.5 plus the
two CPU-bound `saturation-*` modes that cover the actors-versus-workers axis.

Every script self-reports the work it did (`mode=... messages=... processed=...`,
then `Actor.stats`, then `ok` or `FAILED`) and this runner verifies that report
**before** it computes any rate: a silent failure must never read as infinite
throughput (protoClojure's 2026-06-14 lesson, protoPython's sprint-9 lesson).

This machine is a shared daily-driver desktop with a load-average floor of
roughly 2-3 on 12 cores that never drops to zero. Rather than wait for a quiet
window, every cell is sampled several times, **round-robin across modes,
worker counts and runtimes** (one sample of protoScala at (mode, w), then one
sample of protoClojure at the same (mode, w) if it has a twin, then the next
(mode, w) pair, repeated for SAMPLES rounds) so that ambient load hits every
column alike over the course of the run. The report gives the median rate and
its [min-max] spread for every cell; ratios to protoClojure are the primary
comparison, absolute msg/s is indicative only.

Usage:
    benchmarks/run_actor_benchmarks.py [--name NAME] [--workers 1,2,4,6,8,16]
                                       [--only single,MPSC] [--size N]
                                       [--samples 5] [--no-compare]

Honoured env vars: PROTOSCALA_BIN, PROTOCLJ_BIN, PROTOCLJ_BENCH_DIR.
"""
from __future__ import annotations

import argparse
import datetime
import os
import re
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
ACTOR_DIR = SCRIPT_DIR / "actors"
REPORTS = SCRIPT_DIR / "reports"
WORKSPACE = PROJECT_ROOT.parent

sys.path.insert(0, str(SCRIPT_DIR))
from run_benchmarks import (  # noqa: E402  (imported for the shared machine header)
    build_type,
    fmt_load,
    git_rev,
    loadavg,
    machine_info,
    median,
)

DEFAULT_WORKERS = [1, 2, 4, 6, 8, 16]
DEFAULT_SAMPLES = 5

# sum(1..20000), the compute every saturation message folds into its actor's
# state. The saturation scripts assert the computed sum, not the exit code.
SATURATION_PER_MSG = 200010000

# (name, script, default N, expected processed, note)
MODES = [
    ("single", "actor-throughput.scala", 1000000, lambda n: n + 1,
     "1 sender thread x 1 actor: the per-actor pipeline floor. The "
     "single-method invariant serialises one actor, so more workers cannot help."),
    ("fan-out", "actor-fanout.scala", 1000000, lambda n: (n // 1000) * 1000,
     "1 sender thread x 1000 actors: ready-queue stress."),
    ("MPSC", "actor-mpsc.scala", 1000000, lambda n: (n // 4) * 4,
     "4 OS producer threads -> 1 actor: sender contention on one mailbox."),
    ("MPMC", "actor-mpmc.scala", 1000000, lambda n: (n // 4) * 4,
     "4 OS producer threads x 4 actors, round-robin."),
    ("ping-pong", "actor-pingpong.scala", 100000, lambda n: n // 2,
     "ask/reply latency through the cooperative suspension: every round trip "
     "is two messages, one frame snapshot and one resume."),
    ("await", "actor-await.scala", 100000, lambda n: 100 * (n // 200),
     "100 caller actors awaiting one shared echo actor. Must complete at "
     "every worker count, including 1."),
    ("priority", "actor-priority.scala", 200000, lambda n: n + 1001,
     "a Low-band flood plus 1000 timed High-band asks; the p50/p99 below are "
     "the High-band ask latencies measured with System.nanoTime."),
    ("saturation-8", "actor-saturation-8.scala", 4800,
     lambda n: ((n // 8) * 8 + 8) * SATURATION_PER_MSG,
     "8 actors x N/8 CPU-bound messages (20,000-iteration summation each, "
     "~1.5 ms) from one sender. The only modes that can exhibit a rise up to "
     "the physical core count: the send loop is under 1% of the run, so the "
     "workers and not the sender are the constraint. 8 actors means at most 8 "
     "can run at once under the single-method invariant. Mirrors protoST's "
     "saturation_8a.st. `processed` is the sum the actors computed, not a "
     "message count."),
    ("saturation-32", "actor-saturation-32.scala", 4800,
     lambda n: ((n // 32) * 32 + 32) * SATURATION_PER_MSG,
     "32 actors x N/32 CPU-bound messages, identical total work to "
     "saturation-8. More runnable actors than workers at every worker count, "
     "so it separates 'the scheduler cannot fill the cores' from '8 actors "
     "cannot fill the cores'. Mirrors protoST's saturation_32a.st."),
]

# protoClojure's twin script for the shapes it also measures.
CLJ_SCRIPTS = {
    "single": "actor-throughput.clj",
    "fan-out": "actor-fanout.clj",
    "MPSC": "actor-mpsc.clj",
    "MPMC": "actor-mpmc.clj",
    "saturation-8": "actor-saturation-8.clj",
    "saturation-32": "actor-saturation-32.clj",
}

# The message count each protoClojure twin must report. The older twins all
# carry 1,000,000 trivial messages; the saturation twins carry a few thousand
# expensive ones, so a single global floor would reject them. Their counts
# match their protoScala twins exactly (4800 sends + one closing probe per
# actor), which is what makes the two curves comparable.
CLJ_MIN_MESSAGES = {
    "single": 1000000,
    "fan-out": 1000000,
    "MPSC": 1000000,
    "MPMC": 1000000,
    "saturation-8": 4808,
    "saturation-32": 4832,
}

# Twins that also assert the value they computed. `processed` here is the sum
# the actors folded into their state, so a handler that silently did no work
# fails the check instead of reading as a very fast run. The protoClojure
# closing probe returns the state without adding to it, hence N and not N + K.
CLJ_EXPECTED_PROCESSED = {
    "saturation-8": 4800 * SATURATION_PER_MSG,
    "saturation-32": 4800 * SATURATION_PER_MSG,
}


def percentile(xs, q):
    if not xs:
        return None
    s = sorted(xs)
    k = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[k]


def spread(xs):
    return (min(xs), max(xs)) if xs else None


def first_existing(paths):
    for p in paths:
        if Path(p).exists():
            return Path(p)
    return None


def protoscala_binary():
    env = os.environ.get("PROTOSCALA_BIN")
    if env:
        return Path(env)
    return first_existing([PROJECT_ROOT / "build_release" / "protoscala"])


def protoclj_binary():
    env = os.environ.get("PROTOCLJ_BIN")
    if env:
        return Path(env)
    return first_existing([WORKSPACE / "protoClojure" / "build_release" / "protoclj",
                           WORKSPACE / "protoClojure" / "build" / "protoclj"])


def protoclj_bench_dir():
    env = os.environ.get("PROTOCLJ_BENCH_DIR")
    if env:
        return Path(env)
    return WORKSPACE / "protoClojure" / "benchmarks"


def mailbox_backend(binary):
    """The mailbox the measured binary was built with (never guessed)."""
    try:
        out = subprocess.run([str(binary), "--version"], capture_output=True,
                             text=True, timeout=30).stdout
        m = re.search(r"actor mailboxes: ([^)]+)\)", out)
        if m:
            return m.group(1)
    except (OSError, subprocess.SubprocessError):
        pass
    cache = Path(binary).resolve().parent / "CMakeCache.txt"
    if cache.exists() and "PROTOSCALA_HAS_PMQ" in cache.read_text(errors="replace"):
        return "ProtoMPSCQueue"
    return "(unknown)"


class Agg:
    """Accumulates samples for one (mode, workers) cell of one runtime.
    On the first failed sample the cell is marked FAILED for good (a silent
    failure must never be averaged away or read as a fast run)."""

    def __init__(self, mode, workers):
        self.mode, self.workers = mode, workers
        self.rates = []
        self.latencies = []
        self.failure = None

    def add_success(self, rate, latencies=None):
        if self.failure:
            return
        self.rates.append(rate)
        if latencies:
            self.latencies.extend(latencies)

    def add_failure(self, why):
        if not self.failure:
            self.failure = why

    @property
    def rate_median(self):
        return None if self.failure or not self.rates else median(self.rates)

    @property
    def rate_spread(self):
        return None if self.failure or not self.rates else spread(self.rates)


def run_protoscala_sample(binary, mode, script, n, workers):
    """One protoScala sample. Returns (rate, latencies, failure)."""
    env = {**os.environ,
           "PROTOSCALA_ACTOR_WORKERS": str(workers),
           "PROTOSCALA_BENCH_N": str(n)}
    t0 = time.perf_counter()
    try:
        p = subprocess.run([str(binary), str(ACTOR_DIR / script)], env=env,
                           capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        return None, None, "timed out after 900 s"
    seconds = time.perf_counter() - t0
    out = p.stdout.strip().splitlines()
    if p.returncode != 0:
        return None, None, f"exit {p.returncode}: {(p.stderr or p.stdout).strip()[-200:]}"
    if not out or out[-1] != "ok":
        return None, None, f"the script did not print 'ok' (last line: {out[-1] if out else '<none>'})"
    head = next((ln for ln in out if ln.startswith("mode=")), "")
    m = re.match(r"mode=(\S+) messages=(\d+) processed=(-?\d+)", head)
    if not m:
        return None, None, "no self-report line"
    if m.group(1) != mode:
        return None, None, f"the script reports mode={m.group(1)}, expected {mode}"
    messages, processed = int(m.group(2)), int(m.group(3))
    stats_line = next((ln for ln in out if ln.startswith("ActorStats(")), "")
    ms = re.match(r"ActorStats\((\d+),\s*(\d+)\)", stats_line)
    if not ms:
        return None, None, "no Actor.stats line"
    stats = (int(ms.group(1)), int(ms.group(2)))
    if stats[1] < messages:
        return None, None, (f"Actor.stats reports {stats[1]} messages, fewer than the "
                            f"{messages} the script claims")
    lat = next((ln for ln in out if ln.startswith("latencies=")), None)
    latencies = [int(x) for x in lat[len("latencies="):].split() if x] if lat else []
    if seconds <= 0:
        return None, None, "non-positive elapsed time"
    return (messages / seconds, processed, latencies), latencies, None


def run_clj_sample(binary, bench_dir, mode, workers):
    """One protoClojure sample of the twin shape, run directly (no wrapper
    script) so it can be interleaved with protoScala sample-for-sample.
    Returns (rate, failure)."""
    script = bench_dir / CLJ_SCRIPTS[mode]
    env = {**os.environ, "PROTOCLJ_ACTOR_WORKERS": str(workers)}
    t0 = time.perf_counter()
    try:
        p = subprocess.run([str(binary), str(script)], env=env,
                           capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        return None, "timed out after 900 s"
    seconds = time.perf_counter() - t0
    out = (p.stdout or "") + (p.stderr or "")
    if p.returncode != 0:
        return None, f"exit {p.returncode}: {out.strip()[-200:]}"
    m = re.search(r":messages-processed\s+(\d+)", out)
    if not m:
        return None, f"no :messages-processed in output: {out.strip()[-200:]}"
    processed = int(m.group(1))
    floor = CLJ_MIN_MESSAGES[mode]
    if processed < floor:
        return None, f"processed {processed}, expected at least {floor}"
    # Twins that report the value they computed are checked against it, so a
    # handler that silently did no work cannot be recorded as a fast run.
    want = CLJ_EXPECTED_PROCESSED.get(mode)
    if want is not None:
        head = re.search(r"mode=(\S+) messages=(\d+) processed=(-?\d+)", out)
        if not head:
            return None, "no self-report line"
        if head.group(1) != mode:
            return None, f"the script reports mode={head.group(1)}, expected {mode}"
        got = int(head.group(3))
        if got != want:
            return None, f"computed {got}, expected {want}"
        if not re.search(r"^ok$", out, re.M):
            return None, "the script did not print 'ok'"
    if seconds <= 0:
        return None, "non-positive elapsed time"
    return processed / seconds, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="actors")
    ap.add_argument("--workers", default=",".join(str(w) for w in DEFAULT_WORKERS))
    ap.add_argument("--only", default="")
    ap.add_argument("--size", type=int, default=0,
                    help="override PROTOSCALA_BENCH_N for every mode")
    ap.add_argument("--samples", type=int, default=DEFAULT_SAMPLES,
                    help="samples per cell, interleaved round-robin (default %(default)s)")
    ap.add_argument("--no-compare", action="store_true")
    args = ap.parse_args()

    binary = protoscala_binary()
    if not binary or not binary.exists():
        print("protoscala binary not found; build build_release first", file=sys.stderr)
        return 2
    workers = [int(w) for w in args.workers.split(",") if w]
    only = {s.strip() for s in args.only.split(",") if s.strip()}
    modes = [m for m in MODES if not only or m[0] in only]
    samples = max(1, args.samples)

    cpu, phys, logical = machine_info()
    backend = mailbox_backend(binary)

    clj_bin = None if args.no_compare else protoclj_binary()
    clj_dir = protoclj_bench_dir()
    clj_available = bool(clj_bin and clj_bin.exists() and clj_dir.exists())
    if not args.no_compare and not clj_available:
        print(f"note: protoClojure not available for comparison "
              f"(binary={clj_bin}, dir={clj_dir})", file=sys.stderr)

    # Build the flat cell list once so every round visits cells in the same
    # order -- only the runtime interleaving (protoScala then protoClojure)
    # varies within a cell.
    cell_list = [(name, script, args.size or default_n, expected, w)
                 for name, script, default_n, expected, _note in modes
                 for w in workers]

    ps_results = {(name, w): Agg(name, w) for name, _s, _n, _e, _note in modes for w in workers}
    clj_results = {(name, w): Agg(name, w) for name in CLJ_SCRIPTS for w in workers} \
        if clj_available else {}

    started = datetime.datetime.now()
    load_start = loadavg()
    print(f"protoScala actor-bench harness -- {started:%Y-%m-%d %H:%M}")
    print(f"  binary:     {binary} ({build_type(binary)}, mailboxes: {backend})")
    print(f"  protoclj:   {clj_bin if clj_available else '(not compared)'}")
    print(f"  modes:      {', '.join(m[0] for m in modes)}")
    print(f"  workers:    {workers}")
    print(f"  samples:    {samples} per cell, interleaved round-robin "
          f"(protoScala {'and protoClojure ' if clj_available else ''}alike)")
    print(f"  load average at start: {fmt_load(load_start)}\n")

    load_mid = None
    mid_round = samples // 2
    total_cells = len(cell_list)
    for s in range(samples):
        print(f"== round {s + 1}/{samples} ==", flush=True)
        for name, script, n, expected, w in cell_list:
            ps = ps_results[(name, w)]
            rate_lat_proc, latencies, failure = run_protoscala_sample(binary, name, script, n, w)
            if failure:
                ps.add_failure(failure)
                print(f"  protoScala {name:<9} w={w:<3} FAILED: {failure}", flush=True)
            else:
                rate, processed, _lat = rate_lat_proc
                if processed != expected(n):
                    ps.add_failure(f"processed {processed}, expected {expected(n)}")
                    print(f"  protoScala {name:<9} w={w:<3} FAILED: "
                          f"processed {processed}, expected {expected(n)}", flush=True)
                else:
                    ps.add_success(rate, latencies)
                    print(f"  protoScala {name:<9} w={w:<3} {rate:,.0f} msg/s", flush=True)

            if clj_available and name in CLJ_SCRIPTS:
                cj = clj_results[(name, w)]
                rate, failure = run_clj_sample(clj_bin, clj_dir, name, w)
                if failure:
                    cj.add_failure(failure)
                    print(f"  protoClojure {name:<9} w={w:<3} FAILED: {failure}", flush=True)
                else:
                    cj.add_success(rate)
                    print(f"  protoClojure {name:<9} w={w:<3} {rate:,.0f} msg/s", flush=True)
        if s == mid_round:
            load_mid = loadavg()
            print(f"  [load average at midpoint: {fmt_load(load_mid)}]", flush=True)

    load_end = loadavg()
    print(f"\n  load average at end: {fmt_load(load_end)}")

    REPORTS.mkdir(exist_ok=True)
    path = REPORTS / f"{started:%Y-%m-%d}-{args.name}.md"
    path.write_text(render(args, binary, backend, cpu, phys, logical, started,
                           load_start, load_mid, load_end, workers, modes, samples,
                           ps_results, clj_results, clj_available, clj_bin))
    print(f"\nwrote {path}")
    failures = [k for k, r in ps_results.items() if r.failure]
    failures += [k for k, r in clj_results.items() if r.failure]
    if failures:
        print(f"{len(failures)} cell(s) FAILED and were not turned into a rate.")
    return 0


def render(args, binary, backend, cpu, phys, logical, started, load_start, load_mid,
           load_end, workers, modes, samples, ps_results, clj_results, clj_available, clj_bin):
    out = []
    a = out.append
    a(f"# protoScala actor benchmarks — {started:%Y-%m-%d}\n")
    a("The seven modes of docs/DESIGN.md §8.5 plus the two CPU-bound `saturation-*`")
    a("modes. Every script self-reports the work")
    a("it did and this runner verified that report before computing any rate; a")
    a("cell that failed any check is printed as FAILED and never as a number.\n")
    a("This machine is a shared daily-driver desktop (VS Code, Chrome and PyCharm")
    a("run throughout; load-average floor ~2-3 on 12 logical CPUs). Rather than wait")
    a(f"for a quiet window, every cell was sampled {samples} times, round-robin across")
    a("modes, worker counts and runtimes (one protoScala sample, then one protoClojure")
    a("sample at the same mode/workers when a twin exists, then the next cell), so")
    a("ambient load hits every column alike. Median msg/s is the headline number and")
    a("`[min-max]` is its spread; ratios to protoClojure are the primary comparison,")
    a("absolute msg/s is indicative only.\n")
    a("| | |")
    a("|---|---|")
    a(f"| machine | {cpu} |")
    a(f"| cores | {phys} physical / {logical} logical |")
    a(f"| date | {started:%Y-%m-%d %H:%M} |")
    a(f"| protoScala | {git_rev(PROJECT_ROOT)} |")
    a(f"| binary | `{binary}` ({build_type(binary)}) |")
    a(f"| actor mailboxes | {backend} |")
    a(f"| protoCore | {git_rev(PROJECT_ROOT.parent / 'protoCore')} |")
    if clj_available:
        a(f"| protoClojure | {git_rev(PROJECT_ROOT.parent / 'protoClojure')} |")
        a(f"| protoClojure binary | `{clj_bin}` |")
    a(f"| samples per cell | {samples}, interleaved round-robin |")
    a(f"| load average at start | {fmt_load(load_start)} |")
    if load_mid:
        a(f"| load average at midpoint | {fmt_load(load_mid)} |")
    a(f"| load average at end | {fmt_load(load_end)} |")
    a("")
    a("## Rates (messages per second, median [min-max] over "
      f"{samples} interleaved samples, verified)\n")
    header = "| mode | " + " | ".join(f"w={w}" for w in workers) + " | peak (median) |"
    a(header)
    a("|" + "---|" * (len(workers) + 2))
    for name, _script, default_n, _exp, _note in modes:
        cells, best, bestw = [], None, None
        for w in workers:
            r = ps_results[(name, w)]
            if r.failure or not r.rates:
                cells.append("FAILED" if r.failure else "not measured")
                continue
            lo, hi = r.rate_spread
            cells.append(f"{r.rate_median:,.0f} [{lo:,.0f}-{hi:,.0f}]")
            if best is None or r.rate_median > best:
                best, bestw = r.rate_median, w
        peak = f"{best:,.0f} @ w={bestw}" if best else "—"
        a(f"| {name} | " + " | ".join(cells) + f" | {peak} |")
    a("")
    a("## What each mode measures\n")
    for name, script, default_n, _exp, note in modes:
        a(f"- **{name}** (`benchmarks/actors/{script}`, N={args.size or default_n}): {note}")
    a("")
    fails = [(k, r) for k, r in ps_results.items() if r.failure]
    fails += [(("protoClojure", *k), r) for k, r in clj_results.items() if r.failure]
    if fails:
        a("## Failed cells\n")
        for k, r in sorted(fails, key=lambda kv: str(kv[0])):
            if len(k) == 2:
                name, w = k
                a(f"- `{name}` at {w} worker(s): {r.failure}")
            else:
                _tag, name, w = k
                a(f"- protoClojure `{name}` at {w} worker(s): {r.failure}")
        a("")
    prio = [(w, ps_results[("priority", w)]) for w in workers if ("priority", w) in ps_results]
    prio = [(w, r) for w, r in prio if r.latencies]
    if prio:
        a("## High-band ask latency under a Low-band flood\n")
        a(f"Latencies pooled across the {samples} interleaved samples for each worker count.\n")
        a("| workers | samples (pooled) | p50 (µs) | p99 (µs) |")
        a("|---|---|---|---|")
        for w, r in prio:
            a(f"| {w} | {len(r.latencies)} | "
              f"{percentile(r.latencies, 0.50) / 1000:.1f} | "
              f"{percentile(r.latencies, 0.99) / 1000:.1f} |")
        a("")
    a("## protoClojure, same machine, same conditions (interleaved sample-for-sample)\n")
    if not clj_available:
        why = "PROTOCLJ_BIN/PROTOCLJ_BENCH_DIR not found, or --no-compare was given"
        a(f"Not available on this machine: {why}. No number is invented for it.\n")
    else:
        a("protoClojure's twin scripts (`actor-throughput.clj`, `actor-fanout.clj`,")
        a("`actor-mpsc.clj`, `actor-mpmc.clj`) run the same four shapes with their own")
        a("1M-message sizes, invoked directly (not through protoClojure's own")
        a("`actor-bench.sh` wrapper) so every sample interleaves with the matching")
        a("protoScala sample at the same mode and worker count. `ping-pong`, `await`")
        a("and `priority` have no protoClojure twin, so they are not compared.\n")
        a("| mode | workers | protoScala msg/s [min-max] | protoClojure msg/s [min-max] | ratio (median) |")
        a("|---|---|---|---|---|")
        for name in CLJ_SCRIPTS:
            for w in workers:
                r = ps_results.get((name, w))
                c = clj_results.get((name, w))
                if not r or r.failure or not r.rates or not c or c.failure or not c.rates:
                    continue
                rlo, rhi = r.rate_spread
                clo, chi = c.rate_spread
                a(f"| {name} | {w} | {r.rate_median:,.0f} [{rlo:,.0f}-{rhi:,.0f}] | "
                  f"{c.rate_median:,.0f} [{clo:,.0f}-{chi:,.0f}] | "
                  f"{r.rate_median / c.rate_median:.2f}x |")
        a("")
    a("## How to reproduce\n")
    a("```bash")
    a("benchmarks/actor-bench.sh --name actors")
    a("```")
    a("")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    sys.exit(main())
