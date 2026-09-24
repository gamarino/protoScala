// The module chapter 15 imports types from: `Point` is a value (its companion's
// `apply`) and a type (a pattern, a `new`, a type test) in the importing file.
case class Point(x: Int, y: Int)
def area(p: Point): Int = p.x * p.y
