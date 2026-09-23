// EXPECT: Point(1,2) true Point(1,5) 3
case class Point(x: Int, y: Int)

@main def run(): Unit =
  val p = Point(1, 2)
  println(p.toString + " " + (p == Point(1, 2)) + " " + p.copy(y = 5) + " " + (p.x + p.y))
