// EXPECT: IllegalArgumentException: requirement failed
// A failed `require` blames the caller, so it is an IllegalArgumentException and
// not an AssertionError. scalac 3.9.0 prints
// `java.lang.IllegalArgumentException: requirement failed`.
@main def run(): Unit =
  try require(false)
  catch case e: IllegalArgumentException => println(e.toString)
