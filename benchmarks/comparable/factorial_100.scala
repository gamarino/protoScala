// EXPECT: 93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000
// factorial_100.scala - 100! by an accumulating loop (158 digits).
// Twin of protoClojure's benchmarks/factorial-100.clj.
// The accumulator is declared BigInt so that the same source is valid on the
// JVM, where Int overflows at 13! and Long at 21!. On protoScala the
// declaration is not needed: integers promote to arbitrary precision (D1).
// Result: the 158-digit value of 100! on the first line of this file.

def factorial(n: Int): BigInt =
  var acc: BigInt = 1
  var k = 2
  while k <= n do
    acc = acc * k
    k += 1
  acc

@main def benchFactorial100(): Unit =
  println(factorial(100))
