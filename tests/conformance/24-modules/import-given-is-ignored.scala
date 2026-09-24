// EXPECT: HELLO!
// `given` selectors are parsed and ignored (D3, D93).
import util.Strings.{shout, given}
@main def run(): Unit = println(shout("hello"))
