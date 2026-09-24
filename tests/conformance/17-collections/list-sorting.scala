// EXPECT: List(1, 2, 3, 5) List(5, 3, 2, 1) List(a, bb, ccc) List(ccc, bb, a)
// D62: `sorted` uses the runtime's own ordering, since there is no Ordering
// (D3). For numbers and strings the answer is Scala's; verified against
// tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(3, 1, 5, 2)
  val ws = List("bb", "ccc", "a")
  println(xs.sorted.toString + " " + xs.sortWith(_ > _) + " " + ws.sortBy(_.length) + " " +
    ws.sortWith(_.length > _.length))
