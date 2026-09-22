// EXPECT: 10 0 6
def sum(xs: Int*): Int =
  var total = 0
  xs.foreach(x => total += x)
  total
def sumAll(xs: Int*): Int = sum(xs*)
@main def run(): Unit =
  println(sum(1, 2, 3, 4).toString + " " + sum() + " " + sumAll(1, 2, 3))
