// EXPECT: 2 7 7 4
// A constructor binds named arguments and fills its defaults in its own prologue,
// exactly as a method does; NEW carries a KwSendSite instead of a SendSite and no
// extra opcode is needed.
class P(val x: Int, val y: Int = 7)
@main def run(): Unit =
  val p = new P(x = 2)
  println(p.x.toString + " " + p.y + " " + new P(1).y + " " + new P(3, 4).y)
