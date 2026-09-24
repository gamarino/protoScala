// EXPECT: 15 15 120 -15 1 5 15
// foldLeft(0)(_ - _) is ((((0-1)-2)-3)-4)-5 = -15, computed by scalac, not by
// hand. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.foldLeft(0)(_ + _).toString + " " + xs.sum + " " + xs.product + " " +
    xs.foldLeft(0)(_ - _) + " " + xs.min + " " + xs.max + " " + xs.reduce(_ + _))
