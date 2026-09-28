#!/usr/bin/env python3
"""Run the Scala 3 (dotty) `tests/run` corpus against a protoScala binary.

Measurement only: this script never modifies the corpus or any repository. It
writes one JSON object per corpus file to a results file, which `score.py` then
reads. `triage.py` assigns each file to a bucket; bucket 3 is the in-scope set.

Scoring follows dotty's own rule for `tests/run`: the program must run and its
output must match its `.check` file. Where a test has no `.check` file, dotty
requires only that it run, and this harness has two readings of that:

  strict  (default) -- exit 0 AND nothing unexpected on stdout
  lenient            -- exit 0, whatever it prints

Both are recorded per test (`rc`, `has_check`, `stdout`), so a results file can be
re-scored either way afterwards without re-running anything. Published protoScala
figures use the strict rule; see tools/corpus/README.md.

One harness adaptation, and it is not a change to protoScala: the corpus is made
of `object X { def main(args: Array[String]) }`, and protoScala runs a file rather
than a class, so a driver line calling that object's `main` is appended to the
source before the run. Tests with no such entry point are run as they are.

Usage (see --help): the corpus directory and the binary are arguments; nothing in
this file is specific to any machine.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

DEFAULT_TIMEOUT = 10

MAIN_RE = re.compile(r"^\s*(?:object|class)\s+([A-Za-z_][A-Za-z0-9_]*)", re.M)
DEFMAIN_RE = re.compile(r"\bdef\s+main\s*\(\s*\w+\s*:\s*Array\s*\[\s*String\s*\]")


def driver_for(src):
    """Return a top-level driver line that invokes the test's entry point."""
    if not DEFMAIN_RE.search(src):
        return None
    pos = DEFMAIN_RE.search(src).start()
    cands = list(re.finditer(r"\bobject\s+([A-Za-z_$][A-Za-z0-9_$]*)", src[:pos]))
    if not cands:
        return None
    return "\n" + cands[-1].group(1) + ".main(Nil)\n"


def run_one(args, name):
    corpus, work, binary, timeout = args.corpus, args.work, args.protoscala, args.timeout
    path = os.path.join(corpus, name + ".scala")
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            src = f.read()
    except OSError as e:
        return {"test": name, "status": "read_error", "detail": str(e)}
    drv = driver_for(src)
    wpath = os.path.join(work, name.replace("/", "_") + ".scala")
    with open(wpath, "w", encoding="utf-8") as f:
        f.write(src + (drv or ""))
    try:
        p = subprocess.run([binary, wpath], capture_output=True, text=True,
                           timeout=timeout, errors="replace")
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return {"test": name, "status": "timeout", "driver": bool(drv)}
    ckp = os.path.join(corpus, name + ".check")
    expected = None
    if os.path.exists(ckp):
        with open(ckp, encoding="utf-8", errors="replace") as f:
            expected = f.read()
    rec = {"test": name, "rc": rc, "driver": bool(drv),
           "has_check": expected is not None,
           "stdout": out[:4000], "stderr": err[:4000]}
    if rc < 0:
        rec["status"] = "signal"
        return rec
    if expected is None:
        # dotty: no checkfile means the program must run. STRICT reading here;
        # score.py can re-read the same record leniently.
        if rc != 0:
            rec["status"] = "fail"
            rec["why"] = "nonzero exit"
        elif out.strip() != "":
            rec["status"] = "fail"
            rec["why"] = "unexpected output (no checkfile)"
        else:
            rec["status"] = "pass"
        return rec

    def norm(s):
        return "\n".join(line.rstrip() for line in s.strip().split("\n"))

    if rc == 0 and norm(out) == norm(expected):
        rec["status"] = "pass"
    elif rc == 0 and norm(out + err) == norm(expected):
        rec["status"] = "pass"
        rec["why"] = "matched with stderr appended"
    else:
        rec["status"] = "fail"
        rec["why"] = "nonzero exit" if rc != 0 else "output mismatch"
        rec["expected"] = expected[:4000]
    return rec


def parse_args(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(os.path.dirname(here))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default=os.environ.get("SCALA3_CORPUS"),
                    help="dotty checkout's tests/run directory "
                         "(default: $SCALA3_CORPUS; see README.md for the clone)")
    ap.add_argument("--protoscala", default=os.environ.get("PROTOSCALA_BIN"),
                    help="the protoscala binary to measure (default: $PROTOSCALA_BIN, "
                         "else build_release/protoscala beside this checkout, else "
                         "protoscala on PATH)")
    ap.add_argument("--out", default="results.jsonl", help="results file to write")
    ap.add_argument("--work", default=None,
                    help="scratch directory for the driver-appended sources "
                         "(default: <out>.work)")
    ap.add_argument("--jobs", type=int, default=3, help="concurrent processes")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                    help="per-file timeout in seconds")
    ap.add_argument("--limit", type=int, default=0,
                    help="run only the first N files (smoke check)")
    a = ap.parse_args(argv)
    if not a.corpus:
        ap.error("no corpus: pass --corpus or set SCALA3_CORPUS "
                 "(tools/corpus/README.md has the clone command)")
    if not a.protoscala:
        cand = os.path.join(repo, "build_release", "protoscala")
        a.protoscala = cand if os.path.exists(cand) else shutil.which("protoscala")
    if not a.protoscala or not os.path.exists(a.protoscala):
        ap.error("no protoscala binary: pass --protoscala or set PROTOSCALA_BIN")
    if not os.path.isdir(a.corpus):
        ap.error(f"corpus directory not found: {a.corpus}")
    a.work = a.work or (a.out + ".work")
    return a


def main(argv=None):
    a = parse_args(argv)
    os.makedirs(a.work, exist_ok=True)
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(a.corpus)
                   if f.endswith(".scala"))
    if a.limit:
        names = names[:a.limit]
    print(f"corpus   {a.corpus} ({len(names)} files)", file=sys.stderr)
    print(f"binary   {a.protoscala}", file=sys.stderr)
    done = 0
    with open(a.out, "w") as outf, ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for rec in ex.map(lambda n: run_one(a, n), names):
            outf.write(json.dumps(rec) + "\n")
            done += 1
            if done % 100 == 0:
                outf.flush()
                print(f"{done}/{len(names)}", file=sys.stderr, flush=True)
    print(f"done {done} -> {a.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
