// EXPECT: ()
@main def run(): Unit =
  val r = if false then 1
  println(r)
