// EXPECT: 1
// B does not define m, so super[B].m must find the m B inherits from A: the
// search probes T itself first and then continues after T, which is what "the
// member m as seen from B" means.
trait A:
  def m: Int = 1
trait B extends A
class C extends B:
  override def m: Int = 9
  def viaB: Int = super[B].m
@main def run(): Unit = println(new C().viaB)
