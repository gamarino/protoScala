// EXPECT: 2147483648 9223372036854775808
// D1: Scala on the JVM prints -2147483648 -9223372036854775808.
@main def run(): Unit =
  val i: Int = 2147483647
  val l: Long = 9223372036854775807L
  println((i + 1).toString + " " + (l + 1))
