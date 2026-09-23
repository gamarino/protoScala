// EXPECT: 600000
// attr_lookup.scala - 100000 iterations reading three fields of an object and
// summing them. Twin of protoPython's benchmarks/attr_lookup.py (BENCH_N=100000)
// and protoST's benchmarks/comparable/attr_lookup.st. Result: 600000.

class FastObject(val a: Int, val b: Int, val c: Int)

def runBench(obj: FastObject, n: Int): Int =
  var total = 0
  var i = 0
  while i < n do
    total += obj.a
    total += obj.b
    total += obj.c
    i += 1
  total

@main def benchAttrLookup(): Unit =
  println(runBench(new FastObject(1, 2, 3), 100000))
