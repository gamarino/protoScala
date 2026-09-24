// EXPECT: hit hit 1
// The brace-syntax twin of map-case-class-keys.scala.
case class K(a: Int, b: String)
@main def run(): Unit = {
  val m = Map(K(1, "x") -> "hit")
  println(m(K(1, "x")) + " " + m.getOrElse(K(1, "x"), "miss") + " " +
    (m + (K(1, "x") -> "hit")).size)
}
