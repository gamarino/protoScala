// EXPECT: true true true true false
@main def run(): Unit =
  val e = new ArithmeticException("x")
  println(e.isInstanceOf[ArithmeticException].toString + " " + e.isInstanceOf[RuntimeException] +
    " " + e.isInstanceOf[Exception] + " " + e.isInstanceOf[Throwable] + " " +
    e.isInstanceOf[Error])
