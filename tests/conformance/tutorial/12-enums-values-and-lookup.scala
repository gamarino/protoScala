// EXPECT: Red,Green,Blue | Green | Blue | no such case
enum Colour:
  case Red, Green, Blue
@main def run(): Unit =
  val missing =
    try Colour.valueOf("Purple").toString
    catch case e: IllegalArgumentException => "no such case"
  println(Colour.values.map(_.toString).mkString(",") + " | " + Colour.valueOf("Green") +
    " | " + Colour.fromOrdinal(2) + " | " + missing)
