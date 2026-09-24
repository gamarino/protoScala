// EXPECT-ERROR: unknown string interpolator 'json'
// D56: only s, f and raw exist until extension methods arrive (Phase 4).
@main def run(): Unit =
  val n = 1
  println(json"$n")
