// XFAIL: pi=3.14 n=00042 hex=ff pct=42%
// Task 4 implements the f interpolator; until then the compiler rejects it.
// The EXPECT line above is the spec-correct output, verified against scalac.
@main def run(): Unit =
  val pi = 3.14159
  val n = 42
  val h = 255
  println(f"pi=$pi%.2f n=$n%05d hex=$h%x pct=$n%%")
