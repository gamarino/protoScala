// EXPECT: pos=[1,2] kw=[dtype=float64,order=C]
// A callee that is not a Scala method receives named arguments in protoCore's
// keywordParameters, keyed by the address of the interned parameter-name symbol.
// `__kwprobe` reads them exactly as a UMD-provided foreign method will — nothing
// about it is Scala-aware — so this fixture exercises the whole protoScala half of
// the convention without UMD, which is Phase 6.
@main def run(): Unit =
  println(__kwprobe.call(1, 2, dtype = "float64", order = "C"))
