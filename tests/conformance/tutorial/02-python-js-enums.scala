// EXPECT: Red 0 3 | leaf 3
// A simple enum is Python's Enum with `ordinal` where Python has `value`. The ADT
// form is the one neither Python nor JavaScript has.
enum Colour:
  case Red, Green, Blue
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
@main def run(): Unit =
  val described = Tree.Leaf(3) match
    case Tree.Leaf(n)    => "leaf " + n
    case Tree.Node(_, _) => "node"
  println(Colour.Red.toString + " " + Colour.Red.ordinal + " " + Colour.values.length +
    " | " + described)
