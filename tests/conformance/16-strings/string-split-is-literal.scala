// EXPECT: List(a, b, c) List(abc) List() List(a, , b)
// D70: the separator is literal, not a regular expression. `"a.b.c".split(".")`
// yields List(a, b, c) here and List() in Scala, which reads `.` as "any
// character". Matching Scala would mean a regex engine, a dependency this
// dialect does not have and would not add for `split`.
@main def run(): Unit =
  println("a.b.c".split(".").toString + " " + "abc".split(",") + " " +
    "".split(",").filter(_ != "") + " " + "a,,b".split(","))
