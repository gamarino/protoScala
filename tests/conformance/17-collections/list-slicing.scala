// EXPECT: List(1, 2) List(3, 4, 5) List(1, 2) List(3, 4, 5) (List(1, 2),List(3, 4, 5))
// splitAt prints with no space after the comma, which is scalac's Tuple2
// rendering; verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.take(2).toString + " " + xs.drop(2) + " " + xs.takeWhile(_ < 3) + " " +
    xs.dropWhile(_ < 3) + " " + xs.splitAt(2))
