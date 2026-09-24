// EXPECT: List((Ada,37), (Alan,42)) List((Alan,41)) List(Ada, Alan)
@main def run(): Unit =
  val ages = Map("Ada" -> 36, "Alan" -> 41)
  println(ages.map((name, age) => (name, age + 1)).toList.sortBy(_._1).toString + " " +
    ages.filter((name, age) => age > 40).toList + " " +
    ages.map(pair => pair._1).toList.sorted)
