// EXPECT: 9007199254740992 9223372036854775808 15511210043330985984000000
// D1: Int and Long never wrap around. Scala prints -9223372036854775808 for the
// second value; protoScala promotes to arbitrary precision.
def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)
@main def run(): Unit =
  val a = 9007199254740991L + 1
  val b = 9223372036854775807L + 1
  println(a.toString + " " + b + " " + fact(25))
