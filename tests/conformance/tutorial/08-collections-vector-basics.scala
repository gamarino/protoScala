// EXPECT: Vector(10, 20, 30) 20 Vector(10, 20, 30, 40) Vector(0, 20, 30) Vector(1, 2, 3) List(10, 20, 30)
@main def run(): Unit =
  val v = Vector(10, 20, 30)
  println(v.toString + " " + v(1) + " " + (v :+ 40) + " " + v.updated(0, 0) + " " +
    v.map(_ / 10) + " " + v.toList)
