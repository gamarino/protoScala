// EXPECT: 6 6
// D82: an extension is global and session-wide, with no import scoping, so a
// method defined at the top level is visible inside every later definition.
// Scoping needs an import mechanism, which arrives with UMD in Phase 6.
extension (n: Int) def triple: Int = n * 3
def useIt(n: Int): Int = n.triple
@main def run(): Unit = println(useIt(2).toString + " " + 2.triple)
