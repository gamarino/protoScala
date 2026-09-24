// EXPECT: an Error, not an Exception
def deep(n: Int): Int = 1 + deep(n + 1)
@main def run(): Unit =
  try
    try println(deep(0))
    catch case e: Exception => println("wrongly caught")
  catch case e: StackOverflowError => println("an Error, not an Exception")
