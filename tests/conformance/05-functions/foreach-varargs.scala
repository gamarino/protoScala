// EXPECT: sum=10
def sum(xs: Int*): Int =
  var total = 0
  xs.foreach(x => total += x)
  total
@main def run(): Unit = println("sum=" + sum(1, 2, 3, 4))
