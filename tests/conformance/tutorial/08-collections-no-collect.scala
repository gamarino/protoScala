// EXPECT-ERROR: NoSuchMethodError: value collect is not a member of List
@main def run(): Unit =
  println(List(1, 2, 3, 4).collect { case n if n % 2 == 0 => n * 10 })
