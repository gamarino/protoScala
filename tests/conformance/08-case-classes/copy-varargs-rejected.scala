// EXPECT-ERROR: value copy is not a member of Poly
case class Poly(coeffs: Int*)
@main def run(): Unit = println(Poly(1, 2, 3).copy())
