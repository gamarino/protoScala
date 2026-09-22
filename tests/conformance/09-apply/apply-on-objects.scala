// EXPECT: 9 16 5
object Square:
  def apply(n: Int) = n * n
class Poly(val a: Int, val b: Int):
  def apply(x: Int) = a * x + b

@main def run(): Unit =
  val p = new Poly(2, 3)
  val f = Square
  println(Square(3).toString + " " + f(4) + " " + p(1))
