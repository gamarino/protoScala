// EXPECT: built 0 messages, then 1
// The message is by-name, as in Scala: an assertion that holds never builds it.
// A strict parameter would make `assert(ok, expensiveReport())` pay for the
// report on every call.
var built = 0
def report(): String = { built = built + 1; "bad state" }
@main def run(): Unit =
  assert(true, report())
  val quiet = built
  try assert(false, report())
  catch case e: AssertionError => ()
  println("built " + quiet + " messages, then " + built)
