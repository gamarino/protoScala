// EXPECT: 20 20
// Scala assigns constructor parameter fields before the superclass initialiser
// runs, so `B`'s body sees C's `override val y`, not the 10 it was constructed
// with. scalac 3.9 prints `20 20` for this program; protoScala printed `10 20`
// until parameter fields became STORE_FIELD_IF_NEW -- an ancestor's own
// parameter field was clobbering the override for the length of the chain,
// because a public member's attribute key is its plain name and the two `y`s
// are one slot.
var log = ""
class B(val y: Int):
  log = "" + this.y
class C(override val y: Int) extends B(10)

@main def run(): Unit =
  val c = new C(20)
  println(log + " " + c.y)
