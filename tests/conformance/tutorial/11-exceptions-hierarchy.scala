// EXPECT: true true true false
@main def run(): Unit =
  val e = new ArithmeticException("/ by zero")
  println(e.isInstanceOf[RuntimeException].toString + " " + e.isInstanceOf[Exception] + " " +
    e.isInstanceOf[Throwable] + " " + e.isInstanceOf[Error])
