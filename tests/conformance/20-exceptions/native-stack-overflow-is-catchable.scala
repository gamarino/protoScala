// EXPECT: caught overflow
// Not tail-recursive on purpose: scalac turns a self tail call into a loop,
// which never overflows, so the reference would run forever.
def deep(n: Int): Int = 1 + deep(n + 1)
@main def run(): Unit =
  try println(deep(0))
  catch case e: StackOverflowError => println("caught overflow")
