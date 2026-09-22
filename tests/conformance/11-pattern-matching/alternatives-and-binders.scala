// EXPECT: weekend weekday Point(1,1)-diag x=3
case class Point(x: Int, y: Int)
def day(d: String) = d match
  case "sat" | "sun" => "weekend"
  case _ => "weekday"
def pt(p: Point) = p match
  case q @ Point(a, b) if a == b => q.toString + "-diag"
  case Point(a, _) => "x=" + a

@main def run(): Unit = println(day("sun") + " " + day("mon") + " " + pt(Point(1, 1)) + " " + pt(Point(3, 4)))
