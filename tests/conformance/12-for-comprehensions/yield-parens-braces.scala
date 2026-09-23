// EXPECT: List(2, 4, 6)
@main def run(): Unit = {
  val xs = List(1, 2, 3)
  val ys = for (x <- xs) yield x * 2
  println(ys)
}
