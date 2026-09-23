// EXPECT: List(0, 4, 16)
@main def run(): Unit =
  val xs = List(0, 1, 2, 3, 4)
  println(for (x <- xs if x % 2 == 0) yield x * x)
