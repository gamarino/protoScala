// EXPECT: false false false true
case class P(a: Int)
case class D(v: Double)
class Q(val a: Int)
@main def run(): Unit =
  val q: Any = new Q(1)
  val one: Any = 1
  println((P(1) == q).toString + " " + (new Q(1) == new Q(1)) + " " + (P(1) == one) + " " +
    (D(0.0).hashCode == D(-0.0).hashCode))
