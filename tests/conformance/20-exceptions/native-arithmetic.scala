// EXPECT: caught / by zero
@main def run(): Unit =
  try println(1 / 0)
  catch case e: ArithmeticException => println("caught " + e.getMessage)
