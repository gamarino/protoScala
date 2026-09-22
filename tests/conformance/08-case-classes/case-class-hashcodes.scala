// EXPECT: -694993394 -1480185351 1971805870 1316541600 -1756661775 67081517 96354
case class Point(x: Int, y: Int)
case class Box(s: String)
case object Unique
case class Empty()

@main def run(): Unit =
  println(Point(1, 2).hashCode.toString + " " + Box("abc").hashCode + " " + (1, "a").hashCode + " " +
    (1, 2).hashCode + " " + Unique.hashCode + " " + Empty().hashCode + " " + "abc".hashCode)
