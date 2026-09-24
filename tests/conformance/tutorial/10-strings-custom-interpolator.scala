// EXPECT: HELLO WORLD
extension (sc: StringContext)
  def shout(args: Any*): String =
    var out = sc.parts(0)
    var i = 0
    while i < args.length do
      out = out + args(i) + sc.parts(i + 1)
      i += 1
    out.toUpperCase

@main def run(): Unit =
  val who = "world"
  println(shout"hello $who")
