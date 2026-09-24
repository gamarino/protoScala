// EXPECT: 11 21
// D76: protoScala accepts any ancestor in the linearization, where scalac 3.9
// requires `T` to be a DIRECT parent — it rejects `super[A]` here with "A does
// not name a parent of class C" (verified). protoScala keeps no direct-parent
// list: ClassInfo stores the flattened linearization, so the check is
// membership in it. This is permissiveness, not a semantic mismatch: no program
// scalac accepts behaves differently here, which
// super-qualified-direct-parent.scala pins against scalac.
trait A:
  def f(n: Int): Int = n + 1
trait B extends A:
  override def f(n: Int): Int = n + 2
class C extends B:
  def viaA(n: Int): Int = super[A].f(n)
  def viaB(n: Int): Int = super[B].f(n)
@main def run(): Unit =
  val c = new C()
  println(c.viaA(10).toString + " " + c.viaB(19))
