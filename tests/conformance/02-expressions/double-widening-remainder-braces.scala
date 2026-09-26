// EXPECT: 2 3 List(4, 5)
// The brace-syntax twin of double-widening-remainder.scala (D110). scalac 3.9
// prints `2.0 3.0 List(4.0, 5.0)`.
class P(var x: Double)

@main def run(): Unit = {
  var v: Double = 1
  v = 2
  val p = new P(0)
  p.x = 3
  val l: List[Double] = List(4, 5)
  println(v.toString + " " + p.x + " " + l)
}
