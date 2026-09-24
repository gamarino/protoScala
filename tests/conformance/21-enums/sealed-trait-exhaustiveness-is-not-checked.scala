// EXPECT-ERROR: MatchError
// D4: types are erased, so a non-exhaustive match compiles and fails at run time.
// scalac rejects this at compile time.
sealed trait Shape
case class Circle(r: Int) extends Shape
case class Square(s: Int) extends Shape
@main def run(): Unit =
  val x: Shape = Square(2)
  println(x match { case Circle(r) => r })
