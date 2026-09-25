// EXPECT-ERROR: ImportError: no module found for 'Nope'
// A prefix that is neither in scope nor a module still gets Phase 6's message,
// naming every path that was tried. The member-import form must not swallow it
// into something vaguer.
import Nope.*
@main def run(): Unit = println("unreachable")
