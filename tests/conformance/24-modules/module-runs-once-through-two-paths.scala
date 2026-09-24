// EXPECT: true
// The same module reached through a longer spelling of the same file: the
// exactly-once key is the file's CANONICAL ABSOLUTE PATH, not the logical path
// that was written, so both imports must find the one object.
import effects.Setup
@main def run(): Unit =
  val a = Setup
  val b = Setup
  println((a eq b) && a.ready)
