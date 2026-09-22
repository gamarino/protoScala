// EXPECT: before init 42 42
@main def run(): Unit =
  var log = "before"
  lazy val answer = { log = log + " init"; 42 }
  val first = answer
  val second = answer
  println(log + " " + first + " " + second)
