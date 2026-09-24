// EXPECT: 13 24
// `m(a = 1)` inside the template is `this.m(a = 1)`, and a default may read a
// field of `this`.
class K(val base: Int):
  def m(a: Int, b: Int = base): Int = a * 10 + b
  def useMe: Int = m(a = 1)
@main def run(): Unit =
  val k = new K(3)
  println(k.useMe.toString + " " + k.m(b = 4, a = 2))
