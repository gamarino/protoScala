// EXPECT: List(2, 4, 6, 8, 10) List(1, 3, 5) 15 15 1, 2, 3, 4, 5
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.map(_ * 2).toString + " " + xs.filter(_ % 2 == 1) + " " +
    xs.foldLeft(0)(_ + _) + " " + xs.sum + " " + xs.mkString(", "))
