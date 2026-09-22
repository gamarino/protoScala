# factorial_100.py - CPython/protoPython twin of
# benchmarks/comparable/factorial_100.scala and protoClojure's
# benchmarks/factorial-100.clj. Prints 100! (158 digits) on the last line.


def factorial(n):
    acc = 1
    k = 2
    while k <= n:
        acc = acc * k
        k += 1
    return acc


print(factorial(100))
