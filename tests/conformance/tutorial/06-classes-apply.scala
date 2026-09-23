// EXPECT: 0 7 10
class Pair:
  var first = 0
  var second = 0
  def apply(i: Int): Int = if i == 0 then first else second
  def update(i: Int, v: Int): Unit = if i == 0 then first = v else second = v

@main def run(): Unit =
  val p = new Pair
  p(1) = 7
  val scale = (n: Int) => n * 10
  println(p(0).toString + " " + p(1) + " " + scale(1))
