// EXPECT: 12 20
@main def run(): Unit =
  println((2 + 3 * 4 - 10 / 2 % 3).toString + " " + (2 + 3) * 4)
