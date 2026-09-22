// EXPECT: List(1, 3)
@main def run(): Unit =
  println(List(1, 2, 3).flatMap(x => if x % 2 == 1 then Some(x) else None))
