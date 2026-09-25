// EXPECT: AssertionError: assumption failed: unreachable
// `assume` is `assert` with a different prefix and the same exception type.
// scalac 3.9.0: `java.lang.AssertionError: assumption failed: why`.
@main def run(): Unit =
  try assume(false, "unreachable")
  catch case e: AssertionError => println(e.toString)
