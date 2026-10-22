import re,statistics,sys
C={}
for l in open(sys.argv[1]):
    n,v,rc,ms,so,cl=re.match(r"(\S+) (\S+) rep\d rc=(\d+) ms=([\d.]+) so_bytes=(\d+) cpp_lines=(\d+)",l).groups()
    C.setdefault(n,{}).setdefault(v,[]).append(float(ms)); assert rc=="0"
for n,d in C.items():
    b,a=statistics.median(d["before"]),statistics.median(d["after"])
    print("| `%s` | %.0f | %.0f | %.3f | %s | %s |"%(n,b,a,a/b,"/".join("%.0f"%x for x in d["before"]),"/".join("%.0f"%x for x in d["after"])))
