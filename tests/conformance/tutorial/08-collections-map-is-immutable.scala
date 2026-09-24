// EXPECT: List((apples,3), (pears,12))
@main def run(): Unit =
  val stock = Map("apples" -> 3, "pears" -> 0)
  val restocked = stock + ("pears" -> 12)
  println(restocked.toList.sortBy(_._1))
