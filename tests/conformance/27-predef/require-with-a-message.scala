// EXPECT: IllegalArgumentException: requirement failed: n must be positive
// scalac 3.9.0: `java.lang.IllegalArgumentException: requirement failed: why`.
def half(n: Int): Int =
  require(n > 0, "n must be positive")
  n / 2
@main def run(): Unit =
  try println(half(-2))
  catch case e: IllegalArgumentException => println(e.toString)
