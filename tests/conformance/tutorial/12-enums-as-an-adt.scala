// EXPECT: 6 3
// The form neither Python nor JavaScript has: cases that carry different data, and
// a `match` that takes them apart.
enum Tree:
  case Leaf(n: Int)
  case Node(left: Tree, right: Tree)
def sum(t: Tree): Int = t match
  case Tree.Leaf(n)       => n
  case Tree.Node(l, r)    => sum(l) + sum(r)
def depth(t: Tree): Int = t match
  case Tree.Leaf(_)    => 1
  case Tree.Node(l, r) => 1 + (if depth(l) > depth(r) then depth(l) else depth(r))
@main def run(): Unit =
  val t = Tree.Node(Tree.Leaf(1), Tree.Node(Tree.Leaf(2), Tree.Leaf(3)))
  println(sum(t).toString + " " + depth(t))
