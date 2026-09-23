// EXPECT-ERROR: ClassCastException: String cannot be cast to Int
@main def run(): Unit = {
  val v: Any = "s"
  println(v.asInstanceOf[Int] + 1)
}
