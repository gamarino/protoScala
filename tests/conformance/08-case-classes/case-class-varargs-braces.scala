// EXPECT: Poly(List(1, 2, 3)) 3
case class Poly(coeffs: Int*)
@main def run(): Unit = {
  val p = Poly(1, 2, 3)
  println(p.toString + " " + p.coeffs.length)
}
