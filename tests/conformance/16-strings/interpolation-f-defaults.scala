// EXPECT: a=1 b=hello c=true
// A hole with no specifier is %s. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val a = 1
  val b = "hello"
  val c = true
  println(f"a=$a b=$b c=$c")
