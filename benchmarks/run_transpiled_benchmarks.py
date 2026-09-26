#!/usr/bin/env python3
"""
protoScala: the same source, interpreted and transpiled.

**What this measures, because the obvious reading is wrong.** Both paths call the
SAME opcode bodies -- `src/runtime/OpcodeOps.h`, one implementation with two
consumers, which is the architecture Phase 7 deliberately chose. So this is *not*
compiled-versus-interpreted arithmetic: `a + b` is the same function call either
way. What the transpiled path removes is

  * the dispatch loop: instruction fetch, operand decode, the `switch`;
  * the stack machine's push/pop, replaced by a constant slot index the emitter
    knew at emit time;
  * the per-frame bytecode setup, and (on the whole-process figure) the front
    end -- the parse, desugar and compile of the user program.

What it does **not** remove, and what is byte-for-byte identical on both paths:
dynamic method dispatch through the prototype chain, attribute lookup, allocation
and collection, and every prelude call.

**Do not read the delta as a performance claim.** protoScala's positioning is an
agile, interoperable, easily integrable and very simple Scala -- explicitly not a
fast one. This is a diagnostic. A SMALL delta is the more valuable result: it would
say interpretation is not where the cost lives, and that optimisation effort
belongs in the object model rather than in the dispatch loop.

**Method, which is what decides whether the number is worth anything.** This host is
not quiet: 2-5 of its 12 CPUs carry a desktop and swap is full. So

  * the two paths are interleaved ROUND-ROBIN inside one window, never in blocks,
    so contention hits both columns equally and **the ratio survives even though
    the absolutes do not**;
  * the ratio is the finding; absolutes are marked contended, with the load
    average at the start, the middle and the end, and the sample count and
    min-max spread of every cell;
  * every sample -- warmup included -- is verified against the workload's own
    `// EXPECT:` line. A crash that exits 0 is never a win;
  * a workload with no transpiled twin is LISTED with the reason, never omitted.

Usage:
    benchmarks/run_transpiled_benchmarks.py [--rounds N] [--warmup N] [--only name,...]
"""
import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COMPARABLE = os.path.join(HERE, "comparable")
ACTORS = os.path.join(HERE, "actors")
PS = os.path.join(ROOT, "build_release", "protoscala")
PSC = os.path.join(ROOT, "build_release", "protoscalac")
SCRATCH = os.path.join(ROOT, "..", ".agent_scratch", "phase7-transpiler", "bench-run")

# Three workloads hold a live set that does not fit the ambient sweep ceiling, and
# pin their own in tests/CMakeLists.txt for the same reason.
CEILINGS = {
    "object_tree": "2000000",
    "map_build": "2000000",
    "list_ops": "2000000",
}
TIMEOUT = 600


def load_avg():
    with open("/proc/loadavg") as f:
        return float(f.read().split()[0])


def expect_of(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^//\s*EXPECT:\s*(.*)$", line.rstrip("\n"))
            if m:
                return m.group(1)
    return None


def run_once(cmd, env_extra, expected):
    """One cold process. Returns (seconds, ok, last_line)."""
    env = dict(os.environ)
    env.update(env_extra or {})
    t0 = time.perf_counter()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT,
                           env=env, errors="replace")
    except subprocess.TimeoutExpired:
        return None, False, "<timeout>"
    dt = time.perf_counter() - t0
    last = ""
    for line in p.stdout.splitlines():
        if line.strip():
            last = line.strip()
    ok = p.returncode == 0 and (expected is None or last == expected)
    return dt, ok, last


def build_transpiled(name, src, out_dir):
    """Returns (module_path, None) or (None, refusal-reason)."""
    os.makedirs(out_dir, exist_ok=True)
    for junk in os.listdir(out_dir):
        os.remove(os.path.join(out_dir, junk))
    p = subprocess.run([PSC, src, "-o", out_dir, "--build-so"],
                       capture_output=True, text=True, timeout=TIMEOUT, errors="replace")
    blob = p.stdout + p.stderr
    if p.returncode != 0:
        m = re.search(r"error: ([^\n]*?)\s*\((D\d+)\)", blob)
        if m:
            return None, f"{m.group(2)}: {m.group(1)}"
        m = re.search(r"error: ([^\n]*)", blob)
        return None, (m.group(1) if m else "protoscalac failed")[:160]
    mod = os.path.join(out_dir, "module.so")
    if not os.path.exists(mod):
        return None, "make produced no module.so"
    return mod, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--only", default="")
    args = ap.parse_args()

    for exe in (PS, PSC):
        if not os.access(exe, os.X_OK):
            sys.exit(f"not executable: {exe}")

    os.makedirs(SCRATCH, exist_ok=True)
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(COMPARABLE)
                   if f.endswith(".scala"))
    if args.only:
        wanted = set(args.only.split(","))
        names = [n for n in names if n in wanted]

    # --- transpile everything first, and record what has no twin --------------
    twins, refused = {}, {}
    for n in names:
        src = os.path.join(COMPARABLE, n + ".scala")
        mod, why = build_transpiled(n, src, os.path.join(SCRATCH, n))
        if mod:
            twins[n] = mod
        else:
            refused[n] = why
            print(f"no transpiled twin: {n} -- {why}", file=sys.stderr)

    # The actor workloads are listed too, because a comparison table with silent
    # gaps is the same fault as a wildcard exclusion. They are not timed: `await`
    # is refused at transpile time, so there is nothing to compare against.
    actor_refused = {}
    if os.path.isdir(ACTORS):
        for f in sorted(os.listdir(ACTORS)):
            if not f.endswith(".scala"):
                continue
            n = "actors/" + os.path.splitext(f)[0]
            mod, why = build_transpiled(n, os.path.join(ACTORS, f),
                                        os.path.join(SCRATCH, "actor_" + os.path.splitext(f)[0]))
            actor_refused[n] = why or "transpiles, but the actor suite is not part of this table"

    # --- the null program: each path's fixed cost ----------------------------
    # Reported as an OPERAND, and the work-only ratio derived from it is labelled
    # derived. Without it the whole-process ratio also carries the front end,
    # which is a real saving but not the dispatch loop.
    null_src = os.path.join(SCRATCH, "null.scala")
    with open(null_src, "w") as f:
        f.write("// EXPECT: null-program\n@main def run(): Unit = println(\"null-program\")\n")
    null_mod, null_why = build_transpiled("null", null_src, os.path.join(SCRATCH, "null"))

    samples = {n: {"interp": [], "trans": []} for n in names}
    null_samples = {"interp": [], "trans": []}
    failures = []
    loads = [load_avg()]

    order = [n for n in names]
    total_rounds = args.warmup + args.rounds
    for r in range(total_rounds):
        timed = r >= args.warmup
        if r == total_rounds // 2:
            loads.append(load_avg())
        for n in order:
            src = os.path.join(COMPARABLE, n + ".scala")
            expected = expect_of(src)
            env = {}
            if n in CEILINGS:
                env["PROTOCORE_HEAP_LIMIT_CELLS"] = CEILINGS[n]
            # ROUND-ROBIN: the two paths of one workload are adjacent, so a change
            # in load between them is the smallest it can be.
            dt_i, ok_i, last_i = run_once([PS, src], env, expected)
            if not ok_i:
                failures.append((n, "interpreted", last_i))
            elif timed:
                samples[n]["interp"].append(dt_i)
            if n in twins:
                dt_t, ok_t, last_t = run_once([PS, "--run-module", twins[n]], env, expected)
                if not ok_t:
                    failures.append((n, "transpiled", last_t))
                elif timed:
                    samples[n]["trans"].append(dt_t)
        if null_mod:
            dt_i, ok_i, _ = run_once([PS, null_src], {}, "null-program")
            dt_t, ok_t, _ = run_once([PS, "--run-module", null_mod], {}, "null-program")
            if timed and ok_i and ok_t:
                null_samples["interp"].append(dt_i)
                null_samples["trans"].append(dt_t)
    loads.append(load_avg())

    out = {
        "rounds": args.rounds, "warmup": args.warmup,
        "load_start_mid_end": loads,
        "failures": failures,
        "no_transpiled_twin": refused,
        "actors": actor_refused,
        "null_program": {
            k: ({"median": statistics.median(v), "min": min(v), "max": max(v), "n": len(v)}
                if v else None)
            for k, v in null_samples.items()
        },
        "workloads": {},
    }
    for n in names:
        si, st = samples[n]["interp"], samples[n]["trans"]
        rec = {
            "interp": ({"median": statistics.median(si), "min": min(si), "max": max(si),
                        "n": len(si)} if si else None),
            "trans": ({"median": statistics.median(st), "min": min(st), "max": max(st),
                       "n": len(st)} if st else None),
        }
        if si and st:
            rec["ratio_whole_process"] = statistics.median(st) / statistics.median(si)
            fi = out["null_program"]["interp"]
            ft = out["null_program"]["trans"]
            if fi and ft:
                wi = statistics.median(si) - fi["median"]
                wt = statistics.median(st) - ft["median"]
                # Derived, and labelled so: the fixed cost of each path is
                # subtracted so that what is left is the work, not the start-up
                # and not the front end.
                rec["ratio_work_only_derived"] = (wt / wi) if wi > 0 else None
                rec["work_interp_s"] = wi
                rec["work_trans_s"] = wt
        out["workloads"][n] = rec

    print(json.dumps(out, indent=2))
    if failures:
        print(f"\n{len(failures)} VERIFICATION FAILURES -- no cell above is usable "
              f"for a workload that appears in them", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
