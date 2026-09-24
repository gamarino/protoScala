// EXPECT: Ada 41 1 2 3 List(0, 1, 4)
@main def run(): Unit =
  val names = Vector("Ada", "Alan")          // Python list / JavaScript Array
  val ages = Map("Ada" -> 36, "Alan" -> 41)  // Python dict / JavaScript Map
  val tags = Set("pioneer", "pioneer")       // Python set / JavaScript Set
  val withGrace = ages + ("Grace" -> 30)     // no ages[k] = v: a new map
  println(names(0) + " " + ages("Alan") + " " + tags.size + " " +
    ages.size + " " + withGrace.size + " " + (for i <- 0 until 3 yield i * i))
