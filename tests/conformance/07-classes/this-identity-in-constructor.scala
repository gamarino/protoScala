// XFAIL: true
// D28: `this` stored during construction is an earlier version of the instance,
// so it is not identical to the object `new` finally returns.
class C(val n: Int):
  val captured = this
  val extra = n * 2

@main def run(): Unit =
  val c = new C(3)
  println((c.captured eq c).toString)
