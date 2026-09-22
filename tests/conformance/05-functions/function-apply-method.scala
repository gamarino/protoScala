// EXPECT: 49
@main def run(): Unit =
  val sq = (x: Int) => x * x
  println(sq.apply(7))
