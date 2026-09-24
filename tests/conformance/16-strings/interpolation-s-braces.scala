// EXPECT: Alice is 30 and next year 31
// The brace-syntax twin of interpolation-s.scala.
@main def run(): Unit = {
  val name = "Alice"
  val age = 30
  println(s"$name is $age and next year ${age + 1}")
}
