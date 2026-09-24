// EXPECT: 2 miss 1 hit
// The same program twice, differing only by `override def equals`/`hashCode`.
// Overriding moves the key from §6.1's bullet 1 to bullet 2, so the second half
// must find a freshly built equal key where the first half must not. A
// classification that ignored the override would print `1 hit` twice; one that
// ignored the default would print `2 miss` twice. Either way a number changes.
// Verified against tools/scala3-3.9.0.
class Plain(val n: Int)
class Structural(val n: Int):
  override def equals(o: Any): Boolean =
    o.isInstanceOf[Structural] && o.asInstanceOf[Structural].n == n
  override def hashCode: Int = n
@main def run(): Unit =
  val p = Map(new Plain(1) -> "x", new Plain(1) -> "y")
  val s = Map(new Structural(1) -> "x", new Structural(1) -> "y")
  println(p.size.toString + " " + p.getOrElse(new Plain(1), "miss") + " " +
    s.size + " " + (if s.getOrElse(new Structural(1), "miss") == "miss" then "miss" else "hit"))
