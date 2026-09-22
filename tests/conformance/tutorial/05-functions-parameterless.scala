// EXPECT: 1 2
@main def run(): Unit =
  var n = 0
  def next = { n += 1; n }
  val first = next
  println(first.toString + " " + next)
