// EXPECT: a "quoted" word and a \n that is not an escape / and a second line
@main def run(): Unit =
  val block = """a "quoted" word and a \n that is not an escape
and a second line"""
  println(block.split("\n").mkString(" / "))
