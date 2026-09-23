// EXPECT: int:3 long-is-int:5 double:1.5 string:3 bool:true char:z list:2 point:1 unit none
case class Point(x: Int, y: Int)
def kind(v: Any): String = v match {
  case i: Int if i == 5 => "long-is-int:" + i
  case i: Int => "int:" + i
  case d: Double => "double:" + d
  case s: String => "string:" + s.length
  case b: Boolean => "bool:" + b
  case c: Char => "char:" + c
  case l: List[?] => "list:" + l.length
  case p: Point => "point:" + p.x
  case u: Unit => "unit"
  case _ => "none"
}

@main def run(): Unit = {
  println(List(3, 5L, 1.5, "abc", true, 'z', List(1, 2), Point(1, 2), (), null).map(kind).mkString(" "))
}
