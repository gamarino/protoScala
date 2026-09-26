// EXPECT: 3 0 1 5 1073741823
// The brace-syntax twin of unsigned-shift-of-a-non-negative.scala.
@main def run(): Unit = {
  def mid(lo: Int, hi: Int) = { (lo + hi) >>> 1 }
  println((7 >>> 1).toString + " " + (0 >>> 5) + " " + (2147483647 >>> 30) + " " +
          mid(2, 8) + " " + (2147483647 >>> 1))
}
