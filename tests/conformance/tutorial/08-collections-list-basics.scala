// EXPECT: 1 List(2, 3, 4, 5) 5 false 3
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.head.toString + " " + xs.tail + " " + xs.length + " " + xs.isEmpty + " " + xs(2))
