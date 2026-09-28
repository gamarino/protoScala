#!/usr/bin/env python3
"""Score a corpus run: in-scope and whole-corpus pass rates, under one named rule.

Two inputs, either a live pair or an archived summary:

  --results results.jsonl --triage triage.jsonl   what run_corpus.py and triage.py wrote
  --csv <file>                                    an archived summary in this directory

`--rule strict` (default) is the rule protoScala publishes: a corpus test with no
`.check` file passes only if it exits 0 AND prints nothing unexpected. `--rule
lenient` drops the stdout condition, which is also a defensible reading of dotty's
`tests/run` and is the one some earlier protoScala measurements used. The two
differ by about 0.3 percentage points; a table must not mix them.

`--both` prints both, which is how a mixed table gets caught.
"""
import argparse
import csv
import json
import os
import sys


def load_jsonl(results, triage, rule):
    buckets = {}
    with open(triage) as f:
        for line in f:
            r = json.loads(line)
            buckets[r["test"]] = r["bucket"]
    rows = {}
    with open(results) as f:
        for line in f:
            r = json.loads(line)
            status = r.get("status")
            if "rc" in r and not r.get("has_check"):
                ok = r["rc"] == 0 and (rule == "lenient"
                                       or r.get("stdout", "").strip() == "")
                status = "pass" if ok else "fail"
            rows[r["test"]] = {"bucket": buckets.get(r["test"]),
                               "has_check": bool(r.get("has_check")),
                               "status": status}
    return rows


def load_csv(path, rule):
    rows = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            rows[r["test"]] = {
                "bucket": int(r["bucket"]),
                "has_check": r["has_check"] == "1",
                "status": r["strict" if rule == "strict" else "lenient"],
            }
    return rows


def report(rows, rule, label):
    in_scope = [t for t, r in rows.items() if r["bucket"] == 3]
    passes = [t for t in in_scope if rows[t]["status"] == "pass"]
    all_pass = [t for t, r in rows.items() if r["status"] == "pass"]
    checked = sum(1 for t in passes if rows[t]["has_check"])
    n = len(in_scope)
    print(f"{label}  rule={rule}")
    print(f"  in scope (bucket 3)   {len(passes)}/{n} = {100.0 * len(passes) / n:.1f} %"
          if n else "  in scope: empty")
    print(f"  whole corpus          {len(all_pass)}/{len(rows)} = "
          f"{100.0 * len(all_pass) / len(rows):.1f} %")
    print(f"  of the in-scope passes: {checked} matched a .check file, "
          f"{len(passes) - checked} had none and passed on "
          f"\"exited 0 and printed nothing unexpected\" "
          f"({100.0 * (len(passes) - checked) / len(passes):.0f} %)"
          if passes else "  no in-scope passes")


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results")
    ap.add_argument("--triage", default="triage.jsonl")
    ap.add_argument("--csv", nargs="?", const=os.path.join(here, "2026-09-25-track-s.csv"))
    ap.add_argument("--rule", choices=("strict", "lenient"), default="strict")
    ap.add_argument("--both", action="store_true")
    a = ap.parse_args(argv)
    if not a.results and not a.csv:
        ap.error("pass --results (with --triage) or --csv")
    rules = ("strict", "lenient") if a.both else (a.rule,)
    for rule in rules:
        if a.csv:
            rows = load_csv(a.csv, rule)
            label = os.path.basename(a.csv)
        else:
            rows = load_jsonl(a.results, a.triage, rule)
            label = a.results
        report(rows, rule, label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
