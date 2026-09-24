// EXPECT: pi=3.14 n=00042 plain=42 done=42%
@main def run(): Unit =
  val pi = 3.14159
  val n = 42
  println(f"pi=$pi%.2f n=$n%05d plain=$n done=$n%%")
