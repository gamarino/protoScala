// EXPECT: $5 costs $5
// `$$` is a literal dollar. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val n = 5
  println(s"$$$n costs $$$n")
