// EXPECT: zero one:1 two:3 List(one:1, one:2)
// The brace-syntax twin of top-level-overloads.scala.
def f(x: Int): String = { "one:" + x }
def f(x: Int, y: Int): String = { "two:" + (x + y) }
def f(): String = { "zero" }

@main def run(): Unit = {
  println(f() + " " + f(1) + " " + f(1, 2) + " " + List(1, 2).map(f))
}
