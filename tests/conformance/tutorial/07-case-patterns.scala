// EXPECT: zero small:3 list:1+2 pair tuple:a text other
def kind(v: Any): String = v match
  case 0 => "zero"
  case n: Int if n < 10 => "small:" + n
  case h :: t if t.length == 1 => "list:" + h + "+" + t.head
  case (_, _: Int) => "pair"
  case (s: String, _) => "tuple:" + s
  case "hello" | "hi" => "text"
  case _ => "other"

@main def run(): Unit =
  println(List(0, 3, List(1, 2), ("k", 5), ("a", "b"), "hi", 3.5).map(kind).mkString(" "))
