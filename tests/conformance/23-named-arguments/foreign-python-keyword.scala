// XFAIL: protoPython ships no numpy, and registers no UMD alias `py`
// The motivating case. Python keyword arguments are load-bearing — np.array
// cannot be called without `dtype` — and they cross the boundary with no adapter,
// because a Python callee reached through UMD receives them in protoCore's
// keywordParameters like every other protoCore method.
//
// WHY THIS ONE CANNOT BE CONVERTED, AND st COULD. Track Y made `import st.<module>`
// work by changing protoST's provider alone, and left this fixture XFAIL. The `py.`
// prefix routes: `import py.numpy` asks protoCore for `provider:py` and reports
// `ImportError: no provider registered for 'py'`. What is missing is not
// protoScala's routing. Four facts, read from protoPython's source on 2026-09-24:
//   1. protoPython ships NO numpy. `lib/python3.14/` has 213 entries and none is
//      numpy, so nothing can answer `float64` here. A stand-in dressed as numpy
//      would be a green test asserting numpy behaviour against a stub, which is
//      the failure mode this family already rejected once — so this fixture stays
//      XFAIL even if every other blocker below is removed.
//   2. protoPython registers the aliases `native` (GUID protoPython.native),
//      `python_stdlib`, `compiled` and `hpy` — none of them `py` (INTEROP §3).
//   3. `PythonEnvironment::fromContext(ctx)` IGNORES ctx and returns `s_threadEnv`,
//      a thread_local. protoST's fix was to take the runtime from the provider's
//      own state; protoPython's providers take it from the calling thread, so the
//      equivalent change is not provider-local.
//   4. `PythonEnvironment::getProcessSpace()` is a function-local static
//      `proto::ProtoSpace`, commented "L-Shape: one per process". A co-resident
//      protoScala Session owns its own space, so that premise is false in such a
//      process. This is R5 — a maintainer-owned ruling, not a protoScala change.
// `protoscala` links libprotoCore and libreadline and nothing else of the family,
// so numpy would additionally need a provider plug-in (INTEROP §3.1).
// The cross-repository work is ROADMAP's Track Y, and INTEROP §6.1 is the write-up.
//
// The KEYWORD half of this fixture IS exercised against a real UMD boundary:
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
