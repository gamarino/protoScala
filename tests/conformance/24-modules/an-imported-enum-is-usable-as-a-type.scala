// EXPECT: Debug
// And its cases are reachable through a selector import, as a type and a term.
import util.Levels.{Level}
@main def run(): Unit =
  val l: Level = Level.Debug
  println(l match { case Level.Debug => "Debug"; case _ => "other" })
