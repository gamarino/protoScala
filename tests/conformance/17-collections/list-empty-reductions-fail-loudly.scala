// EXPECT-ERROR: reduce of empty collection
@main def run(): Unit =
  println(List[Int]().reduce((a, b) => a + b))
