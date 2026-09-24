// EXPECT: List(1, 2, 3) Vector(1, 2, 3) Vector(0, 1, 2)
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println(Vector(1, 2, 3).toList.toString + " " + List(1, 2, 3).toVector + " " +
    (0 until 3).toVector)
