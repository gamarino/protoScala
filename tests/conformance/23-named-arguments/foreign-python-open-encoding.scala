// XFAIL: requires UMD (Phase 6): `import py.` routing is not implemented
// Expected once UMD lands: EXPECT: utf-8
import py.io as io
@main def run(): Unit =
  val f = io.open("/dev/null", mode = "r", encoding = "utf-8")
  println(f.encoding)
