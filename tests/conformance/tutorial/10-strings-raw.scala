// EXPECT: C:\new\table 2 | C:\new\table 2
@main def run(): Unit =
  val n = 2
  println(raw"C:\new\table $n" + " | " + s"C:\\new\\table $n")
