// EXPECT: 7
// tak.scala - Takeuchi function tak(18, 12, 6): deep non-tail recursion.
// Twin of protoClojure's benchmarks/tak.clj (same arguments).
// Result: 7.

def tak(x: Int, y: Int, z: Int): Int =
  if !(y < x) then z
  else tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))

@main def benchTak(): Unit =
  println(tak(18, 12, 6))
