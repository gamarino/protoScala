// EXPECT: true true true true true false
@main def run(): Unit =
  val nan = 0.0 / 0.0
  println((1 == 1.0).toString + " " + ("ab" == "a" + "b") + " " + (1 != 2) + " " +
    ('a' == 97) + " " + (null == null) + " " + (nan == nan))
