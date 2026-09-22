// EXPECT: 6765
def fib(n: Int): Int = {
  if (n < 2) n
  else fib(n - 1) + fib(n - 2)
}
@main def run(): Unit = { println(fib(20)) }
