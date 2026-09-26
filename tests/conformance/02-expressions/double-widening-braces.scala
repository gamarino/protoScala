// EXPECT: 42.0 3.0 1.0 2.0 7.0 4.0 5.0 6.0
// The brace-syntax twin of double-widening.scala.
class P(val x: Double)
case class Q(x: Double)

@main def run(): Unit = {
  val d: Double = 42
  val f: Float = 3
  def g(x: Int): Double = x
  def h(x: Double) = x
  val asc = (7: Double)
  val lam: Double => Double = (y: Double) => y
  println(d.toString + " " + f + " " + g(1) + " " + h(2) + " " + asc + " " +
          new P(4).x + " " + Q(5).x + " " + lam(6))
}
