// EXPECT: caught: provider exploded
// The whole point of the boundary shape in one file: a C++ exception thrown
// inside a foreign module is a catchable Scala exception at the call site, with
// its message intact.
import js.probe as p
@main def run(): Unit =
  try p.boom()
  catch case e: RuntimeException => println("caught: " + e.getMessage)
