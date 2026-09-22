// EXPECT: 6
@main def run(): Unit =
  var total = 0
  for (x <- List(1, 2, 3))
    total += x
  println(total)
