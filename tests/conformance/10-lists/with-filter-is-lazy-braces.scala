// EXPECT: f1 x1 f2 x2 f3 x3
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
@main def run(): Unit = {
  List(1, 2, 3).withFilter(x => { note("f" + x); true }).foreach(x => note("x" + x))
  println(log)
}
