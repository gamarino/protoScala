// EXPECT: 4 a\nb 3
@main def run(): Unit =
  val raw = """a\nb"""
  val multi = """x
y"""
  println(raw.length.toString + " " + raw + " " + multi.length)
