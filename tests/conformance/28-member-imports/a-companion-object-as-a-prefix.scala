// EXPECT: 0 7
// A companion object is an object: its members import like any other's, values
// and factory methods alike. scalac 3.9.0 prints `companion: 0 7` for the same
// program.
class Box(val v: Int)
object Box:
  val zero = new Box(0)
  def make(n: Int): Box = new Box(n)
import Box.{zero, make}
@main def run(): Unit = println(zero.v.toString + " " + make(7).v)
