// EXPECT-ERROR: Not found: C
// D41: Scala 3's universal apply (a creator application, `C(args)` standing for
// `new C(args)`) is not synthesised for a plain class; scalac 3.9 compiles this
// and prints 1. Case classes and companions with an `apply` are unaffected.
class C(val a: Int)
@main def run(): Unit = {
  println(C(1).a)
}
