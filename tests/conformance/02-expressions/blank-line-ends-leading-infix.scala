// EXPECT: 1
// A blank line ends the expression, so the leading `+` starts a new statement
// instead of continuing `val x = 1` (dotty gates its leading-infix rule on
// `!pastBlankLine`). scalac 3.9 prints 1 for this program and warns that the
// operator line is indented too far to the left; protoScala printed 31 before
// this rule, having read `val x = 1 + a * 6`.
@main def run(): Unit =
  val a = 5
  val x = 1

  + a * 6
  println(x)
