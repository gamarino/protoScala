// EXPECT: caught
// A member that does not exist is a NoSuchMethodError, an Error rather than an
// Exception, so `case e: Throwable` is what catches it.
@main def run(): Unit =
  try
    val x: Any = 5
    println(x.asInstanceOf[List[Int]].head)
  catch case e: Throwable => println("caught")
