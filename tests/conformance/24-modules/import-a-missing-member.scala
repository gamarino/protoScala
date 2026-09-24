// EXPECT-ERROR: Strings has no member named 'nope'
import util.Strings.{nope}
@main def run(): Unit = println(1)
