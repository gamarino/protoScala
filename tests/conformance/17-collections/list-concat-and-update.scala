// EXPECT: List(1, 2, 3, 4) List(1, 2, 3) List(0, 1, 2) List(1, 9, 3)
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2)
  println((xs ++ List(3, 4)).toString + " " + (xs :+ 3) + " " + (0 +: xs) + " " +
    List(1, 2, 3).updated(1, 9))
