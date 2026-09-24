// EXPECT: Range 0 until 5 Range 1 to 5 Range 0 until 10 by 2 empty Range 5 until 5
// scalac 3.9's rendering, including the "empty " prefix; verified against
// tools/scala3-3.9.0 rather than assumed.
@main def run(): Unit =
  println((0 until 5).toString + " " + (1 to 5) + " " + (0 until 10 by 2) + " " + (5 until 5))
