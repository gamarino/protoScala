// EXPECT: true true true hit
@main def run(): Unit =
  println((List(1, 2, 3) == Vector(1, 2, 3)).toString + " " + ((1 to 3) == List(1, 2, 3)) + " " +
    (List(1, 2).## == Vector(1, 2).##) + " " + Map(Vector(1, 2) -> "hit").getOrElse(List(1, 2), "miss"))
