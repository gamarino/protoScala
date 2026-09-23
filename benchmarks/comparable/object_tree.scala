// EXPECT: 131071 278364170 725606090
// object_tree.scala - a deep immutable object graph: build a complete binary
// tree of case classes (depth 16, 131071 objects), path-copy its leftmost
// spine (the copy shares every right subtree with the original), and fold
// both versions with pattern matching. Twin of
// benchmarks/comparable/python/object_tree.py.

sealed trait Tree
case class Leaf(value: Int) extends Tree
case class Node(left: Tree, right: Tree, weight: Int) extends Tree

def build(depth: Int, seed: Int): Tree =
  if depth == 0 then Leaf(seed % 1000)
  else Node(build(depth - 1, seed * 2 + 1), build(depth - 1, seed * 2 + 2), depth)

def bump(t: Tree): Tree = t match
  case Leaf(v)       => Leaf(v + 1)
  case Node(l, r, w) => Node(bump(l), r, w)

def count(t: Tree): Int = t match
  case Leaf(_)       => 1
  case Node(l, r, _) => 1 + count(l) + count(r)

def checksum(t: Tree): Long = t match
  case Leaf(v)       => v
  case Node(l, r, w) => (checksum(l) * 31 + checksum(r) + w) % 1000000007L

@main def benchObjectTree(): Unit =
  val t = build(16, 0)
  val t2 = bump(t)
  println(count(t).toString + " " + checksum(t) + " " + checksum(t2))
