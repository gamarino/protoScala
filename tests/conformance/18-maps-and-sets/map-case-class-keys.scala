// EXPECT: hit hit 1
// DESIGN §6.1 bullet 2: a case class is a value key, so a freshly built equal
// key hits. Verified against tools/scala3-3.9.0.
case class K(a: Int, b: String)
@main def run(): Unit =
  val m = Map(K(1, "x") -> "hit")
  println(m(K(1, "x")) + " " + m.getOrElse(K(1, "x"), "miss") + " " +
    (m + (K(1, "x") -> "hit")).size)
