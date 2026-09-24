# map_build.py - CPython/protoPython twin of benchmarks/comparable/map_build.scala:
# build a 50000-entry dict keyed by str, read every key back, and fold the values.
# Result: 50000 1249975000 50000 (computed with tools/scala3-3.9.0).
#
# Python's dict is mutable and Scala's Map is not, so the twin measures the same
# algorithm rather than the same allocation behaviour; benchmarks/README.md says
# so for every workload whose data structure differs.


def main():
    m = {}
    i = 0
    while i < 50000:
        m["k" + str(i)] = i
        i += 1
    total = 0
    found = 0
    j = 0
    while j < 50000:
        v = m.get("k" + str(j), -1)
        if v >= 0:
            total += v
            found += 1
        j += 1
    print(str(len(m)) + " " + str(total) + " " + str(found))


main()
