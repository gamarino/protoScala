// EXPECT: List(1, 3)
@main def run(): Unit =
  val xs = for (x <- List(1, 2, 3); y <- (if x % 2 == 1 then Some(x) else None)) yield y
  println(xs)
