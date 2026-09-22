// EXPECT: 7 origin on-x (1,2)
case class Point(x: Int, y: Int)
case class Line(a: Point, b: Point)
def describe(v: Any): String = v match
  case Line(Point(x1, _), Point(x2, _)) => (x1 + x2).toString
  case Point(0, 0) => "origin"
  case Point(_, 0) => "on-x"
  case Point(x, y) => "(" + x + "," + y + ")"

@main def run(): Unit =
  println(List(Line(Point(3, 0), Point(4, 9)), Point(0, 0), Point(5, 0), Point(1, 2)).map(describe).mkString(" "))
