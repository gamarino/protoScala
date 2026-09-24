// EXPECT: 81129638414606681695789005144064 true 9007199254740991
// 2^106 is far outside 64 bits: protoScala's Int is arbitrary precision (D1).
@main def run(): Unit =
  val max = 9007199254740991L
  val big = (max + 1) * (max + 1)    // 2^106, far outside 64 bits
  val square = big / (max + 1)
  println(big.toString + " " + (big > 0) + " " + (square - 1))
