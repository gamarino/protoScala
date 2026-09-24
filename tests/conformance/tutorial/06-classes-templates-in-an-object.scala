// EXPECT: P(1,2) 5
object Geometry:
  case class P(x: Int, y: Int)
  object Origin:
    val distance: Int = 5
@main def run(): Unit =
  println(Geometry.P(1, 2).toString + " " + Geometry.Origin.distance)
