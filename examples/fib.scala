// Recursive Fibonacci. Run: protoscala examples/fib.scala [n]
def fib(n: Int): Int =
  if n < 2 then n
  else fib(n - 1) + fib(n - 2)

@main def run(args: String*): Unit =
  val n = if args.isEmpty then 25 else args(0).toInt
  println("fib(" + n + ") = " + fib(n))
