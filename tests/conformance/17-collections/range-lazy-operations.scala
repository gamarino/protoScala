// EXPECT: true false 5 0 999999999
// exists short-circuits and apply/length/head/last are O(1) arithmetic, so none
// of these materialises a billion elements.
@main def run(): Unit =
  val r = 0 until 1000000000
  println(r.exists(_ == 5).toString + " " + r.contains(-1) + " " + r(5) + " " + r.head + " " +
    r.last)
