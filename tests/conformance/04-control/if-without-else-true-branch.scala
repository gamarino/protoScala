// EXPECT: () ()
@main def run(): Unit =
  val x = if true then 5
  var n = 0
  val y = if n == 0 then { n = 1; n + 41 }
  println(x.toString + " " + y)
