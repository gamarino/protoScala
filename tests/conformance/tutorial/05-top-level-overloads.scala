// EXPECT: zero one:1 two:3
// Tutorial chapter 5, §5.11. Verified against scalac 3.9.0.
def f(): String = "zero"
def f(x: Int): String = "one:" + x
def f(x: Int, y: Int): String = "two:" + (x + y)

@main def run(): Unit =
  println(f() + " " + f(1) + " " + f(1, 2))
