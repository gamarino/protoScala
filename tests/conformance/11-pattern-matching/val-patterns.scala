// EXPECT: 7 3 4 1 List(2, 3)
case class Point(x: Int, y: Int)
val (a, b) = (3, 4)
@main def run(): Unit =
  val Point(x, y) = Point(3, 4)
  val h :: t = List(1, 2, 3)
  println((a + b).toString + " " + x + " " + y + " " + h + " " + t)
