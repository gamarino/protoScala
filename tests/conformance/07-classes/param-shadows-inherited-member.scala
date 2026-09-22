// EXPECT: 5 5
class Node(val v: Int)
class T2(v: Int) extends Node(v):
  def local = v

@main def run(): Unit =
  val t = new T2(5)
  println(t.v.toString + " " + t.local)
