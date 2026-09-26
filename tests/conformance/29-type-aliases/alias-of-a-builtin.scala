// EXPECT: 3 1 6 x
// `type X = Int` answered "'type' definitions are not implemented yet" before
// Track S. Types are erased (D4), so an alias is a naming concern: the compiler
// records the target name and expands it wherever a type name is consumed.
// scalac 3.9 prints `3 1 6 x` for the same program.
type Name = String
type Pair = (Int, Int)
type Ints = List[Int]
type Alias = Name

@main def run(): Unit =
  val n: Name = "abc"
  val p: Pair = (1, 2)
  val xs: Ints = List(1, 2, 3)
  val a: Alias = "x"
  println(n.length.toString + " " + p._1 + " " + xs.sum + " " + a)
