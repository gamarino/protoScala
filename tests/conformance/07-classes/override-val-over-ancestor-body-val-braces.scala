// XFAIL: 20 20
// The brace-syntax twin of override-val-over-ancestor-body-val.scala (D108).
var log = ""
class B { val y: Int = 10; log = "" + this.y }
class C(override val y: Int) extends B

@main def run(): Unit = {
  val c = new C(20)
  println(log + " " + c.y)
}
