// EXPECT: caught finally
@main def run(): Unit =
  try
    throw new RuntimeException("x")
  catch
    case e: RuntimeException => print("caught ")
  finally
    println("finally")
