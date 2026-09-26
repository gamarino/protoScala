// EXPECT: 3 0 1 5 1073741823
// `>>>` used to raise UnsupportedOperationException unconditionally: it fills the
// vacated bits from the operand's width, and protoScala's integers have no width
// (D1). For a **non-negative** operand there are no bits to fill, so the answer is
// the arithmetic shift and agrees with Scala exactly -- which covers the
// `(lo + hi) >>> 1` idiom binary search is written with. scalac 3.9 prints
// `3`, `0`, `1`, `5` and `1073741823` for these.
@main def run(): Unit =
  def mid(lo: Int, hi: Int) = (lo + hi) >>> 1
  println((7 >>> 1).toString + " " + (0 >>> 5) + " " + (2147483647 >>> 30) + " " +
          mid(2, 8) + " " + (2147483647 >>> 1))
