// EXPECT: finally caught
@main def run(): Unit = {
  try {
    try {
      throw new RuntimeException("x")
    } finally {
      print("finally ")
    }
  } catch {
    case e: RuntimeException => println("caught")
  }
}
