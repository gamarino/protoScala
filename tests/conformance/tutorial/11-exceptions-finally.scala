// EXPECT: opened closed caught
@main def run(): Unit =
  try
    print("opened ")
    try
      throw new RuntimeException("boom")
    finally
      print("closed ")
  catch
    case e: RuntimeException => println("caught")
