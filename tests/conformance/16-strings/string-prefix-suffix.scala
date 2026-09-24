// EXPECT: world hello 0 true
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s = "helloworld"
  println(s.stripPrefix("hello") + " " + s.stripSuffix("world") + " " +
    "abc".compareTo("abc") + " " + "ABC".equalsIgnoreCase("abc"))
