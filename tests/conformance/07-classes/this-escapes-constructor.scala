// XFAIL: 4
// D28: a closure made in a constructor captures the `this` of that moment. The
// instance is immutable, so every later field store yields a new object and the
// captured one stays behind, without the fields stored after the closure.
class Box(val a: Int):
  val f = () => this.later
  val later = a + 1

@main def run(): Unit = println(new Box(3).f())
