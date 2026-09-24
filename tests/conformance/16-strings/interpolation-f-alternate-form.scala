// EXPECT: 0xff 0XFF 0377 ff
// D55 lists the `#` flag; this is Java's alternate form, and it must not be
// silently inert. Verified against tools/scala3-3.9.0, which prints the same.
@main def run(): Unit =
  val h = 255
  println(f"$h%#x $h%#X $h%#o $h%x")
