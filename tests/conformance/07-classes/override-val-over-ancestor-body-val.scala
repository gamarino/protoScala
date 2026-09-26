// XFAIL: 20 20
// D108. The first line is scalac 3.9's answer. protoScala prints `10 20`: the
// ancestor declares `y` in its *body*, so its initialiser runs after the
// subclass has stored its parameter field and overwrites it, and the override
// is restored only once the chain returns. That store cannot be guarded the way
// a parameter field's is, because a subclass's own body `val` has to be able to
// overwrite an ancestor's. A faithful fix needs one field per class plus a
// virtual accessor, which the single-slot attribute model does not have.
var log = ""
class B:
  val y: Int = 10
  log = "" + this.y
class C(override val y: Int) extends B

@main def run(): Unit =
  val c = new C(20)
  println(log + " " + c.y)
