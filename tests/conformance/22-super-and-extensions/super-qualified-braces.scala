// EXPECT: 1 2 3
// D76: protoScala accepts any ancestor in the linearization, where scalac 3.9
// requires `T` to be a DIRECT parent — it rejects `super[A]` here with "A does
// not name a parent of class C" (verified). protoScala keeps no direct-parent
// list: ClassInfo stores the flattened linearization, so the check is
// membership in it. This is permissiveness, not a semantic mismatch: no program
// scalac accepts behaves differently here, which
// super-qualified-direct-parent.scala pins against scalac.
trait A { def m: Int = 1 }
trait B extends A { override def m: Int = 2 }
class C extends B {
  override def m: Int = 3
  def fromA: Int = super[A].m
  def fromB: Int = super[B].m
}
@main def run(): Unit = {
  val c = new C()
  println(c.fromA.toString + " " + c.fromB + " " + c.m)
}
