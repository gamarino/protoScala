// EXPECT: 3 List(1, 2, 3)
// foldRight calls f(x, acc), so rebuilding with `::` gives the list back.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val xs = List(1, 2, 3)
  println(xs.foldRight(0)((x, acc) => acc + 1).toString + " " +
    xs.foldRight(List[Int]())((x, acc) => x :: acc))
