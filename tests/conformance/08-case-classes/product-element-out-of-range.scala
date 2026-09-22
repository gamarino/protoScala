// EXPECT-ERROR: IndexOutOfBoundsException: Index out of range: 2
case class P(a: Int, b: Int)
@main def run(): Unit = println(P(1, 2).productElement(2))
