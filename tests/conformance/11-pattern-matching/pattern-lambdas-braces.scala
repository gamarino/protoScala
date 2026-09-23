// EXPECT: List(a, bb) List(3, 7)
@main def run(): Unit = {
  val pairs = List((1, "a"), (2, "b"))
  println(pairs.map { case (n, s) => s * n }.toString + " " + List((1, 2), (3, 4)).map { case (a, b) => a + b })
}
