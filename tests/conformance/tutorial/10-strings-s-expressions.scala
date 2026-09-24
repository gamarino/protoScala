// EXPECT: Point(1,2) List(1, 2) 2 3 $5
case class Point(x: Int, y: Int)

@main def run(): Unit =
  val p = Point(1, 2)
  val xs = List(1, 2)
  val price = 5
  println(s"$p $xs ${xs.length} ${p.x + p.y} $$$price")
