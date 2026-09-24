// EXPECT: 1.235e+03 1.23e+03
// Verified against tools/scala3-3.9.0 under -Duser.language=en.
@main def run(): Unit =
  val x = 1234.5678
  println(f"$x%.3e $x%.2e")
