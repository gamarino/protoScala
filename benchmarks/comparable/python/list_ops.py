# list_ops.py - CPython/protoPython twin of benchmarks/comparable/list_ops.scala:
# build a 100000-element list, then map / filter / fold it.
# Result: 9999900000 100000 50000 (computed with tools/scala3-3.9.0).


def build(n):
    xs = []
    i = n
    while i > 0:
        i -= 1
        xs.insert(0, i)
    return xs


def main():
    xs = build(100000)
    doubled = [x * 2 for x in xs]
    evens = [x for x in xs if x % 2 == 0]
    total = 0
    for x in doubled:
        total += x
    print(str(total) + " " + str(len(xs)) + " " + str(len(evens)))


main()
