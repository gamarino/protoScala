// EXPECT-ERROR: IllegalStateException: nope
// A handler that matches nothing re-raises the exception rather than replacing
// it. This pins protoScala's own report format (D14: `file:line: error: Class:
// message`); scalac prints a JVM stack trace instead.
@main def run(): Unit =
  try
    throw new IllegalStateException("nope")
  catch
    case e: ArithmeticException => println("wrong")
