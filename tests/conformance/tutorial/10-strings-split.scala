// EXPECT: List(name, age, city) 3 NAME|AGE|CITY name / age / city
@main def run(): Unit =
  val csv = "name,age,city"
  println(csv.split(",").toString + " " + csv.split(",").length + " " +
    csv.split(",").map(_.toUpperCase).mkString("|") + " " + csv.replace(",", " / "))
