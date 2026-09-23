#!/usr/bin/env python3
"""Actor benchmark harness — the seven modes of docs/DESIGN.md §8.5.

Every script self-reports the work it did (`mode=... messages=... processed=...`,
then `Actor.stats`, then `ok` or `FAILED`) and this runner verifies that report
**before** it computes any rate: a silent failure must never read as infinite
throughput (protoClojure's 2026-06-14 lesson, protoPython's sprint-9 lesson).

Usage:
    benchmarks/run_actor_benchmarks.py [--name NAME] [--workers 1,2,4,6,8,16]
                                       [--only single,MPSC] [--size N]
                                       [--no-compare]

Honoured env vars: PROTOSCALA_BIN, PROTOCLJ_BIN.
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

sys.path.insert(0, str(SCRIPT_DIR))
from run_benchmarks import (  # noqa: E402  (imported for the shared machine header)
    build_type,
    fmt_load,
    git_rev,
    loadavg,
    machine_info,
)

DEFAULT_WORKERS = [1, 2, 4, 6, 8, 16]

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
]


def percentile(xs, q):
    if not xs:
        return None
    s = sorted(xs)
    k = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[k]


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


class Result:
    def __init__(self, mode, workers):
        self.mode, self.workers = mode, workers
        self.seconds = None
        self.messages = None
        self.processed = None
        self.stats = None
        self.failure = None
        self.latencies = []

    @property
    def rate(self):
        if self.failure or not self.seconds:
            return None
        return self.messages / self.seconds


def run_one(binary, mode, script, n, workers):
    r = Result(mode, workers)
    env = {**os.environ,
           "PROTOSCALA_ACTOR_WORKERS": str(workers),
           "PROTOSCALA_BENCH_N": str(n)}
    t0 = time.perf_counter()
    try:
        p = subprocess.run([str(binary), str(ACTOR_DIR / script)], env=env,
                           capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        r.failure = "timed out after 900 s"
        return r
    r.seconds = time.perf_counter() - t0
    out = p.stdout.strip().splitlines()
    if p.returncode != 0:
        r.failure = f"exit {p.returncode}: {(p.stderr or p.stdout).strip()[-200:]}"
        return r
    if not out or out[-1] != "ok":
        r.failure = f"the script did not print 'ok' (last line: {out[-1] if out else '<none>'})"
        return r
    head = next((ln for ln in out if ln.startswith("mode=")), "")
    m = re.match(r"mode=(\S+) messages=(\d+) processed=(-?\d+)", head)
    if not m:
        r.failure = "no self-report line"
        return r
    if m.group(1) != mode:
        r.failure = f"the script reports mode={m.group(1)}, expected {mode}"
        return r
    r.messages, r.processed = int(m.group(2)), int(m.group(3))
    stats = next((ln for ln in out if ln.startswith("ActorStats(")), "")
    ms = re.match(r"ActorStats\((\d+),\s*(\d+)\)", stats)
    if not ms:
        r.failure = "no Actor.stats line"
        return r
    r.stats = (int(ms.group(1)), int(ms.group(2)))
    if r.stats[1] < r.messages:
        r.failure = (f"Actor.stats reports {r.stats[1]} messages, fewer than the "
                     f"{r.messages} the script claims")
        return r
    lat = next((ln for ln in out if ln.startswith("latencies=")), None)
    if lat:
        r.latencies = [int(x) for x in lat[len("latencies="):].split() if x]
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="actors")
    ap.add_argument("--workers", default=",".join(str(w) for w in DEFAULT_WORKERS))
    ap.add_argument("--only", default="")
    ap.add_argument("--size", type=int, default=0,
                    help="override PROTOSCALA_BENCH_N for every mode")
    ap.add_argument("--no-compare", action="store_true")
    args = ap.parse_args()

    binary = protoscala_binary()
    if not binary or not binary.exists():
        print("protoscala binary not found; build build_release first", file=sys.stderr)
        return 2
    workers = [int(w) for w in args.workers.split(",") if w]
    only = {s.strip() for s in args.only.split(",") if s.strip()}
    modes = [m for m in MODES if not only or m[0] in only]

    cpu, phys, logical = machine_info()
    load_start = loadavg()
    started = datetime.datetime.now()
    backend = mailbox_backend(binary)

    results = {}
    for name, script, default_n, expected, _note in modes:
        n = args.size or default_n
        for w in workers:
            print(f"  {name:<10} workers={w:<3} N={n} ...", end="", flush=True)
            r = run_one(binary, name, script, n, w)
            if not r.failure and r.processed != expected(n):
                r.failure = (f"processed {r.processed}, expected {expected(n)}")
            results[(name, w)] = r
            print(f" {'FAILED: ' + r.failure if r.failure else f'{r.rate:,.0f} msg/s'}",
                  flush=True)
    load_end = loadavg()

    clj = None
    if not args.no_compare:
        clj = run_protoclojure()

    REPORTS.mkdir(exist_ok=True)
    path = REPORTS / f"{started:%Y-%m-%d}-{args.name}.md"
    path.write_text(render(args, binary, backend, cpu, phys, logical, started,
                           load_start, load_end, workers, modes, results, clj))
    print(f"\nwrote {path}")
    failures = [k for k, r in results.items() if r.failure]
    if failures:
        print(f"{len(failures)} cell(s) FAILED and were not turned into a rate.")
    return 0


def run_protoclojure():
    bench = Path(os.environ.get("PROTOCLJ_BENCH",
                                PROJECT_ROOT.parent / "protoClojure" / "benchmarks" /
                                "actor-bench.sh"))
    binary = Path(os.environ.get("PROTOCLJ_BIN",
                                 PROJECT_ROOT.parent / "protoClojure" / "build_release" /
                                 "protoclj"))
    if not bench.exists() or not binary.exists():
        return {"available": False,
                "why": f"protoclj not available on this machine ({binary})"}
    print("\n  protoClojure comparison (same machine, same day) ...", flush=True)
    try:
        p = subprocess.run([str(bench), str(binary)], capture_output=True, text=True,
                           timeout=3600)
    except (OSError, subprocess.SubprocessError) as e:
        return {"available": False, "why": f"protoClojure's actor-bench.sh failed: {e}"}
    rows = {}
    for line in p.stdout.splitlines():
        m = re.match(r"(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", line.strip())
        if m:
            rows[(m.group(1), int(m.group(2)))] = int(m.group(5))
    return {"available": bool(rows), "rows": rows, "raw": p.stdout,
            "why": "" if rows else "actor-bench.sh printed no parsable row"}


def render(args, binary, backend, cpu, phys, logical, started, load_start, load_end,
           workers, modes, results, clj):
    out = []
    a = out.append
    a(f"# protoScala actor benchmarks — {started:%Y-%m-%d}\n")
    a("The seven modes of docs/DESIGN.md §8.5. Every script self-reports the work")
    a("it did and this runner verified that report before computing any rate; a")
    a("cell that failed any check is printed as FAILED and never as a number.\n")
    a("| | |")
    a("|---|---|")
    a(f"| machine | {cpu} |")
    a(f"| cores | {phys} physical / {logical} logical |")
    a(f"| date | {started:%Y-%m-%d %H:%M} |")
    a(f"| protoScala | {git_rev(PROJECT_ROOT)} |")
    a(f"| binary | `{binary}` ({build_type(binary)}) |")
    a(f"| actor mailboxes | {backend} |")
    a(f"| protoCore | {git_rev(PROJECT_ROOT.parent / 'protoCore')} |")
    a(f"| load average at start | {fmt_load(load_start)} |")
    a(f"| load average at end | {fmt_load(load_end)} |")
    a("")
    busy = load_start[0] > 1.0 or load_end[0] > 1.0
    if busy:
        a("> **The machine was shared while these numbers were measured.** The load")
        a(f"> average was {fmt_load(load_start)} at the start and {fmt_load(load_end)}")
        a("> at the end: other builds were running on the same host. The absolute")
        a("> rates are therefore a lower bound and the shape across worker counts is")
        a("> noisier than a quiet-machine run would be. They are recorded as measured,")
        a("> not adjusted.\n")
    a("## Rates (messages per second, verified)\n")
    header = "| mode | " + " | ".join(f"w={w}" for w in workers) + " | peak |"
    a(header)
    a("|" + "---|" * (len(workers) + 2))
    for name, _script, default_n, _exp, _note in modes:
        cells, best, bestw = [], None, None
        for w in workers:
            r = results[(name, w)]
            if r.failure:
                cells.append("FAILED")
                continue
            cells.append(f"{r.rate:,.0f}")
            if best is None or r.rate > best:
                best, bestw = r.rate, w
        peak = f"{best:,.0f} @ w={bestw}" if best else "—"
        a(f"| {name} | " + " | ".join(cells) + f" | {peak} |")
    a("")
    a("## What each mode measures\n")
    for name, script, default_n, _exp, note in modes:
        a(f"- **{name}** (`benchmarks/actors/{script}`, N={args.size or default_n}): {note}")
    a("")
    fails = [(k, r) for k, r in results.items() if r.failure]
    if fails:
        a("## Failed cells\n")
        for (name, w), r in sorted(fails):
            a(f"- `{name}` at {w} worker(s): {r.failure}")
        a("")
    prio = [results[("priority", w)] for w in workers if ("priority", w) in results]
    prio = [r for r in prio if r.latencies]
    if prio:
        a("## High-band ask latency under a Low-band flood\n")
        a("| workers | samples | p50 (µs) | p99 (µs) |")
        a("|---|---|---|---|")
        for r in prio:
            a(f"| {r.workers} | {len(r.latencies)} | "
              f"{percentile(r.latencies, 0.50) / 1000:.1f} | "
              f"{percentile(r.latencies, 0.99) / 1000:.1f} |")
        a("")
    a("## protoClojure, same machine and day\n")
    if not clj or not clj.get("available"):
        why = (clj or {}).get("why", "not run")
        a(f"Not available on this machine: {why}. No number is invented for it.\n")
    else:
        a("protoClojure's `benchmarks/actor-bench.sh` runs the same four shapes")
        a("(`single`, `fan-out`, `MPSC`, `MPMC`) with its own 1M-message sizes.")
        a("`ping-pong`, `await` and `priority` have no protoClojure twin, so they")
        a("are not compared.\n")
        a("| mode | workers | protoScala msg/s | protoClojure msg/s | ratio |")
        a("|---|---|---|---|---|")
        for name in ("single", "fan-out", "MPSC", "MPMC"):
            for w in workers:
                r = results.get((name, w))
                c = clj["rows"].get((name, w))
                if not r or r.failure or not c:
                    continue
                a(f"| {name} | {w} | {r.rate:,.0f} | {c:,.0f} | "
                  f"{r.rate / c:.2f}x |")
        a("")
        a("<details><summary>protoClojure raw output</summary>\n")
        a("```")
        a(clj["raw"].rstrip())
        a("```")
        a("</details>\n")
    a("## How to reproduce\n")
    a("```bash")
    a("benchmarks/actor-bench.sh --name actors")
    a("```")
    a("")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    sys.exit(main())
