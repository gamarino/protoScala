// EXPECT: 4999950000
// int_sum_loop.scala - sum of 0 until N, N = 100000.
// Twin of protoPython's benchmarks/int_sum_loop.py (run with BENCH_N=100000:
// `for i in range(N): s += i`) and protoST's comparable/int_sum_loop.st.
// The sum does not fit an Int, so the accumulator is a Long (on protoScala
// integers promote automatically, D1; on the JVM a 32-bit Int would overflow).
// A `while` loop: `for` over a Range arrives in Phase 2/3.
// Result: 4999950000.

def sumBelow(n: Int): Long =
  var s = 0L
  var i = 0
  while i < n do
    s += i
    i += 1
  s

@main def benchIntSumLoop(): Unit =
  println(sumBelow(100000))
