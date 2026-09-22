// EXPECT: 3 4 7 Point(3, 4) Point(5, 4)
class Point(val x: Int, var y: Int) {
  def sum = x + y
  def moved(dx: Int): Point = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"
}

@main def run(): Unit = {
  val p = new Point(3, 4)
  println(p.x.toString + " " + p.y + " " + p.sum + " " + p + " " + p.moved(2))
}
