// EXPECT: List(1, 2, 3) List(0, 1, 2, 3) 3 List(2, 4, 6) List(1, 3) List(2, 3) true
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val ys = 0 :: xs
  println(xs.toString + " " + ys + " " + xs.length + " " + xs.map(_ * 2) + " " + xs.filter(_ != 2) + " " +
    xs.tail + " " + (xs == List(1, 2, 3)))
