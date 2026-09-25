// EXPECT: assertion failed: null
// A message of `null` is still a message: Scala's string concatenation turns it
// into "null", so scalac 3.9.0 prints `assertion failed: null`. This is why the
// "no message given" default cannot be `null` -- it has to be a value no caller
// can write.
@main def run(): Unit =
  try assert(false, null)
  catch case e: AssertionError => println(e.getMessage)
