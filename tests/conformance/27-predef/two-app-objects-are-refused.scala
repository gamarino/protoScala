// EXPECT-ERROR: only one App object is allowed per file
// Scala allows several App objects because the JVM launcher picks one by class
// name; protoScala runs a file, so there is nothing to pick with (D104).
object First extends App:
  println("first")
object Second extends App:
  println("second")
