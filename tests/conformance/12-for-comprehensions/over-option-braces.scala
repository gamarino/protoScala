// EXPECT: Some(3) None Some(6)
@main def run(): Unit = {
  val a = for { x <- Some(1); y <- Some(2) } yield x + y
  val b = for { x <- Some(1); y <- (None: Option[Int]) } yield x + y
  val c = for { x <- Some(3) if x > 2 } yield x * 2
  println(a.toString + " " + b + " " + c)
}
