// EXPECT: 5 6 7
// Implementing an abstract member needs no `override`; a `val` or a `var` may
// stand in for an abstract `def`.
trait T {
  def d: Int
  def e: Int
  def f: Int
}
class B extends T {
  def d = 5
  val e = 6
  var f = 7
}

@main def run(): Unit = {
  val b = new B
  println(b.d.toString + " " + b.e + " " + b.f)
}
