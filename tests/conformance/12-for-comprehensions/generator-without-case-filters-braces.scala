// EXPECT: List(1, 3)
// D35: a refutable pattern generator without Scala 3's `case` keyword is
// accepted and filters (scalac 3.9 rejects it: "pattern's type Some[Int] is
// more specialized than the right hand side expression's type Option[Int]").
@main def run(): Unit = {
  val os = List(Some(1), None, Some(3))
  val vs = for { Some(v) <- os } yield v
  println(vs)
}
