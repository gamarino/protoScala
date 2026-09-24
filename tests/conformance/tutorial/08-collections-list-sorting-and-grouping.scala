// EXPECT: List(apple, fig, pear) List(fig, pear, apple) List(pear, fig, apple) List((3,List(fig)), (4,List(pear)), (5,List(apple)))
@main def run(): Unit =
  val words = List("pear", "fig", "apple")
  println(words.sorted.toString + " " + words.sortBy(_.length) + " " +
    words.sortWith(_ > _) + " " + words.groupBy(_.length).toList.sortBy(_._1))
