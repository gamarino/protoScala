// EXPECT: 3 2
@main def run(): Unit =
  def unless(cond: Boolean)(body: => Int): Int = if cond then 0 else body
  println(unless(true)({ println("never printed"); 1 }))
  var n = 0
  def twice(body: => Int): Int = body + body
  println(twice({ n = n + 1; n }).toString + " " + n)
