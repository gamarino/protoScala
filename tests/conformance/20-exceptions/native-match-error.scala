// EXPECT: caught
@main def run(): Unit =
  try
    val x: Any = 5
    x match
      case s: String => println(s)
  catch case e: MatchError => println("caught")
