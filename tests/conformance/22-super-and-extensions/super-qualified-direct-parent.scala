// EXPECT: 2 11 9
// `super[B]` where B is a DIRECT parent, which is the form scalac accepts too,
// so this fixture is the one verified against it byte for byte.
trait A:
  def m: Int = 1
  def f(n: Int): Int = n + 1
trait B extends A:
  override def m: Int = 2
  override def f(n: Int): Int = n + 2
class C extends B:
  override def m: Int = 9
  def viaB: Int = super[B].m
  def fViaB(n: Int): Int = super[B].f(n) + 7
@main def run(): Unit =
  val c = new C()
  println(c.viaB.toString + " " + c.fViaB(2) + " " + c.m)
