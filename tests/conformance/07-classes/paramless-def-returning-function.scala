// EXPECT: 5 5
// `c.g()` on a paramless member applies its result (`c.g.apply()`), as Scala does.
class C(n: Int):
  def g = () => n
  val h = () => n

@main def run(): Unit =
  val c = new C(5)
  println(c.g().toString + " " + c.h())
