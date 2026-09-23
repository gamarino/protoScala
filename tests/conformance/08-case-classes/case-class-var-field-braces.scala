// EXPECT: Counter(5) false false true
case class Counter(var n: Int)
@main def run(): Unit = {
  val c = Counter(1)
  val d = Counter(1)
  val h = c.hashCode
  c.n = 5
  println(c.toString + " " + (c == d) + " " + (c.hashCode == h) + " " + (c == Counter(5)))
}
