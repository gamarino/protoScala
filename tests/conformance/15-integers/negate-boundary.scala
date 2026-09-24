// EXPECT: 9007199254740992 9007199254740991 true
// -PROTO_SMALL_INT_MIN does not fit a SmallInteger and must promote. The
// second field is -(min + 1) = 9007199254740991, verified against scalac.
@main def run(): Unit =
  val min = -9007199254740992L
  val neg = -min
  println(neg.toString + " " + (-(min + 1)) + " " + (neg == -min))
