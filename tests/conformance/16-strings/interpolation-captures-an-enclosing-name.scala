// EXPECT: v=1 v=2
// A name read inside a hole is a capture like any other: the closure must see
// the box, not a stale copy. Without the capture walk descending into the
// holes this prints `v=1 v=1`.
@main def run(): Unit =
  var v = 1
  val show = () => s"v=$v"
  val first = show()
  v = 2
  println(first + " " + show())
