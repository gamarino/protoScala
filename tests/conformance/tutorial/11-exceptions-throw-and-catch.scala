// EXPECT: caught: the file is missing
@main def run(): Unit =
  try
    throw new IllegalStateException("the file is missing")
  catch
    case e: IllegalStateException => println("caught: " + e.getMessage)
