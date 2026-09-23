// EXPECT: Point(3, 4) 7 Point(4, 4)
class Point(val x: Int, var y: Int):
  def sum = x + y
  def moved(dx: Int) = new Point(x + dx, y)
  override def toString = "Point(" + x + ", " + y + ")"

@main def run(): Unit =
  val p = new Point(3, 4)
  println(p.toString + " " + p.sum + " " + p.moved(1))
