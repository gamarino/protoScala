// EXPECT: Ada is 36, and next year 37
@main def run(): Unit =
  val name = "Ada"
  val age = 36
  println(s"$name is $age, and next year ${age + 1}")
