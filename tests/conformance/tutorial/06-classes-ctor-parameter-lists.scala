// EXPECT: 3
// D84: multiple constructor parameter lists concatenate into one flat list, so
// `new C(1)(2)` and `new C(1, 2)` are the same call. scalac accepts only the
// curried spelling.
class Rect(val w: Int)(val h: Int)
@main def run(): Unit =
  val r = new Rect(1)(2)
  println(r.w + r.h)
