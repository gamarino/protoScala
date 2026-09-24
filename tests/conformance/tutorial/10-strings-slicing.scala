// EXPECT: [helloworld] world hello world helloworl world hello cba
@main def run(): Unit =
  val s = "  helloworld  "
  println("[" + s.trim + "] " + s.trim.substring(5) + " " + s.trim.take(5) + " " +
    s.trim.drop(5) + " " + s.trim.init + " " + s.trim.stripPrefix("hello") + " " +
    s.trim.stripSuffix("world") + " " + "abc".reverse)
