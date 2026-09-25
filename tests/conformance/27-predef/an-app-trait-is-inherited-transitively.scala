// EXPECT: via Runner
// The App test is on the linearization, not on the written parent name, so an
// object that extends a trait that extends App is still the program.
trait Runner extends App
object Demo extends Runner:
  println("via Runner")
