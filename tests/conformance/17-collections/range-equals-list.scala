// EXPECT: true true true false
// A0-8: Scala Seq equality across kinds. A Range and a List of the same elements
// are ==, and hash alike, so a Map keyed by one is found by the other.
// Implemented through the allocation-free SeqView, not by materialising.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println(((0 until 3) == List(0, 1, 2)).toString + " " +
    (List(0, 1, 2) == (0 until 3)) + " " +
    ((0 until 3).## == List(0, 1, 2).##) + " " +
    ((0 until 3) == List(0, 1)))
