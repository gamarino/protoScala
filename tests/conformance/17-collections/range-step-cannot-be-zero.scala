// EXPECT-ERROR: a Range step cannot be zero
@main def run(): Unit =
  println((0 until 5 by 0).toList)
