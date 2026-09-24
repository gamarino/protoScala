// EXPECT: 5 10 9
// A default runs in the CALLEE's prologue, and it may still name an enclosing
// local: the default block mirrors the callee's whole slot prefix — parameters,
// locals and captures — and the capture analysis walks the defaults too, so the
// local is boxed exactly as one the body names would be.
//
// Getting that second half wrong is the interesting failure: a local def is
// HOISTED, so its function object is built before the enclosing `val` runs, and a
// default that captured an unboxed slot read null. It did so only when the body
// happened not to mention the same local — the same program working or not for an
// unrelated reason.
@main def run(): Unit =
  val outside = 5
  def bodyIgnoresIt(a: Int = outside): Int = a
  def bodyAlsoReadsIt(a: Int = outside): Int = a + outside
  def named(a: Int = 1, b: Int = outside + 2): Int = a * b - 5
  println(bodyIgnoresIt().toString + " " + bodyAlsoReadsIt() + " " + named(a = 2))
