// EXPECT: 12
// README, "protoScala in 10 minutes -- for Scala programmers" point 5 and "What
// polyglot interop does and does not do today". `Point` is used as a TYPE in an
// extractor pattern, which is what compile-time import resolution buys.
import util.Shapes.{Point, area}
@main def run(): Unit =
  Point(3, 4) match
    case Point(x, y) => println(area(Point(x, y)))
