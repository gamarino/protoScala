// EXPECT: 3 3 5
@main def run(): Unit =
  println((1 + 2).toString + " " + 1.+(2) + " " + (3 max 5))
