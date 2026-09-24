// XFAIL: requires UMD (Phase 6): `import py.` routing is not implemented
// The motivating case. Python keyword arguments are load-bearing — np.array
// cannot be called without `dtype` — and they cross the boundary with no adapter,
// because a Python callee reached through UMD receives them in protoCore's
// keywordParameters like every other protoCore method. Phase 6 converts this
// fixture by changing XFAIL to EXPECT; the expected line is recorded here so it is
// not invented then.
// Expected once UMD lands: EXPECT: float64
import py.numpy as np
@main def run(): Unit =
  val a = np.array(List(1, 2, 3), dtype = "float64")
  println(a.dtype.name)
