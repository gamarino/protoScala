// EXPECT: 20 20
// The brace-syntax twin of override-val-parameter-seen-by-super.scala.
var log = ""
class B(val y: Int) { log = "" + this.y }
class C(override val y: Int) extends B(10)

@main def run(): Unit = {
  val c = new C(20)
  println(log + " " + c.y)
}
