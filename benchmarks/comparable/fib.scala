// EXPECT: 75025
// fib.scala - recursive fib(25).
// Twin of protoPython's benchmarks/call_recursion.py (fib(25)) and protoST's
// comparable/fib.st.
// Result: 75025.

def fib(n: Int): Int =
  if n <= 1 then n
  else fib(n - 1) + fib(n - 2)

@main def benchFib(): Unit =
  println(fib(25))
