// EXPECT: bad
// The other half of point 4.
@main def run(): Unit =
  try throw new IllegalStateException("bad")
  catch case e: Exception => println(e.getMessage)
