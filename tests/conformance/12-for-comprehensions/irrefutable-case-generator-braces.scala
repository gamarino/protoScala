// EXPECT: List(3, 7)
// D36: `case` on an irrefutable generator pattern emits no `withFilter` step
// (scalac always inserts one). The result is the same as scalac's, which this
// fixture pins; only the number of intermediate traversals differs.
@main def run(): Unit = {
  val ps = List((1, 2), (3, 4))
  val ss = for { case (a, b) <- ps } yield a + b
  println(ss)
}
