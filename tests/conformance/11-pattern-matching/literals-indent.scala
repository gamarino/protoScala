// EXPECT: one few greeting char yes null unit two and a half char
def describe(x: Any): String = x match
  case 1 => "one"
  case 2 | 3 => "few"
  case "hi" => "greeting"
  case 'c' => "char"
  case true => "yes"
  case null => "null"
  case () => "unit"
  case 2.5 => "two and a half"
  case _ => "other"

@main def run(): Unit =
  println(List(1, 3, "hi", 'c', true, null, (), 2.5, 99).map(describe).mkString(" "))
