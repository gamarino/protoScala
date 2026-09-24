// XFAIL: no runtime in the family registers the UMD alias `py`
// The motivating case. Python keyword arguments are load-bearing — np.array
// cannot be called without `dtype` — and they cross the boundary with no adapter,
// because a Python callee reached through UMD receives them in protoCore's
// keywordParameters like every other protoCore method.
//
// WHAT PHASE 6 DELIVERED, AND WHAT IT DID NOT. The `py.` prefix now routes:
// `import py.numpy` asks protoCore for `provider:py` and reports
// `ImportError: no provider registered for 'py'`. What is missing is not
// protoScala's routing but the provider itself. Three facts, each verified on
// 2026-09-24:
//   1. protoPython registers the aliases `native` (GUID protoPython.native),
//      `python_stdlib`, `compiled` and `hpy` — none of them `py` (INTEROP §3).
//   2. Those providers are installed by PythonEnvironment's initialisation, so
//      having them in this process means constructing a whole second runtime in
//      it, which is R5 — a maintainer-owned risk, not a protoScala change.
//   3. `protoscala` links libprotoCore and libreadline and nothing else of the
//      family, so there is no mechanism by which numpy could be reached.
// The cross-repository work is ROADMAP's Track Y.
//
// The KEYWORD half of this fixture IS now exercised against a real UMD boundary:
// tests/conformance/25-interop/stand-in-provider-keyword.scala sends a named
// argument through a provider loaded by dlopen and reads it back out of
// keywordParameters, and its `-long-keyword` sibling uses a parameter name too
// long to embed in a pointer word, where a non-interned key would fail silently.
// What those fixtures deliberately do NOT do is answer "float64": a stand-in
// dressed as numpy would be a green test asserting numpy behaviour against a
// stub, which is the failure mode this family already learned to reject.
//
// The runner INVERTS an XFAIL verdict and fails loudly when the program
// unexpectedly conforms, so the day a `py` provider appears this fixture goes red
// and says so. That is what keeps the hand-off alive instead of forgotten.
//
// Expected once a `py` provider exists: EXPECT: float64
import py.numpy as np
@main def run(): Unit =
  val a = np.array(List(1, 2, 3), dtype = "float64")
  println(a.dtype.name)
