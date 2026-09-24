// EXPECT: caught boom
@main def run(): Unit = {
  try {
    throw new RuntimeException("boom")
  } catch {
    case e: RuntimeException => println("caught " + e.getMessage)
  }
}
