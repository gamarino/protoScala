// EXPECT: 3 x=11
@main def run(): Unit = {
  val a = 1
  val b: Int = 2
  var x = 10
  x = x + 1
  println((a + b).toString + " x=" + x)
}
