// EXPECT: 100000
// range_iterate.scala - iterate N = 100000 times, counting.
// Twin of protoPython's benchmarks/range_iterate.py (run with BENCH_N=100000)
// and protoST's comparable/range_iterate.st. Phase 1 has no `for` and no
// Range, so the iteration is a `while` loop over an index; it becomes
// `for _ <- 0 until n do` once for-comprehensions land (Phase 2/3).
// Result: 100000.

def countTo(n: Int): Int =
  var count = 0
  var i = 0
  while i < n do
    count += 1
    i += 1
  count

@main def benchRangeIterate(): Unit =
  println(countTo(100000))
