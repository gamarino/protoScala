// EXPECT-ERROR: String.split needs a non-empty separator
@main def run(): Unit =
  println("abc".split(""))
