// EXPECT: 500000500000
// sum_loop.scala - sum of 0..N inclusive, N = 1000000.
// Twin of protoClojure's benchmarks/sum-loop.clj (loop/recur `sum-to`).
// The sum does not fit an Int, so the accumulator is a Long.
// Result: 500000500000.

def sumTo(n: Int): Long =
  var acc = 0L
  var i = 0
  while i <= n do
    acc += i
    i += 1
  acc

@main def benchSumLoop(): Unit =
  println(sumTo(1000000))
