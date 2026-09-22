# tak.py - CPython/protoPython twin of benchmarks/comparable/tak.scala and
# protoClojure's benchmarks/tak.clj. Prints the result on the last line.
# Result: 7.
import sys

sys.setrecursionlimit(10000)


def tak(x, y, z):
    if not (y < x):
        return z
    return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))


print(tak(18, 12, 6))
