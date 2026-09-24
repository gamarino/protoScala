// EXPECT-ERROR: sorted needs comparable elements; use sortWith
// D62: there is no Ordering to resolve (D3), so an unorderable pair fails
// loudly instead of silently picking an order.
class Opaque(val n: Int)
@main def run(): Unit =
  println(List(new Opaque(1), new Opaque(2)).sorted)
