// EXPECT: 9007199254740992 9007199254740991 true
// The brace-syntax twin of negate-boundary.scala.
@main def run(): Unit = {
  val min = -9007199254740992L
  val neg = -min
  println(neg.toString + " " + (-(min + 1)) + " " + (neg == -min))
}
