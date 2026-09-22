// EXPECT: 2 5
// A `val` may override a concrete `def` (with `override`) and implement an
// abstract one (without).
class A:
  def d = 1
trait T:
  def e: Int
class B extends A:
  override val d = 2
class C extends T:
  val e = 5

@main def run(): Unit = println(new B().d.toString + " " + new C().e)
