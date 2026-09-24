// EXPECT: point=Point(1,2) list=List(1, 2) none=None unit=() null=null
// Every hole goes through toScalaString, which calls a user toString.
// Verified against tools/scala3-3.9.0.
case class Point(x: Int, y: Int)
@main def run(): Unit =
  val p = Point(1, 2)
  val xs = List(1, 2)
  val o: Option[Int] = None
  val u: Unit = ()
  val z: String = null
  println(s"point=$p list=$xs none=$o unit=$u null=$z")
