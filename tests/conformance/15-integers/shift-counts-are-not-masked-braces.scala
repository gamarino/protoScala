// EXPECT: 1166520745455517696 8589934592 36893488147419103232 0
// The brace-syntax twin of shift-counts-are-not-masked.scala (D112). scalac 3.9
// prints `271601776 2 2 128`.
@main def run(): Unit = {
  println((0x01030507 << 36).toString + " " + (1 << 33) + " " + (1L << 65) + " " + (256 >> 33))
}
