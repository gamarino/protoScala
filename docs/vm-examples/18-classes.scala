trait Shape:
  def area: Double
  def describe: String = s"shape with area $area"

class Rect(val w: Double, val h: Double) extends Shape:
  def area: Double = w * h

class Square(side: Double) extends Rect(side, side):
  override def describe: String = "square, " + super.describe

object Unit1:
  val one = 1

println(new Square(2.0).describe)
println(Unit1.one)
