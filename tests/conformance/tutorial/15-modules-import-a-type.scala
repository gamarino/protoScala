// EXPECT: 12
// A selector may name a TYPE. `Point` is then usable as a pattern, which is only
// possible because the import was resolved while this file was compiled.
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))
