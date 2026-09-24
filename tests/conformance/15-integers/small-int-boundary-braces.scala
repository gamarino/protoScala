// EXPECT: 9007199254740992 -9007199254740993 9007199254740991 true true
// The brace-syntax twin of small-int-boundary.scala.
@main def run(): Unit = {
  val max = 9007199254740991L
  val min = -9007199254740992L
  val over = max + 1
  val under = min - 1
  val back = over - 1
  println(over.toString + " " + under + " " + back + " " + (back == max) + " " + (over > max))
}
