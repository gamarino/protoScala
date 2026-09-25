// EXPECT: Red Blue 3 n=7
enum Colour:
  case Red, Green, Blue
object Config:
  val retries = 3
  def label(n: Int): String = "n=" + n
import Colour.*
import Config.{retries, label as show}
@main def run(): Unit =
  println(Red.toString + " " + Blue + " " + retries + " " + show(7))
