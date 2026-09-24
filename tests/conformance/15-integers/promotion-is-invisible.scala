// EXPECT: true true true true
// A promoted value behaves exactly like a small one: ==, hashCode consistency,
// ordering and toString all agree across the boundary.
@main def run(): Unit =
  val a = 9007199254740991L + 1
  val b = 4503599627370496L * 2
  println((a == b).toString + " " + (a.## == b.##) + " " + (a <= b) + " " + (a.toString == b.toString))
