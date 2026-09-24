// EXPECT: HELLO, WORLD hello, world Ada 7 10 true true true true -1
@main def run(): Unit =
  val s = "Hello, World"
  println(s.toUpperCase + " " + s.toLowerCase + " " + "ada".capitalize + " " +
    s.indexOf("World") + " " + s.lastIndexOf("l") + " " + s.contains("lo") + " " +
    s.startsWith("He") + " " + s.endsWith("ld") + " " + "ABC".equalsIgnoreCase("abc") + " " +
    "abc".compareTo("abd"))
