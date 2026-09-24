// EXPECT-ERROR: Not found: type Seq
@main def run(): Unit =
  val v: Any = List(1, 2)
  val kind = v match
    case xs: Seq[_] => "a sequence"
    case _ => "something else"
  println(kind)
