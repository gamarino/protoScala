// EXPECT: List(P(1), P(2)) true List((1,a), (2,b))
case class P(n: Int)
@main def run(): Unit =
  val ps = List(P(1), P(2))
  println(ps.toString + " " + (ps == List(P(1), P(2))) + " " + List((1, "a"), (2, "b")))
