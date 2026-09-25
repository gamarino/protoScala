// EXPECT: AssertionError: assertion failed: 1 + 1 is not 3
// With a message, Scala joins it to the prefix with ": ". scalac 3.9.0 prints
// `java.lang.AssertionError: assertion failed: why` for `assert(false, "why")`.
@main def run(): Unit =
  try assert(1 + 1 == 3, "1 + 1 is not 3")
  catch case e: AssertionError => println(e.toString)
