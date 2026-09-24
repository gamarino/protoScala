// EXPECT: List(a, b, c) ABC 3 a c
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s = "abc"
  println(s.toList.toString + " " + s.map(_.toUpper).mkString + " " + s.length + " " +
    s.head + " " + s.last)
