// EXPECT: HELLO! hello
// Python's `import util.strings`, JavaScript's `import * as Strings from ...`:
// the module object under its own name.
import util.Strings
@main def run(): Unit =
  println(Strings.shout("hello") + " " + Strings.greeting)
