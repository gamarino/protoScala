// EXPECT-ERROR: copy has no parameter named z
case class P(a: Int, b: Int)
@main def run(): Unit = println(P(1, 2).copy(z = 3))
