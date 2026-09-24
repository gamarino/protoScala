// EXPECT-ERROR: has no module 'nope'
// A provider miss is reported, not silently empty.
import js.nope
@main def run(): Unit = println(1)
