// EXPECT: Vector(1, 2, 3) 3 1 3 Vector(2, 4, 6) Vector(1, 3)
// Vector.map answers a Vector, List.map a List, from one implementation.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val v = Vector(1, 2, 3)
  println(v.toString + " " + v.length + " " + v(0) + " " + v.last + " " +
    v.map(_ * 2) + " " + v.filter(_ != 2))
