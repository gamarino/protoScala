// EXPECT: List(10, 20, 40)
@main def run(): Unit =
  val r =
    for
      x <- List(1, 2)
      y <- List(10, 20)
      if x + y != 21
    yield x * y
  println(r)
