// EXPECT: escaped past the Exception handler
// AssertionError extends Error, not Exception, in Scala -- so `catch case e:
// Exception` does NOT swallow a failed assertion. That is the whole point of
// having an assertion rather than a thrown Exception, and it is the part a
// handler written around library code depends on.
@main def run(): Unit =
  try
    try assert(false)
    catch case e: Exception => println("WRONG: caught as an Exception")
  catch case e: Error => println("escaped past the Exception handler")
