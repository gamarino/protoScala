// EXPECT: |left      |     right|hel|
@main def run(): Unit =
  val left = "left"
  val right = "right"
  println(f"|$left%-10s|$right%10s|${"hello"}%.3s|")
