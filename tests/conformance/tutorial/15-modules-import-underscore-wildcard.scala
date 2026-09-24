// EXPECT: hello
// `_` is the Scala 2 spelling of the same wildcard, and is accepted too.
import util.Strings._
@main def run(): Unit =
  println(greeting)
