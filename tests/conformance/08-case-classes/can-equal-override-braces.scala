// EXPECT: false false false
case class Strict(x: Int) {
  override def canEqual(that: Any): Boolean = false
}
@main def run(): Unit = {
  println((Strict(1) == Strict(1)).toString + " " + (Strict(1) == Strict(2)) + " " + Strict(1).canEqual(Strict(1)))
}
