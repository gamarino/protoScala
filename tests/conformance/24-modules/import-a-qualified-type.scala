// EXPECT: Tag(a,1)
// No selector import: the module object carries its nested class under the
// qualified name Phase 4 lifting gave it.
import util.Strings
@main def run(): Unit = println(Strings.Tag("a", 1))
