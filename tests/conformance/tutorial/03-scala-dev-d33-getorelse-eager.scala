// EXPECT: 1 evaluated
var log = ""
def fallback(): Int =
  log = "evaluated"
  0

@main def run(): Unit =
  val x = Some(1).getOrElse(fallback())
  println(x.toString + " " + log)
