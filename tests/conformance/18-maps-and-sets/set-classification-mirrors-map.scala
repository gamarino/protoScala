// EXPECT: 2 1 true false
// §6.1's third bullet: Set uses the same scheme with the element as the entry,
// so a Plain (identity) element duplicates and a Pair (value) element does not.
// Verified against tools/scala3-3.9.0.
class Plain(val n: Int)
case class Pair(a: Int, b: String)
@main def run(): Unit =
  val p = Set(new Plain(1), new Plain(1))
  val q = Set(Pair(1, "x"), Pair(1, "x"))
  println(p.size.toString + " " + q.size + " " + q.contains(Pair(1, "x")) + " " +
    p.contains(new Plain(1)))
