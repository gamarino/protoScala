// EXPECT: () ()
def log(msg: String): Unit =
  println(msg)
  msg.length

@main def run(): Unit =
  val result = log("side effect")
  val maybe = if result == () then 42
  println(result.toString + " " + maybe)
