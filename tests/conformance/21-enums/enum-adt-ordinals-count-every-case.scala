// EXPECT: 0 1
// Ordinals count EVERY case in declaration order, even the parameterised ones,
// exactly as Scala's do — while `values` covers only the singletons, which is
// why an enum with a parameterised case has no `values` at all.
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
@main def run(): Unit =
  println(Tree.Leaf(1).ordinal.toString + " " + Tree.Node(Tree.Leaf(1), Tree.Leaf(2)).ordinal)
