// EXPECT: HELLO!
// An import written inside a block is hoisted to the unit (D96): its binding
// outlives the block.
@main def run(): Unit =
  val s =
    import util.Strings.{shout}
    shout("hello")
  println(s)
