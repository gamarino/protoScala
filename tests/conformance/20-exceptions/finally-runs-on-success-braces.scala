// EXPECT: body finally 1
@main def run(): Unit = {
  val r =
    try {
      print("body ")
      1
    } finally {
      print("finally ")
    }
  println(r)
}
