// EXPECT: 42 1 3 42 true
class Holder(val f: () => Int) {
  def m() = 1
  def g(n: Int) = n + 1
}

@main def run(): Unit = {
  val h = new Holder(() => 42)
  val selected = h.f
  val eta = h.g
  println(h.f().toString + " " + h.m() + " " + eta(2) + " " + selected() + " " + (h.f eq selected))
}
