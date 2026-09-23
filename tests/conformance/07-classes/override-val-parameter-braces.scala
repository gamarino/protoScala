// EXPECT: A sees 0 | 5 show=5
var log = ""
class A(val v: Int) {
  log = "A sees " + v
  def show = "show=" + v
}
class B(override val v: Int) extends A(0)

@main def run(): Unit = {
  val b = new B(5)
  println(log + " | " + b.v + " " + b.show)
}
