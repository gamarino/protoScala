// EXPECT: List(2, 3, 4) List(3) 6
@main def run(): Unit =
  val xs = List(1, 2, 3)
  var sum = 0
  xs.foreach(sum += _)
  println(xs.map(_ + 1).toString + " " + xs.filter(_ > 2) + " " + sum)
