// EXPECT: 832040
// fib30.scala - recursive fib(30).
// Twin of protoClojure's benchmarks/fib.clj (fib(30); the protoPython-suite
// row `fib` uses fib(25)).
// Result: 832040.

def fib(n: Int): Int =
  if n < 2 then n
  else fib(n - 1) + fib(n - 2)

@main def benchFib30(): Unit =
  println(fib(30))
