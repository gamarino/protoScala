// EXPECT: 3/1 1/2
class Ratio(val n: Int, val d: Int) {
  def this(n: Int) = this(n, 1)
  override def toString = n.toString + "/" + d
}

@main def run(): Unit = {
  println(new Ratio(3).toString + " " + new Ratio(1, 2))
}
