// EXPECT: <shape 2.0> Sq(3.0)
trait Named:
  def size: Double
  override def toString = "<shape " + size + ">"
case class Circle(size: Double) extends Named
case class Sq(size: Double)

@main def run(): Unit = println(Circle(2.0).toString + " " + Sq(3.0))
