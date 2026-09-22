// EXPECT-ERROR: Illegal variable x in pattern alternative
def f(v: Any) = v match
  case x: Int | x: String => 1
  case _ => 0
