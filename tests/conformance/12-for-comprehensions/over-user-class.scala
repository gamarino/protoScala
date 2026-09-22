// EXPECT: Box(12)
case class Box(v: Int):
  def map(f: Int => Int): Box = Box(f(v))
  def flatMap(f: Int => Box): Box = f(v)

@main def run(): Unit =
  println(for (a <- Box(3); b <- Box(4)) yield a * b)
