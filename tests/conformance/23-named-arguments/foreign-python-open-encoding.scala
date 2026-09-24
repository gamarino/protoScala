// XFAIL: no runtime in the family registers the UMD alias `py`
// The second half of the same hand-off. `io.open(path, mode=, encoding=)` is the
// Python call every tutorial starts with, and it is keyword-only in practice.
//
// Phase 6 implemented the routing: `import py.io` reports `ImportError: no
// provider registered for 'py'`. What is missing is the provider — protoPython
// registers `native`, `python_stdlib`, `compiled` and `hpy`, and installing them
// here means a second runtime in this process (R5). See the long note in
// foreign-python-keyword.scala and ROADMAP's Track Y.
//
// Expected once a `py` provider exists: EXPECT: utf-8
import py.io as io
@main def run(): Unit =
  val f = io.open("/dev/null", mode = "r", encoding = "utf-8")
  println(f.encoding)
