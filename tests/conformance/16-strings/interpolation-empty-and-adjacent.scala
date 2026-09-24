// EXPECT: 12 |1| ||
// Adjacent holes emit no empty literal pieces; s"||" has no hole at all.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val a = 1
  val b = 2
  println(s"$a$b" + " " + s"|$a|" + " " + s"||")
