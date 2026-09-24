// EXPECT: List(20, 40)
@main def run(): Unit =
  println(List(1, 2, 3, 4).filter(_ % 2 == 0).map(_ * 10))
