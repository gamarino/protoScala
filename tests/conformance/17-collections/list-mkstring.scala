// EXPECT: 123 1-2-3 [1, 2, 3]
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2, 3)
  println(xs.mkString + " " + xs.mkString("-") + " " + xs.mkString("[", ", ", "]"))
