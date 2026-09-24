// EXPECT: caught
@main def run(): Unit =
  try
    val x: Any = 5
    println(x.asInstanceOf[String].length)
  catch case e: ClassCastException => println("caught")
