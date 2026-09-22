// EXPECT: true true false true
@main def run(): Unit =
  val a = "pro" + "to"
  println((a == "proto").toString + " " + (1 == 1.0) + " " + (1 == 2) + " " + (a != "Scala"))
