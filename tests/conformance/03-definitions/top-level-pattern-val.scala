// EXPECT: 1 two 3.5 | 10 20 | 6
// A top-level pattern definition binds each variable as a global, readable from
// a def and from @main; its temporary is a slot of the top-level frame, not a
// hidden global. `var` patterns stay assignable (scalac 3.9 prints the same).
val (a, b, c) = (1, "two", 3.5)
var (x, y) = (10, 20)
val List(p, q, r) = List(1, 2, 3)

def show(): String = s"$a $b $c | $x $y"

@main def run(): Unit =
  println(show() + s" | ${p + q + r}")
