// EXPECT: 2 3 List(4, 5)
// D110. scalac 3.9 prints `2.0 3.0 List(4.0, 5.0)`; the first line is
// protoScala's answer, which this fixture pins. protoScala widens where the
// declared type is written *at the point of declaration*, and these three sites
// do not write it there. An assignment names a variable declared earlier, and a
// type argument is erased (D4). Widening an
// assignment by remembering which names were declared `Double` would need a
// scope, and a name matched across scopes would turn a correct `2` into a wrong
// `2.0` -- a worse defect than the one it fixed.
class P(var x: Double)

@main def run(): Unit =
  var v: Double = 1
  v = 2
  val p = new P(0)
  p.x = 3
  val l: List[Double] = List(4, 5)
  println(v.toString + " " + p.x + " " + l)
