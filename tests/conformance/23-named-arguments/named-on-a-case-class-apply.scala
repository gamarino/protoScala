// EXPECT: P(1,9) P(1,2)
case class P(x: Int, y: Int = 9)
@main def run(): Unit =
  println(P(x = 1).toString + " " + P(1).copy(y = 2))
