// EXPECT: 1
// `import M.given` binds nothing and is not an error.
import util.Strings.given
@main def run(): Unit = println(1)
