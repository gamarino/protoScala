// EXPECT: n1 false
class Node(val id: Int):
  Registry.last = this
  val label = "n" + id

object Registry:
  var last: Any = null

@main def run(): Unit =
  val n = new Node(1)
  println(n.label + " " + (Registry.last == n))
