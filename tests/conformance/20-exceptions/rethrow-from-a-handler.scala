// EXPECT: seen rethrown
// A handler may re-raise by throwing the bound value again; the outer try sees
// the same instance.
@main def run(): Unit =
  val original = new IllegalStateException("x")
  try
    try
      throw original
    catch
      case e: IllegalStateException =>
        print("seen ")
        throw e
  catch
    case e: IllegalStateException => println(if e eq original then "rethrown" else "copied")
