// EXPECT-ERROR: ArithmeticException: / by zero
@main def run(): Unit =
  val zero = 0
  println(10 / zero)
