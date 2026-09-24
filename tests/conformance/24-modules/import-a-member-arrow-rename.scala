// EXPECT: HELLO!
// The Scala 2 spelling of a rename is accepted too.
import util.Strings.{shout => yell}
@main def run(): Unit = println(yell("hello"))
