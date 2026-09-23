// EXPECT: A
class A {
  def who = "A"
}
trait U
trait T extends A
class C extends U with T

@main def run(): Unit = println(new C().who)
