// EXPECT-ERROR: is not in the linearization of
// scalac rejects this at compile time ("Unrelated does not name a parent of
// class C"); types are erased here, so protoScala detects it when the send runs
// and names what was expected and what arrived (D4).
trait A:
  def m: Int = 1
trait Unrelated:
  def m: Int = 0
class C extends A:
  def bad: Int = super[Unrelated].m
@main def run(): Unit = println(new C().bad)
