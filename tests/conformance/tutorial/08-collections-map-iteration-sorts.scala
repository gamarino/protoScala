// EXPECT: List(Ada, Alan, Grace) List(30, 36, 41) List((Ada,36), (Alan,41), (Grace,30))
@main def run(): Unit =
  val ages = Map("Grace" -> 30, "Ada" -> 36, "Alan" -> 41)
  println(ages.keys.toList.sorted.toString + " " + ages.values.toList.sorted + " " +
    ages.toList.sortBy(_._1))
