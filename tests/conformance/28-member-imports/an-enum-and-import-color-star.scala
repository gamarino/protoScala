// EXPECT: Red Green Blue
// The idiomatic Scala that Phase 6 broke: `import` became a module-loading form
// and plain Scala's member import went with it, so this three-line program failed
// with `ImportError: no module found for 'Color'`. It is the first thing a Scala
// programmer writes and the first thing that has to work. Verified against scalac
// 3.9.0, which prints the same line.
enum Color:
  case Red, Green, Blue
import Color.*
@main def run(): Unit = println(Red.toString + " " + Green + " " + Blue)
