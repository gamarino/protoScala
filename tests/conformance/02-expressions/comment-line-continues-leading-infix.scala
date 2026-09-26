// EXPECT: 31
// A comment-only line is not a blank line: only whitespace counts, exactly as
// in dotty, so the leading `+` still continues the previous expression and
// `val x` is 1 + 5 * 6. scalac 3.9 prints 31 for this program.
@main def run(): Unit =
  val a = 5
  val x = 1
  // the comment does not end the expression
  + a * 6
  println(x)
