// EXPECT: 6
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
def sum(t: Tree): Int = t match
  case Tree.Leaf(n) => n
  case Tree.Node(l, r) => sum(l) + sum(r)
@main def run(): Unit =
  println(sum(Tree.Node(Tree.Leaf(1), Tree.Node(Tree.Leaf(2), Tree.Leaf(3)))))
