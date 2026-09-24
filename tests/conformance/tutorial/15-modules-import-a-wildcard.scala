// EXPECT: HELLO! hello
// `*` binds every member of the module at once.
import util.Strings.*
@main def run(): Unit =
  println(shout("hello") + " " + greeting)
