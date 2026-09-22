// EXPECT: 5 8 10
class Cell(var value: Int)

@main def run(): Unit =
  val c = new Cell(5)
  val before = c.value
  c.value += 3
  val mid = c.value
  c.value = 10
  println(before.toString + " " + mid + " " + c.value)
