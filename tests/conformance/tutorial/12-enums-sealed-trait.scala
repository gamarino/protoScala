// EXPECT: 12.56 4.0
// The other spelling of the same idea: a sealed trait with case classes. Use it
// when the cases need their own type parameters or their own parents; use `enum`
// when they are just the alternatives of one type.
sealed trait Shape
case class Circle(r: Double) extends Shape
case class Square(side: Double) extends Shape
def area(s: Shape): Double = s match
  case Circle(r)    => 3.14 * r * r
  case Square(side) => side * side
@main def run(): Unit =
  println(area(Circle(2.0)).toString + " " + area(Square(2.0)))
