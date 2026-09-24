// EXPECT: 9007199254740992 -9007199254740993 9007199254740991 true true
// 2^53 = 9007199254740992 is one past PROTO_SMALL_INT_MAX, so every line
// below crosses the SmallInteger -> LargeInteger promotion (DESIGN §3.6, D1).
@main def run(): Unit =
  val max = 9007199254740991L        // PROTO_SMALL_INT_MAX
  val min = -9007199254740992L       // PROTO_SMALL_INT_MIN
  val over = max + 1                 // promotes
  val under = min - 1                // promotes
  val back = over - 1                // demotes back into the SmallInteger range
  println(over.toString + " " + under + " " + back + " " + (back == max) + " " + (over > max))
