// EXPECT: 10 List(1, 4, 9, 16) List(2, 4, 6)
@main def run(): Unit =
  var total = 0
  for i <- 0 until 5 do
    total += i
  val squares = for i <- 1 to 4 yield i * i
  println(total.toString + " " + squares + " " + (1 to 3).map(_ * 2))
