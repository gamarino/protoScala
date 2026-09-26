// EXPECT: 31
// The brace-syntax twin of comment-line-continues-leading-infix.scala.
@main def run(): Unit = {
  val a = 5
  val x = 1
  /* the comment does not end the expression */
  + a * 6
  println(x)
}
