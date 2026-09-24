// EXPECT: a\nb 2
// raw"..." leaves escapes unprocessed. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val n = 2
  println(raw"a\nb $n")
