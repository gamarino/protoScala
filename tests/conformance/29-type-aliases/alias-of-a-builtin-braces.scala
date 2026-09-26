// EXPECT: 3 1 6 x
// The brace-syntax twin of alias-of-a-builtin.scala.
type Name = String
type Pair = (Int, Int)
type Ints = List[Int]
type Alias = Name

@main def run(): Unit = {
  val n: Name = "abc"
  val p: Pair = (1, 2)
  val xs: Ints = List(1, 2, 3)
  val a: Alias = "x"
  println(n.length.toString + " " + p._1 + " " + xs.sum + " " + a)
}
