// EXPECT: 1
// Point 3: match destructures while it tests.
case class P(x: Int, y: Int)
@main def run(): Unit =
  val p = P(1, 2)
  println(p.copy(y = 9))
  println(p match { case P(x, _) => x })
