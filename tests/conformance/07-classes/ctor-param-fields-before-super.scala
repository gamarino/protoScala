// EXPECT: A sees x=5
abstract class A:
  def show: String
  println("A sees " + show)
class B(val x: Int) extends A:
  def show = "x=" + x

@main def run(): Unit = new B(5)
