// EXPECT: List((1,a), (2,b)) List((a,0), (b,1)) List(1, 2, 3) List(1, 2, 3, 4)
// zip truncates to the shorter side. Every pair is a Tuple2 case-class
// instance, never a ProtoTuple (DESIGN §4.6). Verified against
// tools/scala3-3.9.0.
@main def run(): Unit =
  println(List(1, 2, 3).zip(List("a", "b")).toString + " " +
    List("a", "b").zipWithIndex + " " + List(1, 2, 2, 3, 1).distinct + " " +
    List(List(1, 2), List(3), List(4)).flatten)
