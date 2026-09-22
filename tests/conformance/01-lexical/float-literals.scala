// EXPECT: 1.5 1000.0 0.0025 0.5 3.0 1.0E21 1.0E-5
// 3f is a Float in Scala, a Double here (D2); both print 3.0.
@main def run(): Unit =
  println(1.5.toString + " " + 1e3 + " " + 2.5e-3 + " " + .5 + " " + 3f + " " + 1e21 + " " + 0.00001)
