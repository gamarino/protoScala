#!/usr/bin/env python3
"""
protoScala benchmark harness.

Runs the comparable workloads in `benchmarks/comparable/*.scala` on protoScala
and, side by side, the twin program of each workload on every other runtime
that is available on this machine:

  protoScala          build_release/protoscala (canonical RelWithDebInfo build)
  protoScala Release  build_bench/protoscala (-DCMAKE_BUILD_TYPE=Release), if built
  Scala (JVM)         the SAME .scala file, compiled once with scalac (untimed),
                      run as a fresh `java` process per sample
  CPython             protoPython's .py twin (or the twin in comparable/python/)
  protopy             the same .py twin on protoPython
  protost             protoST's comparable/*.st twin
  protoclj            protoClojure's benchmarks/*.clj twin

A twin runs the same algorithm with the same N. Every sample is one cold
process, so start-up time is part of every figure, for every runtime.

Timing discipline (as protoST's harness): WARMUP_RUNS discarded runs, then
N_RUNS timed runs; the median wall-clock is reported and a geometric mean
aggregates the ratios against CPython. Samples of the runtimes of one workload
are interleaved (round-robin), so that a change of machine load affects all
columns alike.

Every run -- warmup included -- is verified: the program prints its result on
its last line and the harness compares it with the expected value (for a
.scala file, the `// EXPECT:` directive on its first line). A wrong result, a
non-zero exit or a timeout marks that cell FAILED; a FAILED cell is never
counted in a median or a geometric mean.

Usage:
  python3 benchmarks/run_benchmarks.py [--name suite] [--runs 5] [--warmup 2]

Environment overrides:
  PROTOSCALA_BIN          canonical protoscala   (default build_release/protoscala)
  PROTOSCALA_RELEASE_BIN  Release protoscala     (default build_bench/protoscala;
                                                  column skipped if absent)
  SCALA_HOME              Scala 3 distribution   (column skipped if unset)
  JAVA_BIN                java launcher          (default: java)
  CPYTHON_BIN             python interpreter     (default: python3)
  PROTOPY_BIN, PROTOST_BIN, PROTOCLJ_BIN
                          sibling runtimes       (default: autodetected in the
                                                  sibling repositories; column
                                                  skipped if absent)
"""

import argparse
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
WORKSPACE = PROJECT_ROOT.parent
COMPARABLE_DIR = SCRIPT_DIR / "comparable"
PY_TWINS_DIR = COMPARABLE_DIR / "python"
REPORTS_DIR = SCRIPT_DIR / "reports"
JVM_BUILD_DIR = PROJECT_ROOT / "build_jvm"

PROTOPYTHON_BENCH = WORKSPACE / "protoPython" / "benchmarks"
PROTOST_BENCH = WORKSPACE / "protoST" / "benchmarks" / "comparable"
PROTOCLJ_BENCH = WORKSPACE / "protoClojure" / "benchmarks"

N_RUNS = 5
WARMUP_RUNS = 2
TIMEOUT = 120          # seconds per run
MAX_LOAD = 4.0         # 1-minute load average above which the run waits
MAX_WAIT = 20 * 60     # seconds the run may wait for the load to drop
WAIT_STEP = 60

# ---------------------------------------------------------------------------
# Workloads. `py`: (script, env, expected), `st` / `clj`: (script, expected).
# The protoScala and JVM expected value is the file's `// EXPECT:` line.
# ---------------------------------------------------------------------------
FACT100 = ("93326215443944152681699238856266700490715968264381621468592963895217"
           "59999322991560894146397615651828625369792082722375825118521091686400"
           "0000000000000000000000")

WORKLOADS = [
    {"name": "int_sum_loop", "scala": "int_sum_loop.scala",
     "what": "sum of 0 until 100000", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "int_sum_loop.py", {"BENCH_N": "100000"}, "4999950000"),
     # protoST's twin sums 1..N: the same work, a result larger by N.
     "st": (PROTOST_BENCH / "int_sum_loop.st", "5000050000"),
     "clj": None},
    {"name": "fib", "scala": "fib.scala",
     "what": "recursive fib(25)", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "call_recursion.py", {"BENCH_N": "25"}, "75025"),
     "st": (PROTOST_BENCH / "fib.st", "75025"),
     "clj": None},
    {"name": "str_concat", "scala": "str_concat.scala",
     "what": "2000 string concatenations", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "str_concat_loop.py", {"BENCH_N": "2000"}, "2000"),
     "st": (PROTOST_BENCH / "str_concat.st", "2000"),
     "clj": None},
    {"name": "range_iterate", "scala": "range_iterate.scala",
     "what": "count 100000 iterations", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "range_iterate.py", {"BENCH_N": "100000"}, "100000"),
     "st": (PROTOST_BENCH / "range_iterate.st", "100000"),
     "clj": None},
    {"name": "tak", "scala": "tak.scala",
     "what": "tak(18, 12, 6)", "origin": "protoClojure",
     "py": (PY_TWINS_DIR / "tak.py", {}, "7"),
     "st": None,
     "clj": (PROTOCLJ_BENCH / "tak.clj", "7")},
    {"name": "fib30", "scala": "fib30.scala",
     "what": "recursive fib(30)", "origin": "protoClojure",
     "py": (PROTOPYTHON_BENCH / "call_recursion.py", {"BENCH_N": "30"}, "832040"),
     "st": None,
     "clj": (PROTOCLJ_BENCH / "fib.clj", "832040")},
    {"name": "sum_loop", "scala": "sum_loop.scala",
     "what": "sum of 0..1000000", "origin": "protoClojure",
     "py": (PY_TWINS_DIR / "sum_loop.py", {}, "500000500000"),
     "st": None,
     "clj": (PROTOCLJ_BENCH / "sum-loop.clj", "500000500000")},
    {"name": "factorial_100", "scala": "factorial_100.scala",
     "what": "100! (BigInt, 158 digits)", "origin": "protoClojure",
     "py": (PY_TWINS_DIR / "factorial_100.py", {}, FACT100),
     "st": None,
     "clj": (PROTOCLJ_BENCH / "factorial-100.clj", FACT100)},
    {"name": "attr_lookup", "scala": "attr_lookup.scala",
     "what": "3 field reads x 100000", "origin": "protoPython / protoST",
     "py": (PROTOPYTHON_BENCH / "attr_lookup.py", {"BENCH_N": "100000"}, "600000"),
     "st": (PROTOST_BENCH / "attr_lookup.st", "600000"),
     "clj": None},
    {"name": "object_tree", "scala": "object_tree.scala",
     "what": "build, path-copy and fold a 131071-object case-class tree", "origin": "protoScala",
     "py": (PY_TWINS_DIR / "object_tree.py", {}, "131071 278364170 725606090"),
     "st": None,
     "clj": None},
]

# Workloads of the sibling suites that protoScala cannot express yet: each one
# waits for the phase named in its entry (collections, exceptions, actors).
# Listed in the report, never approximated.
PENDING = [
    ("list_append", "protoPython / protoST", "Phase 3 (collections)",
     "Phase 2 builds lists by prepending (`::`, `List(...)`) but has no append "
     "(`:+`, `ListBuffer`); re-spreading varargs would copy the whole list per "
     "step, an O(N^2) algorithm that is not a twin."),
    ("sum_squares", "protoClojure", "Phase 3 (collections)",
     "Phase 2 lists have `map`, but the reduction (`sum`/`foldLeft`) that this "
     "workload folds with arrives in Phase 3."),
    ("exception_latency", "protoPython / protoST", "Phase 4 (exceptions)",
     "`throw` and `try`/`catch`."),
]


def median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return None
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    if not xs:
        return None
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def last_result(stdout):
    """The result a program printed: its last non-empty line, or the
    `result=` field of protoPython's `BENCH_RESULT` line."""
    lines = [ln.strip() for ln in stdout.splitlines() if ln.strip()]
    if not lines:
        return ""
    last = lines[-1]
    m = re.search(r"BENCH_RESULT .*\bresult=(\S+)", last)
    return m.group(1) if m else last


def expect_of(scala_file):
    first = scala_file.read_text().splitlines()[0]
    if not first.startswith("// EXPECT: "):
        sys.exit(f"{scala_file}: first line is not an `// EXPECT:` directive")
    return first[len("// EXPECT: "):].strip()


def main_class_of(scala_file):
    m = re.search(r"@main\s+def\s+(\w+)", scala_file.read_text())
    if not m:
        sys.exit(f"{scala_file}: no @main method")
    return m.group(1)


# ---------------------------------------------------------------------------
# Runtime discovery
# ---------------------------------------------------------------------------
def env_path(var):
    v = os.environ.get(var)
    if not v:
        return None, False
    p = Path(v)
    return (p if p.exists() else None), True


def first_existing(cands):
    for c in cands:
        if c.exists() and os.access(c, os.X_OK):
            return c
    return None


def find_bin(var, cands):
    p, overridden = env_path(var)
    return p if overridden else first_existing(cands)


def build_type(binary):
    """CMAKE_BUILD_TYPE of the build tree the binary lives in."""
    d = Path(binary).resolve().parent
    for _ in range(4):
        cache = d / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text(errors="replace").splitlines():
                if line.startswith("CMAKE_BUILD_TYPE:"):
                    return line.split("=", 1)[1] or "(none)"
            return "(unknown)"
        d = d.parent
    return "(unknown)"


def git_rev(repo):
    try:
        rev = subprocess.run(["git", "-C", str(repo), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(repo), "status", "--porcelain",
                                "--untracked-files=no"],
                               capture_output=True, text=True).stdout.strip()
        return rev + ("-dirty" if dirty else "")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "(unknown)"


def machine_info():
    cpu, phys = platform.processor() or platform.machine(), set()
    try:
        pid = None
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
            elif line.startswith("physical id"):
                pid = line.split(":", 1)[1].strip()
            elif line.startswith("core id"):
                phys.add((pid, line.split(":", 1)[1].strip()))
    except OSError:
        pass
    return cpu, len(phys) or None, os.cpu_count() or 1


def loadavg():
    try:
        return tuple(float(x) for x in Path("/proc/loadavg").read_text().split()[:3])
    except OSError:
        return os.getloadavg()


def fmt_load(la):
    return " / ".join(f"{x:.2f}" for x in la)


# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------
class Cell:
    """One (workload, runtime) measurement."""

    def __init__(self, cmd, expected, env=None, cwd=None):
        self.cmd, self.expected = cmd, expected
        self.env, self.cwd = env or {}, cwd or PROJECT_ROOT
        self.samples, self.failure = [], None

    def run_once(self, record):
        if self.failure:
            return
        full_env = {**os.environ, **self.env}
        start = time.perf_counter()
        try:
            p = subprocess.run(self.cmd, cwd=self.cwd, env=full_env, timeout=TIMEOUT,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        except subprocess.TimeoutExpired:
            self.failure = f"timeout after {TIMEOUT} s"
            return
        except OSError as exc:
            self.failure = f"could not run: {exc}"
            return
        ms = (time.perf_counter() - start) * 1000.0
        got = last_result(p.stdout)
        if p.returncode != 0:
            self.failure = f"exit {p.returncode}: {p.stderr.strip()[-200:]}"
        elif got != self.expected:
            self.failure = f"printed {got[:60]!r}, expected {self.expected[:60]!r}"
        elif record:
            self.samples.append(ms)

    @property
    def median(self):
        return None if self.failure else median(self.samples)

    @property
    def spread(self):
        """(min, max) of the timed samples, or None if failed/empty."""
        if self.failure or not self.samples:
            return None
        return min(self.samples), max(self.samples)


def compile_jvm(scala_home, java_opts):
    """scalac every comparable file once, untimed for the benchmark; returns
    {workload: (compile_seconds, error)}."""
    out = {}
    home = JVM_BUILD_DIR / ".home"   # scalac may write caches under $HOME
    home.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "HOME": str(home), "JAVA_OPTS": java_opts}
    for w in WORKLOADS:
        dest = JVM_BUILD_DIR / w["name"]
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        t0 = time.perf_counter()
        p = subprocess.run([str(scala_home / "bin" / "scalac"), "-d", str(dest),
                            str(COMPARABLE_DIR / w["scala"])],
                           env=env, capture_output=True, text=True)
        secs = time.perf_counter() - t0
        out[w["name"]] = (secs, None if p.returncode == 0 else
                          (p.stderr or p.stdout).strip()[-300:])
    return out


def wait_for_load(max_load, max_wait):
    waited, attempts = 0, []
    la = loadavg()
    attempts.append(la)
    while la[0] > max_load and waited < max_wait:
        print(f"  load average {fmt_load(la)} > {max_load}: waiting {WAIT_STEP} s "
              f"({waited}/{max_wait} s used)", flush=True)
        time.sleep(WAIT_STEP)
        waited += WAIT_STEP
        la = loadavg()
        attempts.append(la)
    return la, waited, attempts


def run_cold_start(binary, runs=21):
    p = subprocess.run([str(SCRIPT_DIR / "cold-start.sh"), str(binary), str(runs)],
                       capture_output=True, text=True)
    rows = []
    for line in p.stdout.splitlines():
        m = re.match(r"(\w+): runs=(\d+) verified=(\d+) median_ms=([\d.]+) "
                     r"min_ms=([\d.]+) max_ms=([\d.]+) target_ms=(\d+)", line)
        if m:
            rows.append(m.groups())
    notes = [ln for ln in p.stdout.splitlines() if "FAIL" in ln]
    return rows, p.returncode, notes


def main():
    global N_RUNS, WARMUP_RUNS
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--name", default="suite", help="report name suffix (default: suite)")
    ap.add_argument("--output", default=None, help="explicit report path")
    ap.add_argument("--runs", type=int, default=N_RUNS)
    ap.add_argument("--warmup", type=int, default=WARMUP_RUNS)
    ap.add_argument("--only", default=None, help="comma-separated workload names")
    ap.add_argument("--max-load", type=float, default=MAX_LOAD)
    ap.add_argument("--max-wait", type=int, default=MAX_WAIT,
                    help="seconds to wait for the load to drop (default 1200)")
    ap.add_argument("--no-cold-start", action="store_true")
    args = ap.parse_args()
    N_RUNS, WARMUP_RUNS = args.runs, args.warmup

    workloads = WORKLOADS
    if args.only:
        wanted = set(args.only.split(","))
        workloads = [w for w in WORKLOADS if w["name"] in wanted]

    # --- runtimes -----------------------------------------------------------
    scala_bin = Path(os.environ.get("PROTOSCALA_BIN",
                                    PROJECT_ROOT / "build_release" / "protoscala"))
    if not scala_bin.exists():
        sys.exit(f"protoscala binary not found: {scala_bin} -- build it first.")
    rel_bin = find_bin("PROTOSCALA_RELEASE_BIN", [PROJECT_ROOT / "build_bench" / "protoscala"])
    cpython = os.environ.get("CPYTHON_BIN", "python3")
    if not shutil.which(cpython):
        sys.exit(f"CPython interpreter not found: {cpython}")
    protopy = find_bin("PROTOPY_BIN", [WORKSPACE / "protoPython" / d for d in (
        "build_release/src/runtime/protopy", "build_release/protopy",
        "build/src/runtime/protopy", "build/protopy")])
    protost = find_bin("PROTOST_BIN", [WORKSPACE / "protoST" / d / "protost"
                                       for d in ("build_release", "build")])
    protoclj = find_bin("PROTOCLJ_BIN", [WORKSPACE / "protoClojure" / d / "protoclj"
                                         for d in ("build_release", "build")])
    scala_home = Path(os.environ["SCALA_HOME"]) if os.environ.get("SCALA_HOME") else None
    if scala_home and not (scala_home / "bin" / "scalac").exists():
        scala_home = None
    java = os.environ.get("JAVA_BIN", "java")
    if scala_home and not shutil.which(java):
        scala_home = None
    # -XX:-UsePerfData: the JVM would otherwise write /tmp/hsperfdata_<user>/<pid>.
    java_flags = ["-XX:-UsePerfData"]

    cpu, cores, ncpu = machine_info()
    date = datetime.now()
    commit = git_rev(PROJECT_ROOT)
    print(f"protoScala benchmark harness  --  {date:%Y-%m-%d %H:%M}")
    print(f"  host:       {cpu}  ({cores} cores, {ncpu} logical CPUs)")
    print(f"  protoscala: {scala_bin}  [{build_type(scala_bin)}, {commit}]")
    print(f"  release:    {rel_bin or '(build_bench not built -- column skipped)'}")
    print(f"  scala JVM:  {scala_home or '(SCALA_HOME unset -- JVM column skipped)'}")
    print(f"  cpython:    {cpython}")
    for label, b in (("protopy", protopy), ("protost", protost), ("protoclj", protoclj)):
        print(f"  {label + ':':<11} {b or '(not found -- column skipped)'}")
    print(f"  runs:       {WARMUP_RUNS} warmup + {N_RUNS} timed, median, "
          f"interleaved, every run verified\n")

    load_start, waited, attempts = wait_for_load(args.max_load, args.max_wait)
    print(f"  load average at start: {fmt_load(load_start)}"
          f"{f' (after waiting {waited} s)' if waited else ''}\n")

    jvm_compile = {}
    if scala_home:
        print("== Compiling the JVM twins with scalac (untimed) ==")
        jvm_compile = compile_jvm(scala_home, "-Xmx768m -Xms768m -XX:-UsePerfData")
        for name, (secs, err) in jvm_compile.items():
            print(f"  {name:<16} {secs:6.1f} s  {'OK' if not err else 'FAILED: ' + err}")
        print()

    # Columns in report order: (key, header, binary-or-None).
    columns = [("protoscala", "protoScala", scala_bin),
               ("release", "protoScala Release", rel_bin),
               ("jvm", "Scala (JVM)", scala_home),
               ("cpython", "CPython", cpython),
               ("protopy", "protopy", protopy),
               ("protost", "protost", protost),
               ("protoclj", "protoclj", protoclj)]
    columns = [c for c in columns if c[2]]

    results = {}
    load_mid = None
    mid_index = len(workloads) // 2
    print("== Comparable workloads ==")
    for idx, w in enumerate(workloads):
        sfile = COMPARABLE_DIR / w["scala"]
        expected = expect_of(sfile)
        cells = {"protoscala": Cell([str(scala_bin), str(sfile)], expected)}
        if rel_bin:
            cells["release"] = Cell([str(rel_bin), str(sfile)], expected)
        if scala_home:
            err = jvm_compile.get(w["name"], (0, "not compiled"))[1]
            cp = f"{scala_home}/lib/*:{JVM_BUILD_DIR / w['name']}"
            c = Cell([java, *java_flags, "-cp", cp, main_class_of(sfile)], expected)
            if err:
                c.failure = f"scalac failed: {err}"
            cells["jvm"] = c
        if w["py"]:
            script, env, exp = w["py"]
            cells["cpython"] = Cell([cpython, str(script)], exp, env)
            if protopy:
                cells["protopy"] = Cell([str(protopy), str(script)], exp, env)
        if w["st"] and protost:
            cells["protost"] = Cell([str(protost), str(w["st"][0])], w["st"][1])
        if w["clj"] and protoclj:
            cells["protoclj"] = Cell([str(protoclj), str(w["clj"][0])], w["clj"][1])

        print(f"  {w['name']:<16}", end="", flush=True)
        for i in range(WARMUP_RUNS + N_RUNS):
            for c in cells.values():
                c.run_once(record=i >= WARMUP_RUNS)
        for key, header, _ in columns:
            if key in cells:
                c = cells[key]
                print(f" {key}={'FAILED' if c.failure else f'{c.median:.1f}'}", end="")
        print(flush=True)
        results[w["name"]] = cells
        if idx == mid_index:
            load_mid = loadavg()
            print(f"  [load average at midpoint: {fmt_load(load_mid)}]", flush=True)

    cold = []
    if not args.no_cold_start:
        print("\n== Cold start (benchmarks/cold-start.sh, 21 runs) ==")
        for label, b in (("protoScala", scala_bin), ("protoScala Release", rel_bin)):
            if b:
                rows, rc, notes = run_cold_start(b)
                cold.append((label, build_type(b), rows, rc, notes))
                for r in rows:
                    print(f"  {label:<20} {r[0]:<7} median {r[3]} ms "
                          f"[{r[4]}-{r[5]}] ({r[2]}/{r[1]} verified)")
                for n in notes:
                    print(f"  {label:<20} {n}")

    load_end = loadavg()
    print(f"\n  load average at end: {fmt_load(load_end)}")

    REPORTS_DIR.mkdir(exist_ok=True)
    out = Path(args.output) if args.output else REPORTS_DIR / f"{date:%Y-%m-%d}-{args.name}.md"
    meta = {
        "date": date, "cpu": cpu, "cores": cores, "ncpu": ncpu, "commit": commit,
        "load_start": load_start, "load_mid": load_mid, "load_end": load_end, "waited": waited,
        "attempts": attempts, "max_load": args.max_load,
        "bins": {"protoscala": scala_bin, "release": rel_bin, "cpython": cpython,
                 "protopy": protopy, "protost": protost, "protoclj": protoclj,
                 "jvm": scala_home},
        "java": java,
    }
    write_report(out, meta, columns, workloads, results, jvm_compile, cold)
    print(f"\nReport written: {out}")
    failed = [(w, k) for w, cells in results.items() for k, c in cells.items() if c.failure]
    if failed:
        print(f"{len(failed)} FAILED cell(s): " + ", ".join(f"{w}/{k}" for w, k in failed))
        sys.exit(1)


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
def version_of(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return (p.stdout or p.stderr).strip().splitlines()[0]
    except (OSError, IndexError, subprocess.TimeoutExpired):
        return "(unknown)"


def write_report(path, meta, columns, workloads, results, jvm_compile, cold):
    b = meta["bins"]
    L = []
    L.append(f"# protoScala benchmarks — {meta['date']:%Y-%m-%d}")
    L.append("")
    L.append(f"- **Date:** {meta['date']:%Y-%m-%d %H:%M}")
    L.append(f"- **Machine:** {meta['cpu']} — {meta['cores']} cores, "
             f"{meta['ncpu']} logical CPUs; {platform.system()} {platform.release()} "
             f"({platform.machine()})")
    L.append(f"- **protoScala:** commit `{meta['commit']}`; `build_release/protoscala` "
             f"is {build_type(b['protoscala'])}"
             + (f"; `build_bench/protoscala` is {build_type(b['release'])}"
                if b["release"] else "; no `build_bench` Release build"))
    L.append(f"- **Load average (1/5/15 min):** {fmt_load(meta['load_start'])} at start, "
             + (f"{fmt_load(meta['load_mid'])} at midpoint, " if meta.get("load_mid") else "")
             + f"{fmt_load(meta['load_end'])} at end"
             + (f" (the run waited {meta['waited']} s for the 1-minute load to drop "
                f"below {meta['max_load']})" if meta["waited"] else ""))
    L.append(f"- **Method:** {WARMUP_RUNS} warmup + {N_RUNS} timed runs per cell, "
             "interleaved across runtimes (round-robin: one sample of each runtime, "
             "then the next, so ambient load hits every column alike); median "
             "wall-clock of a cold process (start-up included), spread reported as "
             "`[min-max]` beside every median. This machine is a daily-driver desktop "
             "(VS Code, Chrome and PyCharm run throughout); ratios to CPython are the "
             "primary result, absolute milliseconds are indicative only. Every run's "
             "printed result is verified; a wrong result, non-zero exit or timeout "
             "marks the cell FAILED and it is excluded from every aggregate.")
    L.append("")
    L.append("### Runtimes")
    L.append("")
    L.append("| Column | Binary | Build / version | Twin programs |")
    L.append("|---|---|---|---|")
    L.append(f"| protoScala | `{b['protoscala']}` | {build_type(b['protoscala'])} | "
             "`benchmarks/comparable/*.scala` |")
    if b["release"]:
        L.append(f"| protoScala Release | `{b['release']}` | {build_type(b['release'])} | "
                 "the same `.scala` files |")
    if b["jvm"]:
        L.append(f"| Scala (JVM) | `{b['jvm']}` + `{meta['java']} -XX:-UsePerfData` | "
                 f"{version_of([str(b['jvm'] / 'bin' / 'scalac'), '-version'])}; "
                 f"{version_of([meta['java'], '-version'])} | the same `.scala` files, "
                 "compiled once with `scalac` (untimed) |")
    L.append(f"| CPython | `{b['cpython']}` | "
             f"{version_of([b['cpython'], '--version'])} | protoPython's "
             "`benchmarks/*.py` (with `BENCH_N`), or `benchmarks/comparable/python/*.py` |")
    if b["protopy"]:
        L.append(f"| protopy | `{b['protopy']}` | {build_type(b['protopy'])}, "
                 f"protoPython `{git_rev(WORKSPACE / 'protoPython')}` | the same `.py` files |")
    if b["protost"]:
        L.append(f"| protost | `{b['protost']}` | {build_type(b['protost'])}, "
                 f"protoST `{git_rev(WORKSPACE / 'protoST')}` | "
                 "protoST's `benchmarks/comparable/*.st` |")
    if b["protoclj"]:
        L.append(f"| protoclj | `{b['protoclj']}` | {build_type(b['protoclj'])}, "
                 f"protoClojure `{git_rev(WORKSPACE / 'protoClojure')}` | "
                 "protoClojure's `benchmarks/*.clj` |")
    L.append("")
    L.append("## Comparable workloads")
    L.append("")
    L.append("Median wall-clock in milliseconds per cold process. `—`: the runtime "
             "has no twin of that workload. The last column is protoScala "
             "(canonical build) ÷ CPython (>1: protoScala slower). The geomean row "
             "gives, per column, the geometric mean of its ratio to CPython over the "
             "rows where both cells verified (row count in parentheses).")
    L.append("")
    L.extend(results_table(columns, workloads, results))
    L.append("")
    failures = [(w, k, c.failure) for w, cells in results.items()
                for k, c in cells.items() if c.failure]
    if failures:
        L.append("### FAILED cells")
        L.append("")
        for w, k, f in failures:
            L.append(f"- `{w}` / {k}: {f}")
        L.append("")
    L.append("### Workloads")
    L.append("")
    L.append("| Workload | Work | Origin of the twin set | Notes |")
    L.append("|---|---|---|---|")
    notes = {
        "int_sum_loop": "protoST's twin sums 1..N (result 5000050000); the others sum 0 until N.",
        "range_iterate": "protoScala: a `while` loop. `for` landed in Phase 2, but `Range` is Phase 3, so the twin keeps the loop.",
        "fib30": "protoClojure's `fib.clj`; CPython/protopy run `call_recursion.py` with `BENCH_N=30`.",
        "factorial_100": "Declared `BigInt` so the JVM does not overflow (Int at 13!, Long at 21!); "
                         "protoScala promotes automatically (D1).",
        "object_tree": "the deep-object-graph workload of DESIGN §1; CPython runs the "
                       "__slots__ twin in comparable/python/.",
    }
    for w in workloads:
        L.append(f"| `{w['name']}` | {w['what']} | {w['origin']} | {notes.get(w['name'], '')} |")
    L.append("")
    if jvm_compile:
        L.append("### JVM compile time (untimed for the table)")
        L.append("")
        L.append("`scalac -d build_jvm/<workload> <file>.scala`, once per workload:")
        L.append("")
        L.append("| Workload | scalac (s) | Status |")
        L.append("|---|---:|---|")
        for name, (secs, err) in jvm_compile.items():
            L.append(f"| `{name}` | {secs:.1f} | {'OK' if not err else 'FAILED'} |")
        L.append("")
    if cold:
        L.append("## Cold start")
        L.append("")
        L.append("`benchmarks/cold-start.sh <binary> <runs>` (self-verifying; target < 25 ms, "
                 "DESIGN §1). Verdict: **MET** if every sample (including the worst) is below "
                 "the target; **MISSED** if the median is at or above it; **STRADDLES** if the "
                 "median is below the target but the spread crosses it (the worst sample is at "
                 "or above 25 ms) — neither met nor missed, and reported as such rather than "
                 "picking a side:")
        L.append("")
        L.append("| Build | Case | Runs | Verified | Median (ms) | Spread [min-max] (ms) | Verdict |")
        L.append("|---|---|---:|---:|---:|---:|---|")
        for label, bt, rows, rc, cnotes in cold:
            for case, runs, ok, med, lo, hi, target in rows:
                medf, hif, tgt = float(med), float(hi), float(target)
                if ok != runs:
                    verdict = "**FAIL** (wrong output)"
                elif hif < tgt:
                    verdict = "MET"
                elif medf >= tgt:
                    verdict = "MISSED"
                else:
                    verdict = "STRADDLES"
                L.append(f"| {label} ({bt}) | {case} | {runs} | {ok} | {med} | "
                         f"[{lo}-{hi}] | {verdict} |")
        L.append("")
    L.append("## Pending workloads")
    L.append("")
    L.append("Workloads of the sibling suites that protoScala cannot express yet; "
             "each waits for the phase named below. They are not approximated.")
    L.append("")
    L.append("| Workload | Suite | Arrives with | Why |")
    L.append("|---|---|---|---|")
    for name, suite, phase, why in PENDING:
        L.append(f"| `{name}` | {suite} | {phase} | {why} |")
    L.append("")
    L.append("## Reproduce")
    L.append("")
    L.append("```bash")
    L.append("SCALA_HOME=/path/to/scala3 benchmarks/bench.sh --name suite")
    L.append("```")
    L.append("")
    path.write_text("\n".join(L) + "\n")


def results_table(columns, workloads, results):
    heads = [h for _, h, _ in columns]
    rows = ["| Workload | " + " | ".join(f"{h} (ms, median [min-max])" for h in heads)
            + " | protoScala ÷ CPython |",
            "|---|" + "---:|" * (len(heads) + 1)]
    ratios = {k: [] for k, _, _ in columns}
    for w in workloads:
        cells = results[w["name"]]
        py = cells.get("cpython")
        py_ms = py.median if py else None
        vals = []
        for key, _, _ in columns:
            c = cells.get(key)
            if c is None:
                vals.append("—")
            elif c.failure:
                vals.append("FAILED")
            else:
                sp = c.spread
                spread_txt = f" [{sp[0]:.1f}-{sp[1]:.1f}]" if sp else ""
                vals.append(f"{c.median:.1f}{spread_txt}")
                if py_ms and key != "cpython":
                    ratios[key].append(c.median / py_ms)
        ps = cells["protoscala"]
        r = (f"{ps.median / py_ms:.2f}×" if (py_ms and not ps.failure) else "—")
        rows.append(f"| `{w['name']}` | " + " | ".join(vals) + f" | {r} |")
    gm = []
    for key, _, _ in columns:
        if key == "cpython":
            gm.append("1.00×")
            continue
        g = geomean(ratios[key])
        gm.append(f"{g:.2f}× ({len(ratios[key])})" if g else "—")
    g_ps = geomean(ratios["protoscala"])
    rows.append("| **Geomean vs CPython** | " + " | ".join(gm)
                + (f" | **{g_ps:.2f}×** |" if g_ps else " | — |"))
    return rows


if __name__ == "__main__":
    main()
