// EXPECT: P(9,2,7) Empty() P(1,8,3)
case class P(a: Int, b: Int, c: Int)
case class Empty()
@main def run(): Unit = {
  println(P(1, 2, 3).copy(9, c = 7).toString + " " + Empty().copy() + " " + P(1, 2, 3).copy(b = 8))
}
