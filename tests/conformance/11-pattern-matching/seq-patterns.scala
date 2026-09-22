// EXPECT: empty pair:1,2 head:1 rest:List(2, 3) other
def f(xs: List[Int]): String = xs match
  case List() => "empty"
  case List(a, b) => "pair:" + a + "," + b
  case List(1, rest*) if rest.length > 2 => "long"
  case List(h, _*) if h == 1 && xs.length == 1 => "head:" + h
  case List(_, rest @ _*) if rest.length == 2 => "rest:" + rest
  case _ => "other"

@main def run(): Unit =
  println(List(List(), List(1, 2), List(1), List(1, 2, 3), List(5, 6, 7, 8)).map(f).mkString(" "))
