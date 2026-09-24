// EXPECT: outer[inner 3]
// A hole is parsed by the same pipeline as the file, so it may hold another
// interpolation. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val n = 3
  println(s"outer[${s"inner $n"}]")
