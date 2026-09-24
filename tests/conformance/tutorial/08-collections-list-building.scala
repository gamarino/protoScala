// EXPECT: List(1, 2, 3) List(2, 3) List(2, 3, 4) List(2, 3, 9)
@main def run(): Unit =
  val xs = List(2, 3)
  val longer = 1 :: xs
  println(longer.toString + " " + xs + " " + (xs :+ 4) + " " + (xs ++ List(9)))
