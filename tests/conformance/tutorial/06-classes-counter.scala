// EXPECT: 2 12
class Counter:
  var count = 0
  def inc(): Unit = count += 1

@main def run(): Unit =
  val c = new Counter
  c.inc()
  c.inc()
  val two = c.count
  c.count += 10
  println(two.toString + " " + c.count)
