// EXPECT: not an Exception
// An Error is not an Exception: `case e: Exception` must not catch it.
// Not tail-recursive on purpose (see native-stack-overflow-is-catchable).
def deep(n: Int): Int = 1 + deep(n + 1)
@main def run(): Unit =
  try
    try println(deep(0))
    catch case e: Exception => println("wrongly caught as Exception")
  catch case e: Error => println("not an Exception")
