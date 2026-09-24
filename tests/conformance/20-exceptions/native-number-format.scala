// EXPECT: caught format
@main def run(): Unit =
  try println("abc".toInt)
  catch case e: NumberFormatException => println("caught format")
