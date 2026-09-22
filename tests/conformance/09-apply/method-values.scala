// EXPECT: 10 12 true
class Scaler(k: Int):
  def scale(x: Int) = x * k

@main def run(): Unit =
  val s = new Scaler(2)
  val f = s.scale
  val g: Int => Int = s.scale
  println(f(5).toString + " " + g(6) + " " + (f(1) == s.scale(1)))
