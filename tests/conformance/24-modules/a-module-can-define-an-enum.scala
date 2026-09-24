// EXPECT: Warn
// D91 makes every module an `object`, so this is the fixture that would have
// caught "an enum in a module is unusable" -- which it did, in the worked
// example, before the desugarer was fixed.
import util.Levels
@main def run(): Unit = println(Levels.worst())
