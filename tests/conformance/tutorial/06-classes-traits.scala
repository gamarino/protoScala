// EXPECT: T2>T1>B>A
abstract class A:
  def who: String = "A"
trait T1 extends A:
  override def who = "T1>" + super.who
trait T2 extends A:
  override def who = "T2>" + super.who
class B extends A:
  override def who = "B>" + super.who
class C extends B with T1 with T2

@main def run(): Unit = println(new C().who)
