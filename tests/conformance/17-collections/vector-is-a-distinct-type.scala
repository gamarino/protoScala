// EXPECT: true false Vector(1, 2, 3) 6 1-2-3
// A Vector and a List are `==` when their elements are, and still distinct
// types. Verified against tools/scala3-3.9.0 (which warns that the second test
// is always false, and it is).
@main def run(): Unit =
  println(Vector(1).isInstanceOf[Vector[Int]].toString + " " +
    List(1).isInstanceOf[Vector[Int]] + " " + Vector(3, 1, 2).sorted + " " +
    Vector(1, 2, 3).foldLeft(0)(_ + _) + " " + Vector(1, 2, 3).mkString("-"))
