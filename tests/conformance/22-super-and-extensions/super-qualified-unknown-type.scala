// EXPECT-ERROR: is not a protoScala type
trait A:
  def m: Int = 1
class C extends A:
  def bad: Int = super[Widget].m
@main def run(): Unit = println(new C().bad)
