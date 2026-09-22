// EXPECT-ERROR: duplicate pattern variable: x
def f(v: Any) = v match
  case (x, x) => x
  case _ => 0
