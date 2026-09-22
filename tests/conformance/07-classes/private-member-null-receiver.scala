// EXPECT-ERROR: cannot invoke 'value' on null
// A private member's class-qualified key (D5) must not leak into a message.
class Cell(val value: Int)
class Box(private val value: Int):
  def read(c: Cell) = c.value

@main def run(): Unit = println(new Box(1).read(null))
