// EXPECT: List(4, 6) List((1,1), (2,4), (3,9))
@main def run(): Unit = {
  val xs = List(1, 2, 3)
  val a = for { x <- xs; y = x * 2 if y > 2 } yield y
  val b = for { x <- xs; sq = x * x } yield (x, sq)
  println(a.toString + " " + b)
}
