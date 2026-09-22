// EXPECT: 5
@main def run(): Unit =
  var x = 1
  val get = () => x
  x = 5
  println(get())
