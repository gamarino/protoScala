// EXPECT: P(1,2) true
object Geometry:
  case class P(x: Int, y: Int)
@main def run(): Unit =
  val p = Geometry.P(1, 2)
  println(p.toString + " " + (p == Geometry.P(1, 2)))
