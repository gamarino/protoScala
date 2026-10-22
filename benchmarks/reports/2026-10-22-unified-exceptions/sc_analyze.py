#!/usr/bin/env python3
"""protoScala A/B analysis: interpreter harness, transpiled harness, compile
time and probes; medians of the three per-round medians, AFTER/BEFORE."""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uex_common import med, gmean, probe_table
OUT = sys.argv[1]
V, RR = ("before", "after"), (1, 2, 3)
f2 = lambda s: None if s in (None, "FAILED", "—", "-") else float(s)
def parse_interp(p):
    d, cs = {}, {}
    txt = open(p).read()
    for m in re.finditer(r"^  (\w+)\s+protoscala=(\S+) jvm=(\S+) cpython=(\S+)", txt, re.M):
        d[m.group(1)] = tuple(map(f2, m.groups()[1:]))
    for m in re.finditer(r"protoScala\s+(script|repl)\s+median ([\d.]+) ms .*?\((\d+)/(\d+) verified\)", txt):
        cs[m.group(1)] = float(m.group(2))
    return d, cs, "rc=0" in txt, "FAILED" in txt
I = {(v, r): parse_interp(os.path.join(OUT, "interp_%s_r%d.log" % (v, r))) for v in V for r in RR}
print("## Interpreter harness")
print("rc=0 in all 6 runs:", all(x[2] for x in I.values()), "; any FAILED cell:", any(x[3] for x in I.values()))
print("| Workload | Scala JVM | CPython | BEFORE | AFTER | A/B | BEFORE rounds | AFTER rounds |")
print("|---|---:|---:|---:|---:|---:|---|---|")
ab, bj, aj, bc, ac = [], [], [], [], []
names = list(I[("after", 1)][0])
for k in names:
    b = [I[("before", r)][0][k][0] for r in RR]; a = [I[("after", r)][0][k][0] for r in RR]
    j = med([I[(v, r)][0][k][1] for v in V for r in RR]); c = med([I[(v, r)][0][k][2] for v in V for r in RR])
    ab.append(med(a) / med(b)); bj.append(med(b) / j); aj.append(med(a) / j); bc.append(med(b) / c); ac.append(med(a) / c)
    print("| `%s` | %.1f | %.1f | %.1f | %.1f | %.3f | %s | %s |" % (k, j, c, med(b), med(a), med(a) / med(b),
          "/".join("%.0f" % x for x in b), "/".join("%.0f" % x for x in a)))
print("\ngeomean A/B (%d): %.3f" % (len(ab), gmean(ab)))
print("geomean vs JVM: BEFORE %.2fx AFTER %.2fx; vs CPython: BEFORE %.2fx AFTER %.2fx" % (gmean(bj), gmean(aj), gmean(bc), gmean(ac)))
for s in ("script", "repl"):
    b = [I[("before", r)][1].get(s) for r in RR]; a = [I[("after", r)][1].get(s) for r in RR]
    print("cold start %s: BEFORE %.2f ms AFTER %.2f ms (A/B %.3f)" % (s, med(b), med(a), med(a) / med(b)))
print("\n## Transpiled vs interpreted (run_transpiled_benchmarks.py)")
T = {}
for v in V:
    for r in RR:
        T[(v, r)] = json.load(open(os.path.join(OUT, "trans_%s_r%d.json" % (v, r))))
print("failures:", {k: x["failures"] for k, x in T.items() if x["failures"]} or "none")
print("no transpiled twin:", T[("after", 1)]["no_transpiled_twin"], "| BEFORE:", T[("before", 1)]["no_transpiled_twin"])
print("| Workload | interp BEFORE | interp AFTER | interp A/B | trans BEFORE | trans AFTER | trans A/B | trans/interp BEFORE | trans/interp AFTER |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
ia, ta, tib, tia = [], [], [], []
for k in T[("after", 1)]["workloads"]:
    g = lambda v, p: med([(T[(v, r)]["workloads"][k][p] or {}).get("median") for r in RR])
    ib, iA, tb, tA = g("before", "interp"), g("after", "interp"), g("before", "trans"), g("after", "trans")
    if None in (tb, tA):
        print("| `%s` | %.1f | %.1f | %.3f | — | — | — | — | — |" % (k, ib * 1e3, iA * 1e3, iA / ib)); ia.append(iA / ib); continue
    ia.append(iA / ib); ta.append(tA / tb); tib.append(tb / ib); tia.append(tA / iA)
    print("| `%s` | %.1f | %.1f | %.3f | %.1f | %.1f | %.3f | %.2f | %.2f |" % (k, ib * 1e3, iA * 1e3, iA / ib, tb * 1e3, tA * 1e3, tA / tb, tb / ib, tA / iA))
nb = {p: med([T[("before", r)]["null_program"][p]["median"] for r in RR]) for p in ("interp", "trans")}
na = {p: med([T[("after", r)]["null_program"][p]["median"] for r in RR]) for p in ("interp", "trans")}
print("| null program | %.1f | %.1f | %.3f | %.1f | %.1f | %.3f | | |" % (nb["interp"] * 1e3, na["interp"] * 1e3, na["interp"] / nb["interp"], nb["trans"] * 1e3, na["trans"] * 1e3, na["trans"] / nb["trans"]))
print("\ngeomean interp A/B (%d) %.3f; trans A/B (%d) %.3f; trans/interp BEFORE %.2f AFTER %.2f" % (len(ia), gmean(ia), len(ta), gmean(ta), gmean(tib), gmean(tia)))
print("\n## Compile time (compile.txt)")
C = {}
for line in open(os.path.join(OUT, "compile.txt")):
    m = re.match(r"(\S+) (\S+) rep\d rc=(\d+) ms=([\d.]+) so_bytes=(\d+) cpp_lines=(\d+)", line)
    n, v, rc, ms, so, cl = m.groups()
    C.setdefault(n, {}).setdefault(v, []).append((int(rc), float(ms), int(so), int(cl)))
print("| Source | BEFORE ms | AFTER ms | A/B | C++ lines B -> A | module.so bytes B -> A | rc |")
print("|---|---:|---:|---:|---:|---:|---|")
cr = []  # geomean over sources that compiled in every rep
for n, d in C.items():
    b, a = med([x[1] for x in d["before"]]), med([x[1] for x in d["after"]])
    if all(x[0] == 0 for x in d["before"] + d["after"]): cr.append(a / b)
    print("| `%s` | %.0f | %.0f | %.3f | %d -> %d | %s -> %s | %s |" % (n, b, a, a / b, d["before"][0][3], d["after"][0][3],
          "{:,}".format(d["before"][0][2]), "{:,}".format(d["after"][0][2]), "all 0" if all(x[0] == 0 for x in d["before"] + d["after"]) else "FAIL"))
print("geomean compile A/B (%d sources that compiled): %.3f" % (len(cr), gmean(cr)))
print("\n## Probes")
t, raw, su = probe_table(os.path.join(OUT, "probes.txt"), ["interp_before", "interp_after", "trans_before", "trans_after", "jvm"],
                         [("interp_before", "interp_after"), ("trans_before", "trans_after")])
print(t); print(su); print(); print("\n".join(raw))
