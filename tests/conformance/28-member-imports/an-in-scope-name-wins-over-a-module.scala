// EXPECT-ERROR: util has no member named 'Strings'
// The disambiguation rule, stated as a test: a file that defines `object util`
// gets ITS `util`, and the module file of the same name is not consulted. Scala
// resolves it the same way — a definition in scope shadows a package of that
// name — and `tests/conformance/28-member-imports/_modules/util/Strings.scala`
// does exist, which is what makes this fixture mean something.
object util:
  val marker = 1
import util.Strings
@main def run(): Unit = println(Strings)
