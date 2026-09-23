// EXPECT: true
@main def run(): Unit =
  val a = for (x <- List(1, 2); y <- List(10, 20) if x + y != 21) yield x * y
  val b = List(1, 2).flatMap(x => List(10, 20).withFilter(y => x + y != 21).map(y => x * y))
  println(a == b)
