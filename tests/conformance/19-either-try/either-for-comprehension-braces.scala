// EXPECT: Right(5) Left(no)
// The brace-syntax twin of either-for-comprehension.scala.
@main def run(): Unit = {
  val a: Either[String, Int] = Right(2)
  val b: Either[String, Int] = Right(3)
  val bad: Either[String, Int] = Left("no")
  val ok = for {
    x <- a
    y <- b
  } yield x + y
  val ko = for {
    x <- bad
    y <- b
  } yield x + y
  println(ok.toString + " " + ko)
}
