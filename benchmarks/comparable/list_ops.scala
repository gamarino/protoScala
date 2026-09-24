// EXPECT: 9999900000 100000 50000
// list_ops.scala - build a 100000-element List, then map / filter / fold it.
// The ROADMAP's benchmark suite v1 "list-ops" workload (Phase 3). It prints the
// fold result, the built length and the filtered length, so a silently wrong
// collection surface fails the run instead of reading as a fast one.
// The three numbers were computed with tools/scala3-3.9.0, never by hand.

def build(n: Int): List[Int] =
  var xs = List[Int]()
  var i = n
  while i > 0 do
    i -= 1
    xs = i :: xs
  xs

@main def benchListOps(): Unit =
  val xs = build(100000)
  val doubled = xs.map(_ * 2)
  val evens = xs.filter(_ % 2 == 0)
  val total = doubled.foldLeft(0)(_ + _)
  println(total.toString + " " + xs.length + " " + evens.length)
