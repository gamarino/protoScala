// EXPECT: circle:3.0 rect:2.0x5.0 unknown
case class Circle(r: Double)
case class Rect(w: Double, h: Double)

def describe(shape: Any): String = shape match
  case Circle(r) => "circle:" + r
  case Rect(w, h) => "rect:" + w + "x" + h
  case _ => "unknown"

@main def run(): Unit =
  println(describe(Circle(3.0)) + " " + describe(Rect(2.0, 5.0)) + " " + describe(42))
