// EXPECT-ERROR: IndexOutOfBoundsException
@main def run(): Unit =
  println(List(1, 2, 3)(5))
