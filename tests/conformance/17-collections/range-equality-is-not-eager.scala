// EXPECT: false true
// The length check short-circuits, so comparing a billion-element Range against
// a one-element List answers in O(1) and allocates nothing. Under the low-heap
// sweep this fixture would fail outright if the implementation materialised the
// Range. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println(((0 until 1000000000) == List(1)).toString + " " +
    ((0 until 1000000000) == (0 until 1000000000)))
