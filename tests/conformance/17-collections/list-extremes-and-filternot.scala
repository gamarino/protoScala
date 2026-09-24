// EXPECT: 3 1 2 List(1, 3)
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println(List(1, 2, 3).minBy(x => -x).toString + " " + List(1, 2, 3).maxBy(x => -x) + " " +
    List(1, 2, 3).reduceRight((x, acc) => x - acc) + " " + List(1, 2, 3).filterNot(_ == 2))
