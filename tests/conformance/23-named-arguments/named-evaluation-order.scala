// EXPECT: b a 11
// Arguments are evaluated at the call site in SOURCE order, even when the names
// reorder them, which is Scala's rule.
var log = ""
def side(tag: String, v: Int): Int =
  log = log + tag + " "
  v
def f(a: Int, b: Int): Int = a + b * 10
@main def run(): Unit =
  val r = f(b = side("b", 1), a = side("a", 1))
  println(log + r)
