// EXPECT: ()
@main def run(): Unit =
  val nothing = println("side effect")
  println(nothing)
