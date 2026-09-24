// EXPECT-ERROR: probe has no member named 'nope'
import js.probe.{nope}
@main def run(): Unit = println(1)
