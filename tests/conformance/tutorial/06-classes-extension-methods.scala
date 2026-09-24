// EXPECT: 6 SHOUT 5
extension (n: Int) def triple: Int = n * 3
extension (s: String) def shout: String = s.toUpperCase
class Point(val x: Int, val y: Int)
extension (p: Point) def manhattan: Int = p.x + p.y
@main def run(): Unit =
  println(2.triple.toString + " " + "shout".shout + " " + new Point(2, 3).manhattan)
