// EXPECT: List(0, 1, 2, 3, 4) List(1, 2, 3, 4, 5) 5 5 true false
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val u = 0 until 5
  val t = 1 to 5
  println(u.toList.toString + " " + t.toList + " " + u.length + " " + t.length + " " +
    u.contains(4) + " " + u.contains(5))
