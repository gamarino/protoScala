// EXPECT: 9 show=9
class A(val v: Int) {
  def show = "show=" + v
}
class B extends A(0) {
  override val v = 9
}

@main def run(): Unit = {
  val b = new B
  println(b.v.toString + " " + b.show)
}
