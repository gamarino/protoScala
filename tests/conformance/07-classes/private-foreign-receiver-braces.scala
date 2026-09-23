// EXPECT: 7
class Cell(val value: Int)
class Box(private val value: Int) {
  def read(c: Cell) = c.value
  def own = value
}

@main def run(): Unit = println(new Box(1).read(new Cell(7)))
