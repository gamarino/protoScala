# sum_loop.py - CPython/protoPython twin of benchmarks/comparable/sum_loop.scala
# and protoClojure's benchmarks/sum-loop.clj: sum of 0..N inclusive, N = 1000000.
# Result: 500000500000.


def sum_to(n):
    acc = 0
    i = 0
    while i <= n:
        acc += i
        i += 1
    return acc


print(sum_to(1000000))
