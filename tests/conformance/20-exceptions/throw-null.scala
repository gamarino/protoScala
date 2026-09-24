// EXPECT-ERROR: NullPointerException
@main def run(): Unit =
  val e: RuntimeException = null
  throw e
