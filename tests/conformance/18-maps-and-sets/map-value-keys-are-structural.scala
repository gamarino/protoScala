// EXPECT: a b c d
// DESIGN §6.1 bullet 2: a freshly built, structurally equal key must hit for
// every value-equality kind. A kind wrongly classified as identity prints MISS
// in its place. Verified against tools/scala3-3.9.0.
case class Pair(a: Int, b: String)
@main def run(): Unit =
  val m = Map[Any, String](Pair(1, "x") -> "a", (1, 2) -> "b", List(1, 2) -> "c", "s" -> "d")
  println(m.getOrElse(Pair(1, "x"), "MISS") + " " + m.getOrElse((1, 2), "MISS") + " " +
    m.getOrElse(List(1, 2), "MISS") + " " + m.getOrElse("s", "MISS"))
