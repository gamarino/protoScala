// EXPECT-ERROR: sorted needs comparable elements; use sortWith
class Money(val cents: Int)

@main def run(): Unit =
  println(List(new Money(2), new Money(1)).sorted)
