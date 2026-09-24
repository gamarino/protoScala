// EXPECT: replaced
// A finally body that itself throws REPLACES the in-flight exception. Scala does
// the same and warns about it; protoScala cannot warn (D4).
@main def run(): Unit =
  try
    try
      throw new RuntimeException("original")
    finally
      throw new IllegalStateException("replacement")
  catch
    case e: IllegalStateException => println("replaced")
