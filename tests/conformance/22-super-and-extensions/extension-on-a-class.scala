// EXPECT: 5
extension (p: P) def manhattan: Int = p.x + p.y
class P(val x: Int, val y: Int)
@main def run(): Unit = println(new P(2, 3).manhattan)
