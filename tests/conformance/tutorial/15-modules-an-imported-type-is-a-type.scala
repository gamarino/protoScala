// EXPECT: Point(1,2) true
// The same name in a `new` and in a type test.
import util.Shapes.{Point}
@main def run(): Unit =
  val p: Any = new Point(1, 2)
  println(p.toString + " " + p.isInstanceOf[Point])
