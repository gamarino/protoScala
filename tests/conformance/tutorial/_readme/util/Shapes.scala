// The module the README's "protoScala in 10 minutes" section and its interop
// section both show. A helper: it lives under a `_`-prefixed directory, which
// the runner does not walk into.
case class Point(x: Int, y: Int)
def area(p: Point): Int = p.x * p.y
