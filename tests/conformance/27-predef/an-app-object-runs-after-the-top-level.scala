// EXPECT: app
// The file's own top-level statements run first, then the App object, so an App
// object can use a value the top level computed. Scala 3 has no top-level
// statements at all (D9), so this ordering is protoScala's own and is not
// verified against scalac; what IS verified is that Scala runs the App object's
// body as the program.
val greeting = "app"
object Demo extends App:
  println(greeting)
println("top level")
