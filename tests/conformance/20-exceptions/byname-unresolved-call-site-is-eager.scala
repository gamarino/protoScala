// EXPECT: escaped
// D53: at a call site the compiler cannot resolve to a declaration, a by-name
// argument is evaluated ONCE AT THE CALL, so the exception is raised before the
// callee's try is entered and escapes it. scalac resolves this statically and
// prints "-1" instead, because its `useIt` really receives a thunk.
// The receiver here is a method of a class reached through an `Any`; a bare
// top-level def IS resolved, and byname-thunk-throws-at-the-use-site.scala pins
// that side of the boundary.
class Runner:
  def useIt(x: => Int): Int =
    try x catch case e: RuntimeException => -1
def boom: Int = throw new RuntimeException("x")
@main def run(): Unit =
  val r: Any = new Runner()
  try
    println(r.asInstanceOf[Runner].useIt(boom))
  catch
    case e: RuntimeException => println("escaped")
