// EXPECT: caught index
@main def run(): Unit =
  try println(List(1, 2, 3)(9))
  catch case e: IndexOutOfBoundsException => println("caught index")
