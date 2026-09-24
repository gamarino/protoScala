// EXPECT: HELLO! hello
// The plainest form: the module object is bound under its own name, and its
// top-level definitions are its members.
import util.Strings
@main def run(): Unit =
  println(Strings.shout("hello") + " " + Strings.greeting)
