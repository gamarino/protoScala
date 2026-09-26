// EXPECT: 1
// The brace-syntax twin of blank-line-ends-deeper-continuation.scala.
@main def run(): Unit = {
  val a = 5
  val x = 1

    + a * 6
  println(x)
}
