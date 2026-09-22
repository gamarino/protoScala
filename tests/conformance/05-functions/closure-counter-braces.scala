// EXPECT: 3
def makeCounter(): () => Int = {
  var count = 0
  () => { count += 1; count }
}
@main def run(): Unit = {
  val next = makeCounter()
  next()
  next()
  println(next())
}
