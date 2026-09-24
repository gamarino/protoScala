// EXPECT: pos=[] kw=[encoding=utf-8]
@main def run(): Unit =
  println(__kwprobe.call(encoding = "utf-8"))
