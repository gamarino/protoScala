case class Point(x: Int, y: Int)

def describe(p: Point): String = p match
  case Point(0, 0) => "origin"
  case Point(x, y) => s"($x, $y)"

val (a, b) = (1, 2)
println(describe(Point(a, b)))
