// EXPECT-ERROR: Strings has no member named 'nope'
// The module was found; the selector was not one of its members.
import util.Strings.{nope}
@main def run(): Unit = println(1)
