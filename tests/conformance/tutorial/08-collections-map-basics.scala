// EXPECT: 36 Some(36) None 0 true 2
@main def run(): Unit =
  val ages = Map("Ada" -> 36, "Alan" -> 41)
  println(ages("Ada").toString + " " + ages.get("Ada") + " " + ages.get("Grace") + " " +
    ages.getOrElse("Grace", 0) + " " + ages.contains("Alan") + " " + ages.size)
