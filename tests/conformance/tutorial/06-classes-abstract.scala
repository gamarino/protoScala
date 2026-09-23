// EXPECT: circle 12.56 square 4.0
trait Shape:
  def area: Double
  def name: String
  def describe = name + " " + area

class Circle(r: Double) extends Shape:
  def area = 3.14 * r * r
  def name = "circle"

class Square(side: Double) extends Shape:
  def area = side * side
  def name = "square"

@main def run(): Unit =
  println(new Circle(2.0).describe + " " + new Square(2.0).describe)
