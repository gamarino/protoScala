// EXPECT: caught None.get
@main def run(): Unit =
  try println(None.get)
  catch case e: NoSuchElementException => println("caught " + e.getMessage)
