// EXPECT: pi=3.14 n=00042 hex=ff pct=42%
// Verified byte for byte against tools/scala3-3.9.0 (under -Duser.language=en:
// protoScala is locale-free by decision A0-2/D55, the JVM is not).
@main def run(): Unit =
  val pi = 3.14159
  val n = 42
  val h = 255
  println(f"pi=$pi%.2f n=$n%05d hex=$h%x pct=$n%%")
