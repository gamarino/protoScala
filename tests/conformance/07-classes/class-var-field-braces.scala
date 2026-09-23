// EXPECT: 3 13
class Counter {
  var count = 0
  def inc(): Unit = count += 1
}

@main def run(): Unit = {
  val c = new Counter
  c.inc(); c.inc(); c.inc()
  val first = c.count
  c.count = 10
  c.count += 3
  println(first.toString + " " + c.count)
}
