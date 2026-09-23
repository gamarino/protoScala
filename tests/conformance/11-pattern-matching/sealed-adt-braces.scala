// EXPECT: 14 (2 + (3 * 4))
sealed trait Expr
case class Num(n: Int) extends Expr
case class Add(l: Expr, r: Expr) extends Expr
case class Mul(l: Expr, r: Expr) extends Expr
def eval(e: Expr): Int = e match {
  case Num(n) => n
  case Add(l, r) => eval(l) + eval(r)
  case Mul(l, r) => eval(l) * eval(r)
}
def show(e: Expr): String = e match {
  case Num(n) => n.toString
  case Add(l, r) => "(" + show(l) + " + " + show(r) + ")"
  case Mul(l, r) => "(" + show(l) + " * " + show(r) + ")"
}

@main def run(): Unit = {
  val e = Add(Num(2), Mul(Num(3), Num(4)))
  println(eval(e).toString + " " + show(e))
}
