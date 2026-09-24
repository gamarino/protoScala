// EXPECT-ERROR: a Range bound must fit a 54-bit integer
@main def run(): Unit =
  val bound = 9007199254740991L + 1
  println((0 until bound).length)
