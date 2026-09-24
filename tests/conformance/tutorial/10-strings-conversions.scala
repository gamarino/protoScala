// EXPECT: 12 12 3.5 true 42 ababab
@main def run(): Unit =
  println("12".toInt.toString + " " + "12".toLong + " " + "3.5".toDouble + " " +
    "true".toBoolean + " " + 42.toString + " " + "ab".repeat(3))
