// EXPECT: HELLO! A.L hello
// Named selectors bind members directly, and `as` renames one of them.
import util.Strings.{shout, initials as short, greeting}
@main def run(): Unit =
  println(shout("hello") + " " + short("Ada Lovelace") + " " + greeting)
