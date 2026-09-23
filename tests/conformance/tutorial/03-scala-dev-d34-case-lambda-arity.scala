// EXPECT-ERROR: wrong number of arguments
@main def run(): Unit =
  val add: (Int, Int) => Int = { case (a, b) => a + b }
  println(add(1, 2))
