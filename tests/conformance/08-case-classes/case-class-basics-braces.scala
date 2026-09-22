// EXPECT: Point(1,2) true false 1 2 Point(1,5) Point(3,2) 1 3
case class Point(x: Int, y: Int) {
  def norm1 = x.abs + y.abs
}

@main def run(): Unit = {
  val p = Point(1, 2)
  val q = Point(1, 2)
  println(p.toString + " " + (p == q) + " " + (p eq q) + " " + p.x + " " + p._2 + " " +
    p.copy(y = 5) + " " + p.copy(3) + " " + (if (p.hashCode == q.hashCode) 1 else 0) + " " + p.norm1)
}
