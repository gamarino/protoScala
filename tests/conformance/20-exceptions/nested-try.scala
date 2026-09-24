// EXPECT: inner outer
// An exception raised inside a handler body is NOT caught by the same try: the
// Catch handler-table entry covers the try body only.
@main def run(): Unit =
  try
    try
      throw new RuntimeException("a")
    catch
      case e: RuntimeException =>
        print("inner ")
        throw new IllegalStateException("b")
  catch
    case e: IllegalStateException => println("outer")
