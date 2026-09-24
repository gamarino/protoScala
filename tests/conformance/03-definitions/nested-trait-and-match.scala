// EXPECT: leaf 3
object Ast:
  sealed trait T
  case class Leaf(n: Int) extends T
  case class Node(l: T, r: T) extends T
@main def run(): Unit =
  val t: Ast.T = Ast.Leaf(3)
  val out = t match
    case Ast.Leaf(n) => "leaf " + n
    case Ast.Node(_, _) => "node"
  println(out)
