// EXPECT: 2 miss a
// DESIGN §6.1 bullet 1: a class with the DEFAULT equals is an identity key, so
// two structurally identical instances are two different keys. If the
// classification wrongly sent this key down the value path, `size` would print
// 1 and the third field would print `a` instead of `miss` -- a LOUD failure, not
// a quiet one (plan A0-5). Verified against tools/scala3-3.9.0.
class Plain(val n: Int)
@main def run(): Unit =
  val k1 = new Plain(1)
  val k2 = new Plain(1)
  val m = Map(k1 -> "a", k2 -> "b")
  println(m.size.toString + " " + m.getOrElse(new Plain(1), "miss") + " " + m(k1))
