// EXPECT: Vector(1, 2, 3) Vector(0, 1, 2) Vector(1, 9) Vector(1, 2, 3, 4)
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val v = Vector(1, 2)
  println((v :+ 3).toString + " " + (0 +: v) + " " + v.updated(1, 9) + " " + (v ++ Vector(3, 4)))
