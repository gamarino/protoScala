// EXPECT-ERROR: parameter a of copy is already instantiated
case class P(a: Int, b: Int)
@main def run(): Unit = println(P(1, 2).copy(3, a = 4))
