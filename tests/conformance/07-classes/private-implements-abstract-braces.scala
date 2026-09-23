// EXPECT-ERROR: weaker access privileges
trait Shape {
  def area: Double
}
class Circle extends Shape {
  private def area = 1.0
}
@main def run(): Unit = println(1)
