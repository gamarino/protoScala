// EXPECT: a|A|98|9
@main def run(): Unit =
  val tab = '\t'
  println("" + 'a' + '|' + 'A' + '|' + ('a' + 1) + '|' + tab.toInt)
