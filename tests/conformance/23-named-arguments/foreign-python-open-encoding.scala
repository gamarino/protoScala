// XFAIL: protoPython registers no UMD alias `py` and its environment is not reachable from a foreign caller
// The second half of the same hand-off. `io.open(path, mode=, encoding=)` is the
// Python call every tutorial starts with, and it is keyword-only in practice.
//
// Phase 6 implemented the routing: `import py.io` reports `ImportError: no
// provider registered for 'py'`. What is missing is the provider. Unlike its numpy
// sibling this one is not blocked on missing library code — protoPython DOES ship
// `lib/python3.14/io.py` — it is blocked on the provider and on ownership:
// protoPython registers `native`, `python_stdlib`, `compiled` and `hpy` and no
// `py`; `PythonEnvironment::fromContext` ignores the caller's context and returns a
// thread_local; and `PythonEnvironment::getProcessSpace` is a process-singleton
// ProtoSpace by design, which a co-resident protoScala Session contradicts (R5).
// Track Y made `import st.<module>` work by changing protoST's provider alone; the
// equivalent change here is to PythonEnvironment's ownership model, which is the
// maintainer's. Measured 2026-09-24; see INTEROP §6.1, the longer note in
// foreign-python-keyword.scala, and ROADMAP's Track Y.
//
// Expected once a `py` provider exists: EXPECT: utf-8
import py.io as io
@main def run(): Unit =
  val f = io.open("/dev/null", mode = "r", encoding = "utf-8")
  println(f.encoding)
