// EXPECT: 42.0 3.0 1.0 2.0 7.0 4.0 5.0 6.0
// Scala widens an integer to the expected floating-point type. protoScala has no
// expected type (D4), but the declared type *is* written at every one of these
// sites, which is enough: a `val`, a `def` result, a `def` parameter, an explicit
// ascription, a constructor parameter, a case-class parameter and a lambda
// parameter. `val d: Double = 42` printed `42` before Track S. scalac 3.9 prints
// `42.0 3.0 1.0 2.0 7.0 4.0 5.0 6.0`.
class P(val x: Double)
case class Q(x: Double)

@main def run(): Unit =
  val d: Double = 42
  val f: Float = 3
  def g(x: Int): Double = x
  def h(x: Double) = x
  val asc = (7: Double)
  val lam: Double => Double = (y: Double) => y
  println(d.toString + " " + f + " " + g(1) + " " + h(2) + " " + asc + " " +
          new P(4).x + " " + Q(5).x + " " + lam(6))
