// EXPECT: Unique true Unique Empty()
case object Unique
case class Empty()
@main def run(): Unit = {
  val u = Unique
  println(u.toString + " " + (u == Unique) + " " + Unique.productPrefix + " " + Empty())
}
