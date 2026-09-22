// EXPECT: false true D(NaN) true D(-0.0) I(-7) -331522405 1531949307
case class D(v: Double)
case class I(v: Int)
@main def run(): Unit =
  val nan = 0.0 / 0.0
  println((D(nan) == D(nan)).toString + " " + (D(nan).hashCode == D(nan).hashCode) + " " + D(nan) +
    " " + (D(0.0) == D(-0.0)) + " " + D(-0.0) + " " + I(-7) + " " + I(-7).hashCode + " " + D(-2.5).hashCode)
