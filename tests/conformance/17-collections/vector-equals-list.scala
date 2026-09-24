// EXPECT: true true true false
// A0-7/D59: Seq equality and hashing are decided once, in valuesEqual and
// scalaHash over the one SeqView, so `==` written as an operator and
// `.equals` can never disagree. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println((List(1, 2) == Vector(1, 2)).toString + " " + (Vector(1, 2) == List(1, 2)) + " " +
    (List(1, 2).## == Vector(1, 2).##) + " " + (Vector(1, 2) == Vector(2, 1)))
