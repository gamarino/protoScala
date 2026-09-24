// EXPECT: HELLO! A.L
// Python's `from util.strings import shout, initials as short`, and JavaScript's
// `import { shout, initials as short } from "./util/Strings.js"`.
import util.Strings.{shout, initials as short}
@main def run(): Unit =
  println(shout("hello") + " " + short("Ada Lovelace"))
