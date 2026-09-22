// EXPECT: f1 x1 f2 x2 f3 x3
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
@main def run(): Unit =
  for (x <- List(1, 2, 3) if { note("f" + x); true }) note("x" + x)
  println(log)
