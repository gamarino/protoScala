// EXPECT: List(0, 2, 4, 6, 8) List(10, 7, 4, 1) List()
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println((0 until 10 by 2).toList.toString + " " + (10 to 1 by -3).toList + " " +
    (5 until 5).toList)
