// EXPECT-ERROR: IllegalStateException: nope
// The cleanup still runs when no clause matches: the Finally handler entry
// covers the catch cascade too, so the RETHROW a non-matching cascade emits is
// inside its range. The cleanup's output precedes the error report.
@main def run(): Unit =
  try
    throw new IllegalStateException("nope")
  catch
    case e: ArithmeticException => println("wrong")
  finally
    println("cleanup ran")
