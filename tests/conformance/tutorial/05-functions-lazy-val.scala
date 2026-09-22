// EXPECT: start computed 42 42
@main def run(): Unit =
  var log = "start"
  lazy val expensive = { log = log + " computed"; 42 }
  val a = expensive
  val b = expensive
  println(log + " " + a + " " + b)
