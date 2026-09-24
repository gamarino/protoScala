// EXPECT: a 1
// A0-5 ruling C2 (2026-09-23): a parameterised enum case IS a case class -- "es
// el usuario que esta haciendo la conversion" -- so it is a value key and two
// separately built Leaf(1)s are one key, which is Scala's answer. Under the old
// literal reading of "enum cases" as identity keys this would print `miss 1`.
// Written as XFAIL in Phase 3 pending `enum`; flipped in Phase 4 and verified
// against scalac.
enum Tree:
  case Leaf(n: Int)
  case Node(l: Tree, r: Tree)
@main def run(): Unit =
  val m = Map(Tree.Leaf(1) -> "a")
  println(m.getOrElse(Tree.Leaf(1), "miss") + " " + m.size)
