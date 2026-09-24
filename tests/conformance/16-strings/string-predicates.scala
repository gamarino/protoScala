// EXPECT: 2 true true false
// The collection-like predicates of StringOps.
@main def run(): Unit =
  println("abc".count(_ > 'a').toString + " " + "abc".forall(_ >= 'a') + " " +
    "abc".exists(_ == 'b') + " " + "abc".exists(_ == 'z'))
