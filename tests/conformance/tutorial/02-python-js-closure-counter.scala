// EXPECT: 1 2 3
def makeCounter(): () => Int =
  var count = 0
  () =>
    count += 1
    count
@main def run(): Unit =
  val next = makeCounter()
  val a = next()
  val b = next()
  println(a.toString + " " + b + " " + next())
