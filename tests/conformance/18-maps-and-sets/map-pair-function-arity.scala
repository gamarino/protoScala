// EXPECT: List(1, 2) List((1,2), (2,4))
// The same Map traversed with a one-argument function over the pair and with a
// two-argument function over (k, v): protoScala has no static types, so the
// function's own arity decides which it gets.
@main def run(): Unit =
  val m = Map(1 -> 1, 2 -> 2)
  println(m.map(p => p._1).toList.sorted.toString + " " +
    m.map((k, v) => (k, v * 2)).toList.sortBy(_._1))
