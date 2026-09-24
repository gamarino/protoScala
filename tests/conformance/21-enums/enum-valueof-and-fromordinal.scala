// EXPECT: Green Blue caught
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  print(Color.valueOf("Green").toString + " " + Color.fromOrdinal(2) + " ")
  try println(Color.valueOf("Purple"))
  catch case e: IllegalArgumentException => println("caught")
