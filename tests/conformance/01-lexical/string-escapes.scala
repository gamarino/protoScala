// EXPECT: 3 quote["] backslash[\] e-acute[é]
@main def run(): Unit =
  println("a\tb".length.toString + " quote[\"] backslash[\\] e-acute[é]")
