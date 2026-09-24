// EXPECT: 2432902008176640000 51090942171709440000
// fact(21) is outside a 64-bit integer, so the recursion crosses the promotion
// boundary in the middle of a multiply chain.
@main def run(): Unit =
  def fact(n: Int): Int = if n <= 1 then 1 else n * fact(n - 1)
  println(fact(20).toString + " " + fact(21))
