// EXPECT: 1
@main def run(): Unit =
  println(try 1 finally 2)
