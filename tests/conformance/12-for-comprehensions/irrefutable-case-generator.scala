// EXPECT: List(3, 7)
// `case` on a generator inserts a `withFilter` step, as scalac does. Every
// element here is a pair, so nothing is discarded and the values are scalac's,
// which this fixture pins.
@main def run(): Unit =
  val ps = List((1, 2), (3, 4))
  val ss = for (case (a, b) <- ps) yield a + b
  println(ss)
