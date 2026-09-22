// EXPECT-ERROR: MatchError: Point(1,2) (of class Point)
case class Point(x: Int, y: Int)
@main def run(): Unit =
  Point(1, 2) match
    case Point(0, _) => println("zero")
