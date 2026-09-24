// EXPECT: List(Ada:36) List(1, 3)
@main def run(): Unit =
  val ages = Map("Ada" -> 36)
  val names = List("Ada", "Grace")
  val known = for
    name <- names
    age <- ages.get(name)
  yield name + ":" + age
  println(known.toString + " " + List(Some(1), None, Some(3)).flatMap(o => o.toList))
