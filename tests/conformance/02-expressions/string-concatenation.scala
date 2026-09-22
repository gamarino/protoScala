// EXPECT: a12 3a xtrue cd
@main def run(): Unit =
  println(("a" + 1 + 2) + " " + (1 + 2 + "a") + " " + ("x" + true) + " " + ("c" + 'd'))
