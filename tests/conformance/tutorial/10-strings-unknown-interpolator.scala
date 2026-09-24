// EXPECT-ERROR: unknown string interpolator 'json'
@main def run(): Unit =
  val n = 1
  println(json"value is $n")
