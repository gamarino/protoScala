// EXPECT: List(2, 4, 6) List(10, 20, 40)
@main def run(): Unit =
  val xs = List(1, 2, 3)
  val doubled = for x <- xs yield x * 2
  val products =
    for
      x <- List(1, 2)
      y <- List(10, 20)
      if x + y != 21
    yield x * y
  println(doubled.toString + " " + products)
