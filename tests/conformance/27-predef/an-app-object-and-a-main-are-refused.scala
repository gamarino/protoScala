// EXPECT-ERROR: would both be the program
// Two entry points in one file would need a name to choose between them and a
// script has none, so protoScala refuses rather than silently ignoring one
// (D104) -- the same rule as "only one @main method is allowed per file".
object Demo extends App:
  println("from App")
@main def run(): Unit = println("from main")
