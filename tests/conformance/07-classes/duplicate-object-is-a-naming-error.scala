// EXPECT-ERROR: A is already defined as object A
// Two templates of one unit declaring the same type name used to share a single
// type key, and the second one's description was then used to compile the first
// -- which escaped as `internal error: unordered_map::at`, the kind of leak D74
// calls a bug. scalac 3.9 reports `E161 Naming Error: A is already defined as
// object A`; protoScala now reports a naming error of its own. A class and its
// companion object still do not collide.
object A { def f = 1 }
object A { def g = 2 }

@main def run(): Unit = println(A.f)
