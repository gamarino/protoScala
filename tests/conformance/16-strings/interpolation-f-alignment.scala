// EXPECT: |left      ||     right||+7|| 7|
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s = "left"
  val r = "right"
  val n = 7
  println(f"|$s%-10s||$r%10s||$n%+d||$n% d|")
