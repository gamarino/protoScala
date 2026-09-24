// EXPECT: Alice is 30 and next year 31
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val name = "Alice"
  val age = 30
  println(s"$name is $age and next year ${age + 1}")
