// EXPECT: 0 3 1 2
class Bag(val name: String, val xs: Int*):
  def n = xs.length
def make(ys: Int*): Bag = new Bag("b", ys*)

@main def run(): Unit =
  println(make().n.toString + " " + make(1, 2, 3).n + " " + new Bag("a", 9).n + " " + make(1, 2, 3).xs(1))
