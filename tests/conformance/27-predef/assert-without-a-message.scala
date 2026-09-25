// EXPECT: AssertionError: assertion failed
// `assert(false)` raises Scala's AssertionError with Scala's exact text, and the
// text is the bare prefix: there is no ": " and nothing after it. Verified
// against scalac 3.9.0, which prints
//   java.lang.AssertionError: assertion failed
// (protoScala has no `java.lang` namespace, D8).
@main def run(): Unit =
  try assert(1 + 1 == 3)
  catch case e: AssertionError => println(e.toString)
