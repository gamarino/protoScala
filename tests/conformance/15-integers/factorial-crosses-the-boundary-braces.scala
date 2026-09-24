// EXPECT: 2432902008176640000 51090942171709440000
// The brace-syntax twin of factorial-crosses-the-boundary.scala.
@main def run(): Unit = {
  def fact(n: Int): Int = { if (n <= 1) 1 else n * fact(n - 1) }
  println(fact(20).toString + " " + fact(21))
}
