// EXPECT: Some(4) None 4 0
def half(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None

@main def run(): Unit =
  println(half(8).toString + " " + half(3) + " " + half(8).getOrElse(0) + " " + half(3).getOrElse(0))
