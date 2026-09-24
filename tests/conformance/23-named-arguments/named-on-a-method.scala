// EXPECT: 30
class Rect(val w: Int, val h: Int):
  def scaled(byW: Int = 1, byH: Int = 1): Int = w * byW * h * byH
@main def run(): Unit =
  println(new Rect(2, 3).scaled(byH = 5))
