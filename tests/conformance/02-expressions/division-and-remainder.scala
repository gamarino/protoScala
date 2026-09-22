// EXPECT: -3 -1 -3 1 3.5 1.5
@main def run(): Unit =
  println((-7 / 2).toString + " " + (-7 % 2) + " " + (7 / -2) + " " + (7 % -2) + " " + 7.0 / 2 + " " + 5.5 % 2)
