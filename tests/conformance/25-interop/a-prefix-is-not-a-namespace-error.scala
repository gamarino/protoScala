// EXPECT-ERROR: no provider registered for 'py'
// The other half: the same file exists at py/Helper.scala relative to the module
// tree, and `import py.Helper` still goes to the provider rather than to it.
import py.Helper
@main def run(): Unit = println(1)
