// EXPECT: 1 5 List(1, 2, 3, 4) List(5, 4, 3, 2, 1) Some(1) None
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.head.toString + " " + xs.last + " " + xs.init + " " + xs.reverse + " " +
    xs.headOption + " " + List().headOption)
