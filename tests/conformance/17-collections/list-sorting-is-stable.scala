// EXPECT: List((1,a), (1,b), (2,c))
// The merge is stable, as Scala's sort is: an equal pair keeps its input order.
// Verified against tools/scala3-3.9.0.
case class P(k: Int, s: String)
@main def run(): Unit =
  val xs = List(P(1, "a"), P(2, "c"), P(1, "b"))
  println(xs.sortBy(_.k).map(p => (p.k, p.s)))
