// EXPECT: List(a, b, c) a.b.c hello world
// D69: split answers a List, not an Array -- there is no Array type (D12), and
// printing scalac's Array gives `[Ljava.lang.String;@...`, so the fixture calls
// .toList on the scalac side to compare. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s = "a,b,c"
  val m = """|hello
             |world""".stripMargin
  println(s.split(",").toString + " " + s.replace(",", ".") + " " +
    m.split("\n").mkString(" "))
