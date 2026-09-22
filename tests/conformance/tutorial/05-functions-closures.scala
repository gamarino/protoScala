// EXPECT: 5 2
@main def run(): Unit =
  var x = 1
  val readX = () => x
  x = 5
  var calls = 0
  val record = () => calls += 1
  record()
  record()
  println(readX().toString + " " + calls)
