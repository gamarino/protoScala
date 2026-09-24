// EXPECT: 12
// The form neither language has: the selector `Point` names a TYPE, so it works
// in a pattern as well as in a call.
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))
